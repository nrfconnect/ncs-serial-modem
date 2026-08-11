/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/**
 * @file test_at_mqtt.c
 *
 * Unit tests for sm_at_mqtt.c.
 *
 * Covers:
 *   - Basic MQTT flow: AT#XMQTTCFG (set/read), AT#XMQTTCON connect/read/
 *     disconnect, AT#XMQTTPUB, AT#XMQTTSUB, AT#XMQTTUNSUB.
 *   - Reception of an incoming PUBLISH whose payload is delivered by the MQTT
 *     library in several parts, including a -EAGAIN that forces the drain to
 *     span multiple socket poll-callback invocations.
 *
 * The MQTT library is mocked with CMock. The socket poll callback registered
 * via SO_POLLCB is captured from the zsock_setsockopt() mock and invoked from
 * the test to simulate socket-readable events; the resulting work runs on
 * sm_work_q, so k_sleep() is used to let it complete.
 */

#include "unity.h"
#include <zephyr/kernel.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "sm_at_host.h"
#include "uart_stub.h"

/* CMock-generated mocks */
#include "cmock_nrf_modem_at.h"
#include "zephyr/net/cmock_socket.h"
#include "zephyr/net/cmock_mqtt.h"

#include <zephyr/net/socket_ncs.h>
#include <zephyr/net/mqtt.h>

/* Response capture helpers (stubs/sm_at_host_stubs.c) */
extern const char *get_captured_response(void);
extern void clear_captured_response(void);

/* Poll callback registered by sm_at_mqtt.c via SO_POLLCB. */
static socket_ncs_pollcb_t mqtt_pollcb;

/* Simulated incoming PUBLISH, consumed by the mqtt_input/read stubs. */
static const char *pub_topic_in;
static const char *pub_payload_in;
static size_t pub_payload_len;
static size_t pub_payload_off;
static bool eagain_emitted;

/* zsock_setsockopt stub: capture the SO_POLLCB callback, accept everything. */
static int setsockopt_stub(int socket, int level, int option_name,
			   const void *option_value, net_socklen_t option_len, int calls)
{
	if (option_name == SO_POLLCB && option_value != NULL) {
		const struct socket_ncs_pollcb *pcb = option_value;

		mqtt_pollcb = pcb->callback;
	}
	return 0;
}

/* zsock_getaddrinfo stub: resolve any host to a fixed IPv4 address. */
static int getaddrinfo_stub(const char *host, const char *service,
			    const struct zsock_addrinfo *hints,
			    struct zsock_addrinfo **res, int calls)
{
	/* The address storage must be large enough for the full struct
	 * net_sockaddr that util_resolve_host() copies out of ai_addr.
	 */
	static struct {
		struct zsock_addrinfo ai;
		union {
			struct net_sockaddr sa;
			struct net_sockaddr_in sa_in;
			struct net_sockaddr_in6 sa_in6;
		} addr;
	} result;

	memset(&result, 0, sizeof(result));
	result.addr.sa_in.sin_family = AF_INET;
	result.addr.sa_in.sin_port = net_htons(1883);
	result.addr.sa_in.sin_addr.s_addr = net_htonl(0xC0A80001); /* 192.168.0.1 */

	result.ai.ai_family = AF_INET;
	result.ai.ai_socktype = SOCK_STREAM;
	result.ai.ai_protocol = IPPROTO_TCP;
	result.ai.ai_addrlen = sizeof(result.addr.sa_in);
	result.ai.ai_addr = &result.addr.sa;
	result.ai.ai_next = NULL;

	*res = &result.ai;
	return 0;
}

/* mqtt_input stub: deliver a single MQTT_EVT_PUBLISH to the client callback. */
static int mqtt_input_stub(struct mqtt_client *client, int calls)
{
	struct mqtt_evt evt;

	memset(&evt, 0, sizeof(evt));
	evt.type = MQTT_EVT_PUBLISH;
	evt.result = 0;
	evt.param.publish.message.topic.qos = MQTT_QOS_0_AT_MOST_ONCE;
	evt.param.publish.message.topic.topic.utf8 = (uint8_t *)pub_topic_in;
	evt.param.publish.message.topic.topic.size = strlen(pub_topic_in);
	evt.param.publish.message.payload.len = pub_payload_len;
	evt.param.publish.message_id = 1;

	client->evt_cb(client, &evt);
	return 0;
}

