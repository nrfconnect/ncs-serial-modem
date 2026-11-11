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
extern size_t get_captured_response_len(void);
extern void clear_captured_response(void);
extern void mqtt_evt_handler(struct mqtt_client *const c, const struct mqtt_evt *evt);

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

static uint8_t connect_protocol_version;
static struct mqtt_client *connected_client;

static int mqtt_connect_version_stub(struct mqtt_client *client, int num_calls)
{
	ARG_UNUSED(num_calls);

	connect_protocol_version = client->protocol_version;
	connected_client = client;
	return 0;
}

/* Configure with cfg_cmd, connect, and check the CONNECT protocol version. */
static void mqtt_connect_version(const char *cfg_cmd, uint8_t expected_version)
{
	const char *resp;

	connect_protocol_version = 0;

	send_at_command(cfg_cmd);
	resp = get_captured_response();
	TEST_ASSERT_NOT_NULL(strstr(resp, "OK"));

	clear_captured_response();
	__cmock_zsock_getaddrinfo_Stub(getaddrinfo_stub);
	__cmock_zsock_freeaddrinfo_Ignore();
	__cmock_mqtt_connect_Stub(mqtt_connect_version_stub);
	send_at_command("AT#XMQTTCON=1,\"user\",\"pass\",\"broker.example.com\",1883\r\n");
	resp = get_captured_response();
	TEST_ASSERT_NOT_NULL(strstr(resp, "OK"));
	TEST_ASSERT_EQUAL_UINT8(expected_version, connect_protocol_version);
	__cmock_mqtt_connect_Stub(NULL);
	clear_captured_response();
}

static void mqtt_disconnect_ok(void)
{
	const char *resp;

	clear_captured_response();
	__cmock_mqtt_disconnect_ExpectAnyArgsAndReturn(0);
	send_at_command("AT#XMQTTCON=0\r\n");
	resp = get_captured_response();
	TEST_ASSERT_NOT_NULL(strstr(resp, "OK"));
	clear_captured_response();
}

/* Configure with cfg_cmd, connect, check the CONNECT protocol version, disconnect. */
static void mqtt_connect_with_version(const char *cfg_cmd, uint8_t expected_version)
{
	mqtt_connect_version(cfg_cmd, expected_version);
	mqtt_disconnect_ok();
}

static void assert_mqtt_utf8(const char *expected, const struct mqtt_utf8 *actual)
{
	TEST_ASSERT_NOT_NULL(actual);
	TEST_ASSERT_EQUAL_UINT(strlen(expected), actual->size);
	TEST_ASSERT_EQUAL_MEMORY(expected, actual->utf8, actual->size);
}

static int forwarding_getaddrinfo_stub(const char *host, const char *service,
				       const struct zsock_addrinfo *hints,
				       struct zsock_addrinfo **res, int calls)
{
	TEST_ASSERT_EQUAL_STRING("forward.example.com", host);
	TEST_ASSERT_EQUAL_STRING("2883:0", service);
	TEST_ASSERT_EQUAL_INT(AF_INET, hints->ai_family);

	int ret = getaddrinfo_stub(host, service, hints, res, calls);
	struct net_sockaddr_in *addr = (struct net_sockaddr_in *)(*res)->ai_addr;

	addr->sin_port = net_htons(2883);
	return ret;
}

static void connect_parameter_forwarding(uint8_t version)
{
	static const struct {
		const char *command;
		const char *username;
		const char *password;
	} cases[] = {
		{"AT#XMQTTCON=1,\"alice\",\"secret\",\"forward.example.com\",2883\r\n", "alice",
		 "secret"},
		{"AT#XMQTTCON=1,\"bob\",\"\",\"forward.example.com\",2883\r\n", "bob", NULL},
		{"AT#XMQTTCON=1,\"\",\"ignored\",\"forward.example.com\",2883\r\n", NULL, NULL},
		{"AT#XMQTTCON=1,\"\",\"\",\"forward.example.com\",2883\r\n", NULL, NULL},
	};
	char command[96];

	for (size_t i = 0; i < ARRAY_SIZE(cases); i++) {
		snprintf(command, sizeof(command), "AT#XMQTTCFG=\"forward-client\",123,%u,%u\r\n",
			 (unsigned int)(i % 2), version);
		clear_captured_response();
		send_at_command(command);
		TEST_ASSERT_NOT_NULL(strstr(get_captured_response(), "OK"));
		clear_captured_response();
		__cmock_zsock_getaddrinfo_Stub(forwarding_getaddrinfo_stub);
		__cmock_zsock_freeaddrinfo_Ignore();
		__cmock_mqtt_connect_Stub(mqtt_connect_version_stub);
		connected_client = NULL;
		send_at_command(cases[i].command);
		TEST_ASSERT_NOT_NULL(strstr(get_captured_response(), "OK"));
		TEST_ASSERT_NOT_NULL(connected_client);
		TEST_ASSERT_EQUAL_UINT8(version, connected_client->protocol_version);
		assert_mqtt_utf8("forward-client", &connected_client->client_id);
		TEST_ASSERT_EQUAL_UINT16(123, connected_client->keepalive);
		TEST_ASSERT_EQUAL_UINT8(i % 2, connected_client->clean_session);
		if (cases[i].username != NULL) {
			assert_mqtt_utf8(cases[i].username, connected_client->user_name);
		} else {
			TEST_ASSERT_NULL(connected_client->user_name);
		}
		if (cases[i].password != NULL) {
			assert_mqtt_utf8(cases[i].password, connected_client->password);
		} else {
			TEST_ASSERT_NULL(connected_client->password);
		}
		TEST_ASSERT_EQUAL_INT(MQTT_TRANSPORT_NON_SECURE, connected_client->transport.type);
		const struct net_sockaddr_in *broker = connected_client->broker;

		TEST_ASSERT_NOT_NULL(broker);
		TEST_ASSERT_EQUAL_INT(AF_INET, broker->sin_family);
		TEST_ASSERT_EQUAL_UINT16(net_htons(2883), broker->sin_port);
		TEST_ASSERT_EQUAL_HEX32(net_htonl(0xC0A80001), broker->sin_addr.s_addr);
		TEST_ASSERT_NOT_NULL(connected_client->rx_buf);
		TEST_ASSERT_NOT_NULL(connected_client->tx_buf);
		TEST_ASSERT_EQUAL_UINT(CONFIG_SM_MQTTC_MESSAGE_BUFFER_LEN,
				       connected_client->rx_buf_size);
		TEST_ASSERT_EQUAL_UINT(CONFIG_SM_MQTTC_MESSAGE_BUFFER_LEN,
				       connected_client->tx_buf_size);
		TEST_ASSERT_TRUE(connected_client->evt_cb == mqtt_evt_handler);
		__cmock_mqtt_connect_Stub(NULL);
		mqtt_disconnect_ok();
	}
}

void test_mqtt_connect_parameter_forwarding(void)
{
	connect_parameter_forwarding(MQTT_VERSION_3_1_1);
}

void test_mqtt_connect_parameter_forwarding_5_0(void)
{
#if defined(CONFIG_MQTT_VERSION_5_0)
	connect_parameter_forwarding(MQTT_VERSION_5_0);
#else
	TEST_IGNORE_MESSAGE("Requires CONFIG_MQTT_VERSION_5_0");
#endif
}

static uint8_t forwarding_qos;
static uint8_t forwarding_retain;
static uint16_t forwarding_message_id;
static unsigned int forwarding_calls;

static int forwarding_publish_stub(struct mqtt_client *client,
				   const struct mqtt_publish_param *param, int calls)
{
	ARG_UNUSED(calls);

	TEST_ASSERT_EQUAL_PTR(connected_client, client);
	assert_mqtt_utf8("out/topic", &param->message.topic.topic);
	TEST_ASSERT_EQUAL_UINT8(forwarding_qos, param->message.topic.qos);
	TEST_ASSERT_EQUAL_UINT8(forwarding_retain, param->retain_flag);
	TEST_ASSERT_EQUAL_UINT8(0, param->dup_flag);
	TEST_ASSERT_EQUAL_UINT(7, param->message.payload.len);
	TEST_ASSERT_EQUAL_MEMORY("payload", param->message.payload.data, 7);
	TEST_ASSERT_NOT_EQUAL(0, param->message_id);
	if (forwarding_calls > 0) {
		TEST_ASSERT_EQUAL_UINT16(forwarding_message_id + 1, param->message_id);
	}
	forwarding_message_id = param->message_id;
	forwarding_calls++;
	return 0;
}

static void publish_parameter_forwarding(uint8_t version)
{
	char command[96];

	snprintf(command, sizeof(command), "AT#XMQTTCFG=\"test_client\",60,1,%u\r\n", version);
	mqtt_connect_version(command, version);
	forwarding_calls = 0;
	forwarding_qos = 0;
	forwarding_retain = 0;
	__cmock_mqtt_publish_Stub(forwarding_publish_stub);
	send_at_command("AT#XMQTTPUB=\"out/topic\",\"payload\"\r\n");
	TEST_ASSERT_NOT_NULL(strstr(get_captured_response(), "OK"));
	for (uint8_t qos = 0; qos <= 2; qos++) {
		for (uint8_t retain = 0; retain <= 1; retain++) {
			forwarding_qos = qos;
			forwarding_retain = retain;
			snprintf(command, sizeof(command),
				 "AT#XMQTTPUB=\"out/topic\",\"payload\",%u,%u\r\n", qos, retain);
			clear_captured_response();
			send_at_command(command);
			TEST_ASSERT_NOT_NULL(strstr(get_captured_response(), "OK"));
		}
	}
	TEST_ASSERT_EQUAL_UINT(7, forwarding_calls);
	__cmock_mqtt_publish_Stub(NULL);
	mqtt_disconnect_ok();
}

void test_mqtt_publish_parameter_forwarding(void)
{
	publish_parameter_forwarding(MQTT_VERSION_3_1_1);
#if defined(CONFIG_MQTT_VERSION_5_0)
	publish_parameter_forwarding(MQTT_VERSION_5_0);
#endif
}

static int forwarding_subscription_stub(struct mqtt_client *client,
					const struct mqtt_subscription_list *param, int calls)
{
	ARG_UNUSED(calls);

	TEST_ASSERT_EQUAL_PTR(connected_client, client);
	TEST_ASSERT_EQUAL_UINT(1, param->list_count);
	TEST_ASSERT_NOT_NULL(param->list);
	assert_mqtt_utf8("sensors/+/data/#", &param->list[0].topic);
	TEST_ASSERT_EQUAL_UINT8(forwarding_qos, param->list[0].qos);
	TEST_ASSERT_NOT_EQUAL(0, param->message_id);
	if (forwarding_calls > 0) {
		TEST_ASSERT_EQUAL_UINT16(forwarding_message_id + 1, param->message_id);
	}
	forwarding_message_id = param->message_id;
	forwarding_calls++;
	return 0;
}

static void subscription_parameter_forwarding(uint8_t version)
{
	char command[80];

	snprintf(command, sizeof(command), "AT#XMQTTCFG=\"test_client\",60,1,%u\r\n", version);
	mqtt_connect_version(command, version);
	forwarding_calls = 0;
	__cmock_mqtt_subscribe_Stub(forwarding_subscription_stub);
	__cmock_mqtt_unsubscribe_Stub(forwarding_subscription_stub);
	for (uint8_t qos = 0; qos <= 2; qos++) {
		forwarding_qos = qos;
		snprintf(command, sizeof(command), "AT#XMQTTSUB=\"sensors/+/data/#\",%u\r\n", qos);
		clear_captured_response();
		send_at_command(command);
		TEST_ASSERT_NOT_NULL(strstr(get_captured_response(), "OK"));
	}
	forwarding_qos = 0;
	clear_captured_response();
	send_at_command("AT#XMQTTUNSUB=\"sensors/+/data/#\"\r\n");
	TEST_ASSERT_NOT_NULL(strstr(get_captured_response(), "OK"));
	TEST_ASSERT_EQUAL_UINT(4, forwarding_calls);
	__cmock_mqtt_subscribe_Stub(NULL);
	__cmock_mqtt_unsubscribe_Stub(NULL);
	mqtt_disconnect_ok();
}

void test_mqtt_subscription_parameter_forwarding(void)
{
	subscription_parameter_forwarding(MQTT_VERSION_3_1_1);
#if defined(CONFIG_MQTT_VERSION_5_0)
	subscription_parameter_forwarding(MQTT_VERSION_5_0);
#endif
}

/* Pass evt to the MQTT event handler and return the URC output. */
static const char *inject_mqtt_evt(const struct mqtt_evt *evt)
{
	clear_captured_response();
	mqtt_evt_handler(NULL, evt);
	k_sleep(K_MSEC(50));
	/* The URC was sent from the work queue without a waiter for its tx_done credit. */
	uart_stub_tx_done_reset();

	return get_captured_response();
}

static void assert_mqtt_disconnected(void)
{
	clear_captured_response();
	send_at_command("AT#XMQTTCON?\r\n");
	TEST_ASSERT_NOT_NULL(strstr(get_captured_response(), "#XMQTTCON: 0\r\n"));

	clear_captured_response();
	send_at_command("AT#XMQTTPUB=\"my/topic\",\"hello\",0,0\r\n");
	TEST_ASSERT_NOT_NULL(strstr(get_captured_response(), "ERROR"));
}