/* mqtt_read_publish_payload stub: hand back the payload in 5-byte chunks and
 * return -EAGAIN once at the halfway point so the drain must resume on a later
 * poll-callback invocation.
 */
static int mqtt_read_payload_stub(struct mqtt_client *client, void *buffer, size_t length,
				  int calls)
{
	size_t remaining = pub_payload_len - pub_payload_off;
	size_t chunk;

	if (remaining == 0) {
		return 0;
	}

	if (!eagain_emitted && pub_payload_off >= pub_payload_len / 2) {
		eagain_emitted = true;
		return -EAGAIN;
	}

	chunk = MIN(remaining, (size_t)5);
	chunk = MIN(chunk, length);
	memcpy(buffer, pub_payload_in + pub_payload_off, chunk);
	pub_payload_off += chunk;
	return (int)chunk;
}

void setUp(void)
{
	clear_captured_response();

	mqtt_pollcb = NULL;
	pub_topic_in = NULL;
	pub_payload_in = NULL;
	pub_payload_len = 0;
	pub_payload_off = 0;
	eagain_emitted = false;

	__cmock_zsock_setsockopt_Stub(setsockopt_stub);
	__cmock_mqtt_keepalive_time_left_IgnoreAndReturn(60000);
}

void tearDown(void)
{
	/* Let any pending sm_work_q work run before the next test. */
	k_sleep(K_MSEC(1));

	__cmock_zsock_setsockopt_Stub(NULL);
	__cmock_zsock_getaddrinfo_Stub(NULL);
	__cmock_mqtt_input_Stub(NULL);
	__cmock_mqtt_read_publish_payload_Stub(NULL);
}

/* Connect to the broker (non-secure) and assert success. */
static void mqtt_connect_ok(void)
{
	const char *resp;

	__cmock_zsock_getaddrinfo_Stub(getaddrinfo_stub);
	__cmock_zsock_freeaddrinfo_Ignore();
	__cmock_mqtt_connect_ExpectAnyArgsAndReturn(0);

	send_at_command("AT#XMQTTCON=1,\"user\",\"pass\",\"broker.example.com\",1883\r\n");

	resp = get_captured_response();
	TEST_ASSERT_NOT_NULL(strstr(resp, "OK"));
	TEST_ASSERT_NOT_NULL(mqtt_pollcb);
}

/*
 * Test: Basic MQTT command flow.
 * - Configure, connect, publish, subscribe, unsubscribe, disconnect.
 */
void test_mqtt_basic(void)
{
	const char *resp;

	/* Configure client id, keep-alive and clean-session. */
	send_at_command("AT#XMQTTCFG=\"test_client\",60,1\r\n");
	resp = get_captured_response();
	TEST_ASSERT_NOT_NULL(strstr(resp, "OK"));

	/* Read back the configuration. */
	clear_captured_response();
	send_at_command("AT#XMQTTCFG?\r\n");
	resp = get_captured_response();
	TEST_ASSERT_NOT_NULL(strstr(resp, "#XMQTTCFG:"));
	TEST_ASSERT_NOT_NULL(strstr(resp, "test_client"));
	TEST_ASSERT_NOT_NULL(strstr(resp, "OK"));

	/* Connect to the broker. */
	clear_captured_response();
	mqtt_connect_ok();

	/* Read the connection status. */
	clear_captured_response();
	send_at_command("AT#XMQTTCON?\r\n");
	resp = get_captured_response();
	TEST_ASSERT_NOT_NULL(strstr(resp, "#XMQTTCON: 1"));
	TEST_ASSERT_NOT_NULL(strstr(resp, "broker.example.com"));

	/* Publish a message. */
	clear_captured_response();
	__cmock_mqtt_publish_ExpectAnyArgsAndReturn(0);
	send_at_command("AT#XMQTTPUB=\"my/topic\",\"hello\",0,0\r\n");
	resp = get_captured_response();
	TEST_ASSERT_NOT_NULL(strstr(resp, "OK"));

	/* Subscribe to a topic. */
	clear_captured_response();
	__cmock_mqtt_subscribe_ExpectAnyArgsAndReturn(0);
	send_at_command("AT#XMQTTSUB=\"my/topic\",0\r\n");
	resp = get_captured_response();
	TEST_ASSERT_NOT_NULL(strstr(resp, "OK"));

	/* Unsubscribe. */
	clear_captured_response();
	__cmock_mqtt_unsubscribe_ExpectAnyArgsAndReturn(0);
	send_at_command("AT#XMQTTUNSUB=\"my/topic\"\r\n");
	resp = get_captured_response();
	TEST_ASSERT_NOT_NULL(strstr(resp, "OK"));

	/* Disconnect. */
	clear_captured_response();
	__cmock_mqtt_disconnect_ExpectAnyArgsAndReturn(0);
	send_at_command("AT#XMQTTCON=0\r\n");
	resp = get_captured_response();
	TEST_ASSERT_NOT_NULL(strstr(resp, "OK"));
}