static void assert_mqtt_reconnects(void)
{
	clear_captured_response();
	mqtt_connect_ok();
	mqtt_disconnect_ok();
}

void test_mqtt_dns_failure_recovery(void)
{
	send_at_command("AT#XMQTTCFG=\"test_client\",60,1,4\r\n");
	clear_captured_response();
	__cmock_zsock_getaddrinfo_ExpectAnyArgsAndReturn(DNS_EAI_AGAIN);
	__cmock_zsock_gai_strerror_ExpectAndReturn(DNS_EAI_AGAIN, "Temporary failure");
	send_at_command("AT#XMQTTCON=1,\"user\",\"pass\",\"broker.example.com\",1883\r\n");
	TEST_ASSERT_NOT_NULL(strstr(get_captured_response(), "ERROR"));
	assert_mqtt_disconnected();
	assert_mqtt_reconnects();
}

void test_mqtt_connect_failure_recovery(void)
{
	send_at_command("AT#XMQTTCFG=\"test_client\",60,1,4\r\n");
	clear_captured_response();
	__cmock_zsock_getaddrinfo_Stub(getaddrinfo_stub);
	__cmock_zsock_freeaddrinfo_Ignore();
	__cmock_mqtt_connect_ExpectAnyArgsAndReturn(-ECONNREFUSED);
	send_at_command("AT#XMQTTCON=1,\"user\",\"pass\",\"broker.example.com\",1883\r\n");
	TEST_ASSERT_NOT_NULL(strstr(get_captured_response(), "ERROR"));
	assert_mqtt_disconnected();
	assert_mqtt_reconnects();
}

static void mqtt_poll_failure(short revents)
{
	struct socket_ncs_pollcb_params params = {
		.fd = 0,
		.events = ZSOCK_POLLIN,
		.revents = revents,
	};

	mqtt_connect_version("AT#XMQTTCFG=\"test_client\",60,1,4\r\n", MQTT_VERSION_3_1_1);
	__cmock_mqtt_abort_ExpectAnyArgsAndReturn(0);
	TEST_ASSERT_NOT_NULL(mqtt_pollcb);
	mqtt_pollcb(&params);
	k_sleep(K_MSEC(50));
	uart_stub_tx_done_drain();
	TEST_ASSERT_NULL(connected_client->rx_buf);
	TEST_ASSERT_NULL(connected_client->tx_buf);
	TEST_ASSERT_NULL(connected_client->broker);
	assert_mqtt_disconnected();
	assert_mqtt_reconnects();
}

void test_mqtt_poll_error_recovery(void)
{
	mqtt_poll_failure(ZSOCK_POLLERR);
}

void test_mqtt_poll_hangup_recovery(void)
{
	mqtt_poll_failure(ZSOCK_POLLHUP);
}

void test_mqtt_poll_invalid_recovery(void)
{
	mqtt_poll_failure(ZSOCK_POLLNVAL);
}

void test_mqtt_input_failure_recovery(void)
{
	__cmock_mqtt_input_ExpectAnyArgsAndReturn(-EBADMSG);
	mqtt_poll_failure(ZSOCK_POLLIN);
}

void test_mqtt_disconnect_failure_recovery(void)
{
	mqtt_connect_version("AT#XMQTTCFG=\"test_client\",60,1,4\r\n", MQTT_VERSION_3_1_1);
	__cmock_mqtt_disconnect_ExpectAnyArgsAndReturn(-ENOTCONN);
	__cmock_mqtt_abort_ExpectAnyArgsAndReturn(0);
	send_at_command("AT#XMQTTCON=0\r\n");
	TEST_ASSERT_NOT_NULL(strstr(get_captured_response(), "OK"));
	TEST_ASSERT_NULL(connected_client->rx_buf);
	TEST_ASSERT_NULL(connected_client->broker);
	assert_mqtt_disconnected();
	assert_mqtt_reconnects();
}

void test_mqtt_broker_disconnect_recovery(void)
{
	struct mqtt_evt evt = {.type = MQTT_EVT_DISCONNECT};
	const char *resp;

	mqtt_connect_version("AT#XMQTTCFG=\"test_client\",60,1,4\r\n", MQTT_VERSION_3_1_1);
	resp = inject_mqtt_evt(&evt);
	TEST_ASSERT_NOT_NULL(strstr(resp, "#XMQTTEVT: 1,0\r\n"));
	assert_mqtt_disconnected();
	assert_mqtt_reconnects();
}

/*
 * Test: The client connects using MQTT 3.1.1 when the version is omitted.
 */
void test_mqtt_connect_default_version(void)
{
	mqtt_connect_with_version("AT#XMQTTCFG=\"test_client\",60,1\r\n", MQTT_VERSION_3_1_1);
}

/*
 * Test: The client connects using MQTT 3.1.1 when version 4 is given.
 */
void test_mqtt_connect_version_3_1_1(void)
{
	mqtt_connect_with_version("AT#XMQTTCFG=\"test_client\",60,1,4\r\n", MQTT_VERSION_3_1_1);
}

/*
 * Test: Version 5 is accepted only when the library supports MQTT 5.0.
 */
void test_mqtt_connect_version_5_0(void)
{
#if defined(CONFIG_MQTT_VERSION_5_0)
	const char *resp;

	mqtt_connect_with_version("AT#XMQTTCFG=\"test_client\",60,1,5\r\n", MQTT_VERSION_5_0);

	send_at_command("AT#XMQTTCFG?\r\n");
	resp = get_captured_response();
	TEST_ASSERT_NOT_NULL(strstr(resp, "#XMQTTCFG: \"test_client\",60,1,5"));
#else
	const char *resp;

	send_at_command("AT#XMQTTCFG=\"test_client\",60,1,5\r\n");
	resp = get_captured_response();
	TEST_ASSERT_NOT_NULL(strstr(resp, "ERROR"));
#endif
}

/*
 * Test: Unsupported version values are rejected and the read command
 * reports the version.
 */
void test_mqtt_config_version(void)
{
	const char *resp;

	send_at_command("AT#XMQTTCFG=\"test_client\",60,1,3\r\n");
	resp = get_captured_response();
	TEST_ASSERT_NOT_NULL(strstr(resp, "ERROR"));

	clear_captured_response();
	send_at_command("AT#XMQTTCFG=\"test_client\",60,1,6\r\n");
	resp = get_captured_response();
	TEST_ASSERT_NOT_NULL(strstr(resp, "ERROR"));

	clear_captured_response();
	send_at_command("AT#XMQTTCFG=\"test_client\",30,0\r\n");
	resp = get_captured_response();
	TEST_ASSERT_NOT_NULL(strstr(resp, "OK"));

	clear_captured_response();
	send_at_command("AT#XMQTTCFG?\r\n");
	resp = get_captured_response();
	TEST_ASSERT_NOT_NULL(strstr(resp, "#XMQTTCFG: \"test_client\",30,0,4,0\r\n"));

	clear_captured_response();
	send_at_command("AT#XMQTTCFG=?\r\n");
	resp = get_captured_response();
	TEST_ASSERT_NOT_NULL(strstr(resp, "#XMQTTCFG: <client_id>,<keep_alive>,<clean_session>,"
					  "<version>,<session_expiry>"));
}

/*
 * Test: A session expiry interval is accepted only with MQTT 5.0.
 */
void test_mqtt_config_session_expiry(void)
{
	const char *resp;

	send_at_command("AT#XMQTTCFG=\"test_client\",60,0,4,300\r\n");
	resp = get_captured_response();
	TEST_ASSERT_NOT_NULL(strstr(resp, "ERROR"));

	clear_captured_response();
	send_at_command("AT#XMQTTCFG=\"test_client\",60,0,4,0\r\n");
	resp = get_captured_response();
	TEST_ASSERT_NOT_NULL(strstr(resp, "OK"));

	clear_captured_response();
	send_at_command("AT#XMQTTCFG=\"test_client\",60,0,5,300\r\n");
	resp = get_captured_response();
#if defined(CONFIG_MQTT_VERSION_5_0)
	TEST_ASSERT_NOT_NULL(strstr(resp, "OK"));

	clear_captured_response();
	send_at_command("AT#XMQTTCFG?\r\n");
	resp = get_captured_response();
	TEST_ASSERT_NOT_NULL(strstr(resp, "#XMQTTCFG: \"test_client\",60,0,5,300\r\n"));

	/* Reconfiguring without the parameter resets the interval. */
	clear_captured_response();
	send_at_command("AT#XMQTTCFG=\"test_client\",60,0,5\r\n");
	resp = get_captured_response();
	TEST_ASSERT_NOT_NULL(strstr(resp, "OK"));

	clear_captured_response();
	send_at_command("AT#XMQTTCFG?\r\n");
	resp = get_captured_response();
	TEST_ASSERT_NOT_NULL(strstr(resp, "#XMQTTCFG: \"test_client\",60,0,5,0\r\n"));
#else
	TEST_ASSERT_NOT_NULL(strstr(resp, "ERROR"));
#endif
}

/*
 * Test: MQTT 3.1.1 events keep the two-field #XMQTTEVT format.
 */
void test_mqtt_evt_no_reason_code_3_1_1(void)
{
	const char *resp;
	struct mqtt_evt evt = {
		.type = MQTT_EVT_PUBACK,
		.result = 0,
		.param.puback.message_id = 1,
	};
	char expected[32];

	mqtt_connect_version("AT#XMQTTCFG=\"test_client\",60,1,4\r\n", MQTT_VERSION_3_1_1);

	resp = inject_mqtt_evt(&evt);
	snprintf(expected, sizeof(expected), "#XMQTTEVT: %d,0\r\n", MQTT_EVT_PUBACK);
	TEST_ASSERT_NOT_NULL(strstr(resp, expected));

	evt = (struct mqtt_evt){
		.type = MQTT_EVT_CONNACK,
		.param.connack.session_present_flag = 1,
	};
	resp = inject_mqtt_evt(&evt);
	TEST_ASSERT_NOT_NULL(strstr(resp, "#XMQTTEVT: 0,0\r\n"));
	evt.param.connack.session_present_flag = 0;
	resp = inject_mqtt_evt(&evt);
	TEST_ASSERT_NOT_NULL(strstr(resp, "#XMQTTEVT: 0,0\r\n"));

	mqtt_disconnect_ok();
}

void test_mqtt_connack_session_present_5_0(void)
{
#if defined(CONFIG_MQTT_VERSION_5_0)
	const char *resp;
	struct mqtt_evt evt = {.type = MQTT_EVT_CONNACK};

	mqtt_connect_version("AT#XMQTTCFG=\"test_client\",60,0,5,300\r\n", MQTT_VERSION_5_0);

	resp = inject_mqtt_evt(&evt);
	TEST_ASSERT_NOT_NULL(strstr(resp, "#XMQTTEVT: 0,0,0\r\n"));

	evt.param.connack.session_present_flag = 1;
	resp = inject_mqtt_evt(&evt);
	TEST_ASSERT_NOT_NULL(strstr(resp, "#XMQTTEVT: 0,0,1\r\n"));

	mqtt_disconnect_ok();

	evt.result = 0x89;
	resp = inject_mqtt_evt(&evt);
	TEST_ASSERT_NOT_NULL(strstr(resp, "#XMQTTEVT: 0,137\r\n"));

	evt.result = -EINVAL;
	resp = inject_mqtt_evt(&evt);
	TEST_ASSERT_NOT_NULL(strstr(resp, "#XMQTTEVT: 0,-22\r\n"));
#else
	TEST_IGNORE_MESSAGE("Requires CONFIG_MQTT_VERSION_5_0");
#endif
}

void test_mqtt_server_keep_alive(void)
{
#if defined(CONFIG_MQTT_VERSION_5_0)
	struct mqtt_evt evt = {.type = MQTT_EVT_CONNACK};
	const char *resp;

	mqtt_connect_version("AT#XMQTTCFG=\"test_client\",60,1,5\r\n", MQTT_VERSION_5_0);

	evt.param.connack.prop.server_keep_alive = 10;
	inject_mqtt_evt(&evt);
	TEST_ASSERT_EQUAL_UINT16(60, connected_client->keepalive);

	evt.param.connack.prop.rx.has_server_keep_alive = true;
	inject_mqtt_evt(&evt);
	TEST_ASSERT_EQUAL_UINT16(10, connected_client->keepalive);

	clear_captured_response();
	send_at_command("AT#XMQTTCFG?\r\n");
	resp = get_captured_response();
	TEST_ASSERT_NOT_NULL(strstr(resp, "#XMQTTCFG: \"test_client\",60,1,5,0\r\n"));

	evt.param.connack.prop.server_keep_alive = 120;
	inject_mqtt_evt(&evt);
	TEST_ASSERT_EQUAL_UINT16(120, connected_client->keepalive);

	evt.param.connack.prop.server_keep_alive = 0;
	inject_mqtt_evt(&evt);
	TEST_ASSERT_EQUAL_UINT16(0, connected_client->keepalive);
	mqtt_disconnect_ok();

	clear_captured_response();
	__cmock_zsock_getaddrinfo_Stub(getaddrinfo_stub);
	__cmock_zsock_freeaddrinfo_Ignore();
	__cmock_mqtt_connect_Stub(mqtt_connect_version_stub);
	send_at_command("AT#XMQTTCON=1,\"user\",\"pass\",\"broker.example.com\",1883\r\n");
	TEST_ASSERT_NOT_NULL(strstr(get_captured_response(), "OK"));
	TEST_ASSERT_EQUAL_UINT16(60, connected_client->keepalive);
	__cmock_mqtt_connect_Stub(NULL);
	mqtt_disconnect_ok();

	mqtt_connect_version("AT#XMQTTCFG=\"test_client\",60,1,5\r\n", MQTT_VERSION_5_0);
	evt.result = 0x89;
	evt.param.connack.prop.server_keep_alive = 10;
	inject_mqtt_evt(&evt);
	TEST_ASSERT_EQUAL_UINT16(60, connected_client->keepalive);

	mqtt_connect_version("AT#XMQTTCFG=\"test_client\",60,1,4\r\n", MQTT_VERSION_3_1_1);
	evt.result = 0;
	inject_mqtt_evt(&evt);
	TEST_ASSERT_EQUAL_UINT16(60, connected_client->keepalive);
	mqtt_disconnect_ok();
#else
	TEST_IGNORE_MESSAGE("Requires CONFIG_MQTT_VERSION_5_0");
#endif
}