/*
 * Test: Incoming PUBLISH whose payload is delivered in several parts.
 * - The read stub returns the payload in small chunks and injects one -EAGAIN,
 *   forcing the drain to resume on a second poll-callback invocation.
 * - The full topic, full payload and the terminating #XMQTTEVT must be output.
 */
void test_mqtt_publish_received_in_parts(void)
{
	const char *resp;
	struct socket_ncs_pollcb_params params = {
		.fd = 0,
		.events = ZSOCK_POLLIN,
		.revents = ZSOCK_POLLIN,
	};

	mqtt_connect_ok();

	pub_topic_in = "device/data";
	pub_payload_in = "0123456789ABCDEFGHIJ"; /* 20 bytes */
	pub_payload_len = strlen(pub_payload_in);
	pub_payload_off = 0;
	eagain_emitted = false;

	__cmock_mqtt_input_Stub(mqtt_input_stub);
	__cmock_mqtt_read_publish_payload_Stub(mqtt_read_payload_stub);

	clear_captured_response();

	/* First readable event: PUBLISH header + topic, drain until -EAGAIN. */
	TEST_ASSERT_NOT_NULL(mqtt_pollcb);
	mqtt_pollcb(&params);
	k_sleep(K_MSEC(20));

	/* Second readable event: resume and finish draining the payload. */
	TEST_ASSERT_NOT_NULL(mqtt_pollcb);
	mqtt_pollcb(&params);
	k_sleep(K_MSEC(20));

	resp = get_captured_response();

	const char *hdr = strstr(resp, "#XMQTTMSG:");
	const char *topic = strstr(resp, "device/data");
	const char *payload = strstr(resp, "0123456789ABCDEFGHIJ");
	const char *evt = strstr(resp, "#XMQTTEVT:");

	TEST_ASSERT_NOT_NULL(hdr);
	TEST_ASSERT_NOT_NULL(topic);
	TEST_ASSERT_NOT_NULL(payload);
	TEST_ASSERT_NOT_NULL(evt);

	/* Framing order: length header before topic/payload, completion event last. */
	TEST_ASSERT_TRUE(hdr < topic);
	TEST_ASSERT_TRUE(topic < payload);
	TEST_ASSERT_TRUE(payload < evt);

	/* The whole payload must have been consumed from the library. */
	TEST_ASSERT_EQUAL_UINT(pub_payload_len, pub_payload_off);

	/* Drain the stale tx_done semaphore credit left by the poll-work transmits.
	 * mqtt_pollcb() was called directly (no uart_stub_rx waiter), so tx_done_work
	 * fired without being consumed, which would cause the next send_at_command to
	 * return before its response is ready.
	 */
	uart_stub_tx_done_drain();

	/* Disconnect. */
	clear_captured_response();
	__cmock_mqtt_disconnect_ExpectAnyArgsAndReturn(0);
	send_at_command("AT#XMQTTCON=0\r\n");
	resp = get_captured_response();
	TEST_ASSERT_NOT_NULL(strstr(resp, "OK"));
}

extern int unity_main(void);

int main(void)
{
	(void)unity_main();

	return 0;
}