#if defined(CONFIG_MQTT_VERSION_5_0)
static K_SEM_DEFINE(keepalive_calls, 0, 10);
static uint16_t server_keepalive;

static int keepalive_time_left_stub(const struct mqtt_client *client, int calls)
{
	ARG_UNUSED(calls);

	return client->keepalive == 0 ? -1 : client->keepalive * 1000;
}

static int keepalive_live_stub(struct mqtt_client *client, int calls)
{
	ARG_UNUSED(calls);

	TEST_ASSERT_EQUAL_UINT16(server_keepalive, client->keepalive);
	k_sem_give(&keepalive_calls);
	return 0;
}

static int keepalive_connack_stub(struct mqtt_client *client, int calls)
{
	struct mqtt_evt evt = {
		.type = MQTT_EVT_CONNACK,
		.param.connack.prop = {
				.server_keep_alive = server_keepalive,
				.rx.has_server_keep_alive = true,
			},
	};

	ARG_UNUSED(calls);
	client->evt_cb(client, &evt);
	return 0;
}

static void receive_server_keepalive(uint16_t interval)
{
	struct socket_ncs_pollcb_params params = {
		.fd = 0,
		.events = ZSOCK_POLLIN,
		.revents = ZSOCK_POLLIN,
	};

	server_keepalive = interval;
	__cmock_mqtt_input_Stub(keepalive_connack_stub);
	TEST_ASSERT_NOT_NULL(mqtt_pollcb);
	mqtt_pollcb(&params);
	k_sleep(K_MSEC(20));
	uart_stub_tx_done_drain();
	TEST_ASSERT_EQUAL_UINT16(interval, connected_client->keepalive);
}

static void start_keepalive_test(const char *cfg_cmd)
{
	k_sem_reset(&keepalive_calls);
	__cmock_mqtt_keepalive_time_left_Stub(keepalive_time_left_stub);
	__cmock_mqtt_live_Stub(keepalive_live_stub);
	mqtt_connect_version(cfg_cmd, MQTT_VERSION_5_0);
}
#endif

void test_mqtt_server_keep_alive_reschedules_work(void)
{
#if defined(CONFIG_MQTT_VERSION_5_0)
	start_keepalive_test("AT#XMQTTCFG=\"test_client\",60,1,5\r\n");
	receive_server_keepalive(1);

	/* Both executions must precede the original 60-second deadline. */
	TEST_ASSERT_NOT_EQUAL(0, k_sem_take(&keepalive_calls, K_MSEC(500)));
	TEST_ASSERT_EQUAL_INT(0, k_sem_take(&keepalive_calls, K_SECONDS(2)));
	TEST_ASSERT_EQUAL_INT(0, k_sem_take(&keepalive_calls, K_SECONDS(2)));

	mqtt_disconnect_ok();
	k_sem_reset(&keepalive_calls);
	TEST_ASSERT_NOT_EQUAL(0, k_sem_take(&keepalive_calls, K_SECONDS(2)));
#else
	TEST_IGNORE_MESSAGE("Requires CONFIG_MQTT_VERSION_5_0");
#endif
}

void test_mqtt_server_keep_alive_zero_stops_work(void)
{
#if defined(CONFIG_MQTT_VERSION_5_0)
	start_keepalive_test("AT#XMQTTCFG=\"test_client\",1,1,5\r\n");
	receive_server_keepalive(1);
	TEST_ASSERT_EQUAL_INT(0, k_sem_take(&keepalive_calls, K_SECONDS(2)));

	receive_server_keepalive(0);
	k_sem_reset(&keepalive_calls);
	TEST_ASSERT_NOT_EQUAL(0, k_sem_take(&keepalive_calls, K_SECONDS(2)));
	mqtt_disconnect_ok();
#else
	TEST_IGNORE_MESSAGE("Requires CONFIG_MQTT_VERSION_5_0");
#endif
}

void test_mqtt_server_keep_alive_enables_work(void)
{
#if defined(CONFIG_MQTT_VERSION_5_0)
	start_keepalive_test("AT#XMQTTCFG=\"test_client\",0,1,5\r\n");
	TEST_ASSERT_NOT_EQUAL(0, k_sem_take(&keepalive_calls, K_MSEC(100)));

	receive_server_keepalive(1);
	TEST_ASSERT_EQUAL_INT(0, k_sem_take(&keepalive_calls, K_SECONDS(2)));
	mqtt_disconnect_ok();
#else
	TEST_IGNORE_MESSAGE("Requires CONFIG_MQTT_VERSION_5_0");
#endif
}

/*
 * Test: MQTT 5.0 acknowledgment reason codes are reported in #XMQTTEVT.
 */
void test_mqtt_evt_reason_codes_5_0(void)
{
#if defined(CONFIG_MQTT_VERSION_5_0)
	const char *resp;
	uint8_t suback_rc = 0x87;
	uint8_t unsuback_rc = 0x11;
	struct mqtt_evt evt;
	char expected[32];

	mqtt_connect_version("AT#XMQTTCFG=\"test_client\",60,1,5\r\n", MQTT_VERSION_5_0);

	/* PUBACK: No matching subscribers. */
	evt = (struct mqtt_evt){
		.type = MQTT_EVT_PUBACK,
		.param.puback = {.message_id = 1, .reason_code = 0x10},
	};
	resp = inject_mqtt_evt(&evt);
	snprintf(expected, sizeof(expected), "#XMQTTEVT: %d,0,16\r\n", MQTT_EVT_PUBACK);
	TEST_ASSERT_NOT_NULL(strstr(resp, expected));

	/* SUBACK: Not authorized. */
	evt = (struct mqtt_evt){
		.type = MQTT_EVT_SUBACK,
		.param.suback = {.message_id = 2, .return_codes = {.data = &suback_rc, .len = 1}},
	};
	resp = inject_mqtt_evt(&evt);
	snprintf(expected, sizeof(expected), "#XMQTTEVT: %d,0,135\r\n", MQTT_EVT_SUBACK);
	TEST_ASSERT_NOT_NULL(strstr(resp, expected));

	/* UNSUBACK: No subscription existed. */
	evt = (struct mqtt_evt){
		.type = MQTT_EVT_UNSUBACK,
		.param.unsuback = {.message_id = 3,
				   .reason_codes = {.data = &unsuback_rc, .len = 1}},
	};
	resp = inject_mqtt_evt(&evt);
	snprintf(expected, sizeof(expected), "#XMQTTEVT: %d,0,17\r\n", MQTT_EVT_UNSUBACK);
	TEST_ASSERT_NOT_NULL(strstr(resp, expected));

	/* A failed decode reports the error without a reason code. */
	evt = (struct mqtt_evt){
		.type = MQTT_EVT_PUBACK,
		.result = -EINVAL,
	};
	resp = inject_mqtt_evt(&evt);
	snprintf(expected, sizeof(expected), "#XMQTTEVT: %d,%d\r\n", MQTT_EVT_PUBACK, -EINVAL);
	TEST_ASSERT_NOT_NULL(strstr(resp, expected));

	mqtt_disconnect_ok();
#else
	TEST_IGNORE_MESSAGE("Requires CONFIG_MQTT_VERSION_5_0");
#endif
}

/*
 * Test: A PUBREC with a failure reason code is not followed by PUBREL.
 */
void test_mqtt_pubrec_failure_5_0(void)
{
#if defined(CONFIG_MQTT_VERSION_5_0)
	const char *resp;
	struct mqtt_evt evt;
	char expected[32];

	mqtt_connect_version("AT#XMQTTCFG=\"test_client\",60,1,5\r\n", MQTT_VERSION_5_0);

	/* Success: PUBREL is sent. */
	__cmock_mqtt_publish_qos2_release_ExpectAnyArgsAndReturn(0);
	evt = (struct mqtt_evt){
		.type = MQTT_EVT_PUBREC,
		.param.pubrec = {.message_id = 5, .reason_code = 0},
	};
	resp = inject_mqtt_evt(&evt);
	snprintf(expected, sizeof(expected), "#XMQTTEVT: %d,0,0\r\n", MQTT_EVT_PUBREC);
	TEST_ASSERT_NOT_NULL(strstr(resp, expected));

	/* Quota exceeded: no PUBREL. CMock fails the test on an unexpected call. */
	evt = (struct mqtt_evt){
		.type = MQTT_EVT_PUBREC,
		.param.pubrec = {.message_id = 6, .reason_code = 0x97},
	};
	resp = inject_mqtt_evt(&evt);
	snprintf(expected, sizeof(expected), "#XMQTTEVT: %d,0,151\r\n", MQTT_EVT_PUBREC);
	TEST_ASSERT_NOT_NULL(strstr(resp, expected));

	mqtt_disconnect_ok();
#else
	TEST_IGNORE_MESSAGE("Requires CONFIG_MQTT_VERSION_5_0");
#endif
}

void test_mqtt_ack_decode_errors(void)
{
	static const enum mqtt_evt_type types[] = {
		MQTT_EVT_PUBACK,  MQTT_EVT_PUBREC, MQTT_EVT_PUBREL,
		MQTT_EVT_PUBCOMP, MQTT_EVT_SUBACK, MQTT_EVT_UNSUBACK,
	};
	char expected[48];

	mqtt_connect_version("AT#XMQTTCFG=\"test_client\",60,1,4\r\n", MQTT_VERSION_3_1_1);
	for (size_t i = 0; i < ARRAY_SIZE(types); i++) {
		struct mqtt_evt evt = {.type = types[i], .result = -EBADMSG};

		snprintf(expected, sizeof(expected), "#XMQTTEVT: %d,%d\r\n", types[i], -EBADMSG);
		TEST_ASSERT_NOT_NULL(strstr(inject_mqtt_evt(&evt), expected));
	}
	mqtt_disconnect_ok();

#if defined(CONFIG_MQTT_VERSION_5_0)
	mqtt_connect_version("AT#XMQTTCFG=\"test_client\",60,1,5\r\n", MQTT_VERSION_5_0);
	for (size_t i = 0; i < ARRAY_SIZE(types); i++) {
		struct mqtt_evt evt = {.type = types[i], .result = -EBADMSG};

		snprintf(expected, sizeof(expected), "#XMQTTEVT: %d,%d\r\n", types[i], -EBADMSG);
		TEST_ASSERT_NOT_NULL(strstr(inject_mqtt_evt(&evt), expected));
	}
	mqtt_disconnect_ok();
#endif
}

void test_mqtt_ack_format_3_1_1(void)
{
	struct mqtt_evt events[] = {
		{.type = MQTT_EVT_PUBACK, .param.puback.message_id = 1},
		{.type = MQTT_EVT_PUBREC, .param.pubrec.message_id = 2},
		{.type = MQTT_EVT_PUBREL, .param.pubrel.message_id = 3},
		{.type = MQTT_EVT_PUBCOMP, .param.pubcomp.message_id = 4},
		{.type = MQTT_EVT_SUBACK},
		{.type = MQTT_EVT_UNSUBACK},
	};
	char expected[48];

#if defined(CONFIG_MQTT_VERSION_5_0)
	uint8_t reason = 0x80;

	events[0].param.puback.reason_code = 0x80;
	events[1].param.pubrec.reason_code = 0x10;
	events[2].param.pubrel.reason_code = 0x92;
	events[3].param.pubcomp.reason_code = 0x92;
	events[4].param.suback.return_codes.data = &reason;
	events[4].param.suback.return_codes.len = 1;
	events[5].param.unsuback.reason_codes.data = &reason;
	events[5].param.unsuback.reason_codes.len = 1;
#endif

	mqtt_connect_version("AT#XMQTTCFG=\"test_client\",60,1,4\r\n", MQTT_VERSION_3_1_1);
	__cmock_mqtt_publish_qos2_release_ExpectAnyArgsAndReturn(0);
	__cmock_mqtt_publish_qos2_complete_ExpectAnyArgsAndReturn(0);
	for (size_t i = 0; i < ARRAY_SIZE(events); i++) {
		snprintf(expected, sizeof(expected), "#XMQTTEVT: %d,0\r\n", events[i].type);
		TEST_ASSERT_NOT_NULL(strstr(inject_mqtt_evt(&events[i]), expected));
	}
	mqtt_disconnect_ok();
}

void test_mqtt_ack_reason_code_matrix_5_0(void)
{
#if defined(CONFIG_MQTT_VERSION_5_0)
	uint8_t reason = 0;
	struct mqtt_evt events[] = {
		{.type = MQTT_EVT_PUBACK},
		{.type = MQTT_EVT_PUBREC},
		{.type = MQTT_EVT_PUBREL},
		{.type = MQTT_EVT_PUBCOMP},
		{.type = MQTT_EVT_SUBACK, .param.suback.return_codes = {.data = &reason, .len = 1}},
		{.type = MQTT_EVT_UNSUBACK,
		 .param.unsuback.reason_codes = {.data = &reason, .len = 1}},
	};
	char expected[48];

	mqtt_connect_version("AT#XMQTTCFG=\"test_client\",60,1,5\r\n", MQTT_VERSION_5_0);
	for (size_t pass = 0; pass < 2; pass++) {
		reason = pass == 0 ? 0 : 0x80;
		events[0].param.puback.reason_code = reason;
		events[1].param.pubrec.reason_code = reason;
		events[2].param.pubrel.reason_code = pass == 0 ? 0 : 0x92;
		events[3].param.pubcomp.reason_code = pass == 0 ? 0 : 0x92;
		if (pass == 0) {
			__cmock_mqtt_publish_qos2_release_ExpectAnyArgsAndReturn(0);
		}
		__cmock_mqtt_publish_qos2_complete_ExpectAnyArgsAndReturn(0);
		for (size_t i = 0; i < ARRAY_SIZE(events); i++) {
			int code = pass != 0 && (i == 2 || i == 3) ? 0x92 : reason;

			snprintf(expected, sizeof(expected), "#XMQTTEVT: %d,0,%d\r\n",
				 events[i].type, code);
			TEST_ASSERT_NOT_NULL(strstr(inject_mqtt_evt(&events[i]), expected));
		}
	}

	events[4].param.suback.return_codes.len = 0;
	events[4].param.suback.return_codes.data = NULL;
	TEST_ASSERT_NOT_NULL(strstr(inject_mqtt_evt(&events[4]), "#XMQTTEVT: 7,0\r\n"));
	events[5].param.unsuback.reason_codes.len = 0;
	events[5].param.unsuback.reason_codes.data = NULL;
	TEST_ASSERT_NOT_NULL(strstr(inject_mqtt_evt(&events[5]), "#XMQTTEVT: 8,0\r\n"));
	mqtt_disconnect_ok();
#else
	TEST_IGNORE_MESSAGE("Requires CONFIG_MQTT_VERSION_5_0");
#endif
}

void test_mqtt_qos2_send_errors_5_0(void)
{
#if defined(CONFIG_MQTT_VERSION_5_0)
	struct mqtt_evt evt = {
		.type = MQTT_EVT_PUBREC,
		.param.pubrec = {.message_id = 7, .reason_code = 0x10},
	};
	char expected[48];

	mqtt_connect_version("AT#XMQTTCFG=\"test_client\",60,1,5\r\n", MQTT_VERSION_5_0);
	__cmock_mqtt_publish_qos2_release_ExpectAnyArgsAndReturn(-EIO);
	snprintf(expected, sizeof(expected), "#XMQTTEVT: 4,%d,16\r\n", -EIO);
	TEST_ASSERT_NOT_NULL(strstr(inject_mqtt_evt(&evt), expected));

	evt = (struct mqtt_evt){
		.type = MQTT_EVT_PUBREL,
		.param.pubrel = {.message_id = 8, .reason_code = 0x92},
	};
	__cmock_mqtt_publish_qos2_complete_ExpectAnyArgsAndReturn(-EIO);
	snprintf(expected, sizeof(expected), "#XMQTTEVT: 5,%d,146\r\n", -EIO);
	TEST_ASSERT_NOT_NULL(strstr(inject_mqtt_evt(&evt), expected));

	/* Exercise the numeric failure threshold, independent of packet validity checks. */
	evt = (struct mqtt_evt){
		.type = MQTT_EVT_PUBREC,
		.param.pubrec = {.message_id = 9, .reason_code = 127},
	};
	__cmock_mqtt_publish_qos2_release_ExpectAnyArgsAndReturn(0);
	TEST_ASSERT_NOT_NULL(strstr(inject_mqtt_evt(&evt), "#XMQTTEVT: 4,0,127\r\n"));
	evt.param.pubrec.reason_code = 128;
	TEST_ASSERT_NOT_NULL(strstr(inject_mqtt_evt(&evt), "#XMQTTEVT: 4,0,128\r\n"));
	mqtt_disconnect_ok();
#else
	TEST_IGNORE_MESSAGE("Requires CONFIG_MQTT_VERSION_5_0");
#endif
}

void test_mqtt_broker_disconnect_reason_codes_5_0(void)
{
#if defined(CONFIG_MQTT_VERSION_5_0)
	static const int results[] = {0, 0x8e, -ECONNRESET};
	char expected[48];

	for (size_t i = 0; i < ARRAY_SIZE(results); i++) {
		struct mqtt_evt evt = {.type = MQTT_EVT_DISCONNECT, .result = results[i]};

		mqtt_connect_version("AT#XMQTTCFG=\"test_client\",60,1,5\r\n", MQTT_VERSION_5_0);
		snprintf(expected, sizeof(expected), "#XMQTTEVT: 1,%d\r\n", results[i]);
		TEST_ASSERT_NOT_NULL(strstr(inject_mqtt_evt(&evt), expected));
		assert_mqtt_disconnected();
	}
	assert_mqtt_reconnects();
#else
	TEST_IGNORE_MESSAGE("Requires CONFIG_MQTT_VERSION_5_0");
#endif
}

#if defined(CONFIG_MQTT_VERSION_5_0)
static uint32_t connect_session_expiry;

static int mqtt_connect_session_stub(struct mqtt_client *client, int num_calls)
{
	ARG_UNUSED(num_calls);

	connect_session_expiry = client->prop.session_expiry_interval;
	return 0;
}
#endif

/*
 * Test: The session expiry interval is passed to the MQTT library.
 */
void test_mqtt_connect_session_expiry_5_0(void)
{
#if defined(CONFIG_MQTT_VERSION_5_0)
	const char *resp;

	connect_session_expiry = 0;

	send_at_command("AT#XMQTTCFG=\"test_client\",60,0,5,3600\r\n");
	resp = get_captured_response();
	TEST_ASSERT_NOT_NULL(strstr(resp, "OK"));

	clear_captured_response();
	__cmock_zsock_getaddrinfo_Stub(getaddrinfo_stub);
	__cmock_zsock_freeaddrinfo_Ignore();
	__cmock_mqtt_connect_Stub(mqtt_connect_session_stub);
	send_at_command("AT#XMQTTCON=1,\"user\",\"pass\",\"broker.example.com\",1883\r\n");
	resp = get_captured_response();
	TEST_ASSERT_NOT_NULL(strstr(resp, "OK"));
	TEST_ASSERT_EQUAL_UINT32(3600, connect_session_expiry);
	__cmock_mqtt_connect_Stub(NULL);

	mqtt_disconnect_ok();
#else
	TEST_IGNORE_MESSAGE("Requires CONFIG_MQTT_VERSION_5_0");
#endif
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

static const uint8_t binary_payload[] = {'A', 0, '\r', '\n', 0xff, '"', 'Z'};
static size_t published_bytes;
static size_t expected_publish_size;

static int binary_publish_stub(struct mqtt_client *client, const struct mqtt_publish_param *param,
			       int calls)
{
	ARG_UNUSED(calls);

	TEST_ASSERT_EQUAL_PTR(connected_client, client);
	assert_mqtt_utf8("my/topic", &param->message.topic.topic);
	TEST_ASSERT_EQUAL_UINT8(MQTT_QOS_0_AT_MOST_ONCE, param->message.topic.qos);
	TEST_ASSERT_EQUAL_UINT8(0, param->retain_flag);
	TEST_ASSERT_EQUAL_UINT8(0, param->dup_flag);
	TEST_ASSERT_NOT_EQUAL(0, param->message_id);
	TEST_ASSERT_EQUAL_UINT(expected_publish_size, param->message.payload.len);
	for (size_t i = 0; i < expected_publish_size; i++) {
		TEST_ASSERT_EQUAL_HEX8(binary_payload[i % sizeof(binary_payload)],
				       ((const uint8_t *)param->message.payload.data)[i]);
	}
	published_bytes += param->message.payload.len;
	return 0;
}

static void publish_binary_datamode(size_t len)
{
	char command[96];
	uint8_t chunk[64];

	mqtt_connect_version("AT#XMQTTCFG=\"test_client\",60,1,4\r\n", MQTT_VERSION_3_1_1);
	published_bytes = 0;
	expected_publish_size = len;
	__cmock_mqtt_publish_Stub(binary_publish_stub);
	snprintf(command, sizeof(command), "AT#XMQTTPUB=\"my/topic\",\"\",0,0,%zu\r\n", len);
	send_at_command(command);
	TEST_ASSERT_NOT_NULL(strstr(get_captured_response(), "OK"));
	clear_captured_response();

	for (size_t offset = 0; offset < len;) {
		size_t count = MIN(sizeof(chunk), len - offset);

		for (size_t i = 0; i < count; i++) {
			chunk[i] = binary_payload[(offset + i) % sizeof(binary_payload)];
		}
		TEST_ASSERT_EQUAL_UINT(0, published_bytes);
		uart_stub_rx_nowait(chunk, count);
		k_sleep(K_MSEC(1));
		offset += count;
	}
	k_sleep(K_MSEC(20));
	uart_stub_tx_done_reset();
	TEST_ASSERT_EQUAL_UINT(len, published_bytes);
	TEST_ASSERT_NOT_NULL(strstr(get_captured_response(), "#XDATAMODE: 0\r\n"));
	__cmock_mqtt_publish_Stub(NULL);
	mqtt_disconnect_ok();
}

void test_mqtt_datamode_fragmented_binary(void)
{
	publish_binary_datamode(sizeof(binary_payload) * 20);
}

void test_mqtt_datamode_buffer_limit(void)
{
	publish_binary_datamode(CONFIG_SM_DATAMODE_BUF_SIZE);
}

void test_mqtt_datamode_invalid_lengths(void)
{
	char oversized[96];
	static const char *const commands[] = {
		"AT#XMQTTPUB=\"my/topic\",\"\",0,0,-1\r\n",
		"AT#XMQTTPUB=\"my/topic\",\"hello\",0,0,5\r\n",
	};

	snprintf(oversized, sizeof(oversized), "AT#XMQTTPUB=\"my/topic\",\"\",0,0,%u\r\n",
		 CONFIG_SM_DATAMODE_BUF_SIZE + 1U);
	mqtt_connect_version("AT#XMQTTCFG=\"test_client\",60,1,4\r\n", MQTT_VERSION_3_1_1);
	for (size_t i = 0; i < ARRAY_SIZE(commands); i++) {
		clear_captured_response();
		send_at_command(commands[i]);
		TEST_ASSERT_NOT_NULL(strstr(get_captured_response(), "ERROR"));
	}
	clear_captured_response();
	send_at_command(oversized);
	TEST_ASSERT_NOT_NULL(strstr(get_captured_response(), "ERROR"));
	/* Invalid commands must not leave the host in data mode. */
	mqtt_disconnect_ok();
}

static void receive_framed_payload(const uint8_t *payload, size_t len)
{
	struct socket_ncs_pollcb_params params = {
		.fd = 0,
		.events = ZSOCK_POLLIN,
		.revents = ZSOCK_POLLIN,
	};
	char header[80];
	static const char trailer[] = "\r\n\r\n#XMQTTEVT: 2,0\r\n";
	const uint8_t *captured;
	size_t header_len;

	mqtt_connect_version("AT#XMQTTCFG=\"test_client\",60,1,4\r\n", MQTT_VERSION_3_1_1);
	pub_topic_in = "device/data";
	pub_payload_in = (const char *)payload;
	pub_payload_len = len;
	__cmock_mqtt_input_Stub(mqtt_input_stub);
	__cmock_mqtt_read_publish_payload_Stub(mqtt_read_payload_stub);
	clear_captured_response();
	mqtt_pollcb(&params);
	k_sleep(K_MSEC(20));
	if (pub_payload_off < len) {
		mqtt_pollcb(&params);
		k_sleep(K_MSEC(20));
	}
	uart_stub_tx_done_reset();
	header_len =
		snprintf(header, sizeof(header), "\r\n#XMQTTMSG: 11,%zu\r\ndevice/data\r\n", len);
	captured = (const uint8_t *)get_captured_response();
	TEST_ASSERT_EQUAL_UINT(header_len + len + sizeof(trailer) - 1, get_captured_response_len());
	TEST_ASSERT_EQUAL_MEMORY(header, captured, header_len);
	if (len > 0) {
		TEST_ASSERT_EQUAL_MEMORY(payload, captured + header_len, len);
	}
	TEST_ASSERT_EQUAL_MEMORY(trailer, captured + header_len + len, sizeof(trailer) - 1);
	mqtt_disconnect_ok();
}

void test_mqtt_incoming_binary_framing(void)
{
	receive_framed_payload(binary_payload, sizeof(binary_payload));
}

void test_mqtt_incoming_empty_payload(void)
{
	receive_framed_payload(NULL, 0);
}

extern int unity_main(void);

int main(void)
{
	(void)unity_main();

	return 0;
}
