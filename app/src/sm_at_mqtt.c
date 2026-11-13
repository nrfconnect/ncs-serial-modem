/*
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/logging/log.h>
#include <zephyr/kernel.h>
#include <stdio.h>
#include <stdlib.h>
#include <zephyr/net/mqtt.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/tls_credentials.h>
#include <zephyr/random/random.h>
#include <zephyr/net/socket_ncs.h>
#include "sm_util.h"
#include "sm_at_host.h"

LOG_MODULE_REGISTER(sm_mqtt, CONFIG_SM_LOG_LEVEL);

#define MQTT_MAX_TOPIC_LEN	128
#define MQTT_MAX_CID_LEN	64

#define SM_DEFAULT_CID		"sm_default_client_id"

/* Initial value of clean session flag is done this way also in MQTT library.*/
#define MQTT_CLEAN_SESSION_INIT (IS_ENABLED(CONFIG_MQTT_CLEAN_SESSION) ? 1U : 0U)

#define SM_MQTT_VERSION_DEFAULT MQTT_VERSION_3_1_1

/* MQTT 5.0 section 2.4: reason codes from 0x80 indicate failure. */
#define SM_MQTT_REASON_CODE_FAILURE 0x80
#define SM_MQTT_REMAINING_LENGTH_MAX 268435455U

/* Number of outgoing topic aliases; 0 compiles the alias code out. */
#if defined(CONFIG_MQTT_VERSION_5_0) && defined(CONFIG_SM_MQTTC_TOPIC_ALIAS_MAX)
#define SM_MQTT_TOPIC_ALIASES CONFIG_SM_MQTTC_TOPIC_ALIAS_MAX
#else
#define SM_MQTT_TOPIC_ALIASES 0
#endif

/**@brief MQTT client operations. */
enum sm_mqttcon_operation {
	MQTTC_DISCONNECT,
	MQTTC_CONNECT,
	MQTTC_CONNECT6,
};

/**@brief MQTT subscribe operations. */
enum sm_mqttsub_operation {
	AT_MQTTSUB_UNSUB,
	AT_MQTTSUB_SUB
};
/* All ctx access is lock-free because every user runs on sm_work_q and is thus serialized:
 * AT command handlers, mqtt_poll_work and mqtt_keepalive_work.
 */
static struct sm_mqtt_ctx {
	int family; /* Socket address family */

	bool connected;
	bool disconnect_requested;
	struct mqtt_utf8 client_id;
	struct mqtt_utf8 username;
	struct mqtt_utf8 password;
	sec_tag_t sec_tag;
	union {
		struct sockaddr_in  broker;
		struct sockaddr_in6 broker6;
	};
	struct modem_pipe *pipe;
	/* Incoming PUBLISH state: valid from mqtt_input() return until drain completes */
	size_t publish_payload_remaining;
	size_t rx_topic_len;    /* > 0 while header unsent; cleared when pipe is locked */
	int protocol_error;
#if defined(CONFIG_MQTT_VERSION_5_0)
	uint32_t rx_msg_expiry;
	bool rx_has_msg_expiry;
	uint32_t maximum_packet_size;
	uint16_t receive_maximum;
	uint8_t maximum_qos;
	bool retain_available;
	bool wildcard_sub_available;
	bool shared_sub_available;
#endif
#if SM_MQTT_TOPIC_ALIASES > 0
	uint16_t topic_alias_max; /* Usable outgoing aliases: local and broker limit */
#endif
} ctx;

static uint16_t mqtt_broker_port;
static uint16_t mqtt_configured_keepalive;
static char mqtt_clientid[MQTT_MAX_CID_LEN + 1];

#if defined(CONFIG_MQTT_VERSION_5_0)
/* Packet identifier in use by an unacknowledged QoS 1/2 PUBLISH, SUBSCRIBE or UNSUBSCRIBE. */
struct mqtt_inflight {
	struct mqtt_inflight *next;
	uint16_t message_id;
	uint8_t ack_evt; /* enum mqtt_evt_type that completes the exchange */
};
#endif

struct mqtt_conn {
	uint8_t rx[CONFIG_SM_MQTTC_MESSAGE_BUFFER_LEN];
	uint8_t tx[CONFIG_SM_MQTTC_MESSAGE_BUFFER_LEN];
	uint8_t payload_drain[MQTT_MAX_TOPIC_LEN];
	uint8_t pub_topic[MQTT_MAX_TOPIC_LEN];
	char broker_url[SM_MAX_DNS_LEN + 1];
	char username[SM_MAX_USERNAME + 1];
	char password[SM_MAX_PASSWORD + 1];
	struct mqtt_publish_param pub_param;
	uint16_t message_id; /* Last assigned packet identifier */
#if defined(CONFIG_MQTT_VERSION_5_0)
	struct mqtt_inflight *inflight;
	uint16_t inflight_count; /* QoS 1/2 PUBLISH entries, for Receive Maximum */
#endif
#if SM_MQTT_TOPIC_ALIASES > 0
	/* Outgoing aliases in assignment order; alias N is aliases[N - 1]. */
	struct {
		uint8_t topic[MQTT_MAX_TOPIC_LEN];
		uint8_t len; /* 0 while unassigned */
	} aliases[SM_MQTT_TOPIC_ALIASES];
	uint16_t alias_new; /* Alias that pub_param assigns, recorded once sent */
#endif
};
static struct mqtt_conn *mqtt_conn;

/* The mqtt client struct */
static struct mqtt_client client;

#if defined(CONFIG_MQTT_VERSION_5_0)
static struct mqtt_inflight *mqtt_inflight_find(uint16_t message_id)
{
	if (mqtt_conn == NULL) {
		return NULL;
	}
	for (struct mqtt_inflight *item = mqtt_conn->inflight; item != NULL; item = item->next) {
		if (item->message_id == message_id) {
			return item;
		}
	}
	return NULL;
}

static bool mqtt_inflight_is_publish(uint8_t ack_evt)
{
	return ack_evt != MQTT_EVT_SUBACK && ack_evt != MQTT_EVT_UNSUBACK;
}

static int mqtt_inflight_add(uint16_t message_id, uint8_t ack_evt)
{
	struct mqtt_inflight *item = malloc(sizeof(*item));

	if (item == NULL) {
		return -ENOMEM;
	}
	*item = (struct mqtt_inflight){
		.next = mqtt_conn->inflight,
		.message_id = message_id,
		.ack_evt = ack_evt,
	};
	mqtt_conn->inflight = item;
	if (mqtt_inflight_is_publish(ack_evt)) {
		mqtt_conn->inflight_count++;
	}
	return 0;
}

static void mqtt_inflight_remove(uint16_t message_id)
{
	if (mqtt_conn == NULL) {
		return;
	}
	struct mqtt_inflight **link = &mqtt_conn->inflight;

	while (*link != NULL) {
		struct mqtt_inflight *item = *link;

		if (item->message_id == message_id) {
			*link = item->next;
			if (mqtt_inflight_is_publish(item->ack_evt)) {
				mqtt_conn->inflight_count--;
			}
			free(item);
			return;
		}
		link = &item->next;
	}
}

/* Release the packet identifier if ack_evt is the one its exchange is waiting for. */
static void mqtt_inflight_ack(uint16_t message_id, uint8_t ack_evt)
{
	if (client.protocol_version != MQTT_VERSION_5_0) {
		return;
	}
	struct mqtt_inflight *item = mqtt_inflight_find(message_id);

	if (item != NULL && item->ack_evt == ack_evt) {
		mqtt_inflight_remove(message_id);
	}
}
#endif

/* SUBSCRIBE, UNSUBSCRIBE and QoS 1/2 PUBLISH share one packet identifier space
 * (MQTT 5.0 section 2.2.1). Returns an identifier not in use by an MQTT 5.0 exchange.
 */
STATIC int mqtt_message_id_next(void)
{
	for (uint16_t attempts = 0; attempts < UINT16_MAX; attempts++) {
		mqtt_conn->message_id++;
		if (mqtt_conn->message_id == 0 || (client.protocol_version != MQTT_VERSION_5_0 &&
						   mqtt_conn->message_id == UINT16_MAX)) {
			mqtt_conn->message_id = 1;
		}
#if defined(CONFIG_MQTT_VERSION_5_0)
		if (client.protocol_version == MQTT_VERSION_5_0 &&
		    mqtt_inflight_find(mqtt_conn->message_id) != NULL) {
			continue;
		}
#endif
		return mqtt_conn->message_id;
	}
	/* Unreachable in practice: memory runs out long before 65535 exchanges are in flight. */
	return -EAGAIN;
}

#if defined(CONFIG_MQTT_VERSION_5_0)

static void mqtt_capabilities_reset(void)
{
	ctx.maximum_qos = MQTT_QOS_2_EXACTLY_ONCE;
	ctx.retain_available = true;
	ctx.wildcard_sub_available = true;
	ctx.shared_sub_available = true;
#if SM_MQTT_TOPIC_ALIASES > 0
	ctx.topic_alias_max = 0;
#endif
}
#endif

#if SM_MQTT_TOPIC_ALIASES > 0
BUILD_ASSERT(MQTT_MAX_TOPIC_LEN <= UINT8_MAX);

/* Aliases are valid for one network connection, starting from its CONNACK. */
static void mqtt_alias_connack(const struct mqtt_connack_param *connack)
{
	if (mqtt_conn != NULL) {
		memset(mqtt_conn->aliases, 0, sizeof(mqtt_conn->aliases));
		mqtt_conn->alias_new = 0;
	}
	if (connack->prop.rx.has_topic_alias_maximum) {
		ctx.topic_alias_max = MIN(connack->prop.topic_alias_maximum, SM_MQTT_TOPIC_ALIASES);
	}
}

/* Use the alias of a known topic, or assign the next free alias. Without a free alias,
 * the topic is sent in full. Called after pub_param holds the full topic.
 */
static void mqtt_alias_select(void)
{
	struct mqtt_publish_param *param = &mqtt_conn->pub_param;
	size_t len = param->message.topic.topic.size;

	param->prop.topic_alias = 0;
	mqtt_conn->alias_new = 0;
	if (client.protocol_version != MQTT_VERSION_5_0 || len == 0) {
		return;
	}
	for (uint16_t i = 0; i < ctx.topic_alias_max; i++) {
		if (mqtt_conn->aliases[i].len == 0) {
			/* Aliases are assigned in order, so later ones are free too. */
			mqtt_conn->alias_new = i + 1;
			param->prop.topic_alias = i + 1;
			return;
		}
		if (mqtt_conn->aliases[i].len == len &&
		    memcmp(mqtt_conn->aliases[i].topic, mqtt_conn->pub_topic, len) == 0) {
			param->prop.topic_alias = i + 1;
			param->message.topic.topic.size = 0;
			return;
		}
	}
}

/* Record an assigned alias once the PUBLISH that carries its topic was sent. */
static void mqtt_alias_commit(void)
{
	uint16_t alias = mqtt_conn->alias_new;

	if (alias == 0) {
		return;
	}
	mqtt_conn->aliases[alias - 1].len = mqtt_conn->pub_param.message.topic.topic.size;
	memcpy(mqtt_conn->aliases[alias - 1].topic, mqtt_conn->pub_topic,
	       mqtt_conn->aliases[alias - 1].len);
	mqtt_conn->alias_new = 0;
	LOG_DBG("MQTT topic alias %u assigned", alias);
}
#endif

/* Remaining Length excludes the control byte and its own variable-width encoding. */
STATIC int mqtt_check_packet_size(size_t remaining)
{
#if defined(CONFIG_MQTT_VERSION_5_0)
	if (client.protocol_version != MQTT_VERSION_5_0) {
		return 0;
	}
	if (remaining <= SM_MQTT_REMAINING_LENGTH_MAX) {
		size_t packet_size = remaining + 1;
		size_t value = remaining;

		do {
			packet_size++;
			value >>= 7;
		} while (value != 0);

		if (packet_size <= ctx.maximum_packet_size) {
			return 0;
		}
	}
	LOG_ERR("MQTT packet too large: remaining=%zu, maximum=%u", remaining,
		ctx.maximum_packet_size);
	return -EMSGSIZE;
#else
	ARG_UNUSED(remaining);
#endif
	return 0;
}

/**@brief Function to handle received publish event.
 */
static void handle_mqtt_publish_evt(struct mqtt_client *const c, const struct mqtt_evt *evt)
{
	/* Send QoS acknowledgments */
	if (evt->param.publish.message.topic.qos == MQTT_QOS_1_AT_LEAST_ONCE) {
		const struct mqtt_puback_param ack = {
			.message_id = evt->param.publish.message_id
		};

		ctx.protocol_error = mqtt_check_packet_size(2);
		if (ctx.protocol_error != 0) {
			return;
		}
		mqtt_publish_qos1_ack(&client, &ack);
	} else if (evt->param.publish.message.topic.qos == MQTT_QOS_2_EXACTLY_ONCE) {
		const struct mqtt_pubrec_param ack = {
			.message_id = evt->param.publish.message_id
		};

		ctx.protocol_error = mqtt_check_packet_size(2);
		if (ctx.protocol_error != 0) {
			return;
		}
		mqtt_publish_qos2_receive(&client, &ack);
	}

	/* MQTT client does not track the packet identifiers, so MQTT_QOS_2_EXACTLY_ONCE
	 * promise is not kept. This deviates from MQTT v3.1.1.
	 */

	/* Copy topic into payload_drain now; rx_buf may be reused after mqtt_input() returns.
	 * payload_drain is not used for draining until after the topic has been forwarded.
	 */
	ctx.rx_topic_len = MIN(evt->param.publish.message.topic.topic.size, MQTT_MAX_TOPIC_LEN);
	memcpy(mqtt_conn->payload_drain, evt->param.publish.message.topic.topic.utf8,
	       ctx.rx_topic_len);
	ctx.publish_payload_remaining = evt->param.publish.message.payload.len;
#if defined(CONFIG_MQTT_VERSION_5_0)
	ctx.rx_has_msg_expiry = client.protocol_version == MQTT_VERSION_5_0 &&
				evt->param.publish.prop.rx.has_message_expiry_interval;
	ctx.rx_msg_expiry = evt->param.publish.prop.message_expiry_interval;
#endif
}

/* Drain as much publish payload as is available without blocking.
 * Returns 0 when complete, -EAGAIN when more data is needed.
 */
static int drain_publish_payload(struct mqtt_client *const c)
{
	while (ctx.publish_payload_remaining > 0) {
		int ret = mqtt_read_publish_payload(c, mqtt_conn->payload_drain,
					     sizeof(mqtt_conn->payload_drain));

		if (ret == -EAGAIN) {
			return -EAGAIN;
		}
		if (ret <= 0) {
			break;
		}
		data_send(ctx.pipe, mqtt_conn->payload_drain, ret);
		ctx.publish_payload_remaining -= ret;
	}

	ctx.publish_payload_remaining = 0;
	return 0;
}

static void mqtt_msg_header_send(void)
{
#if defined(CONFIG_MQTT_VERSION_5_0)
	if (ctx.rx_has_msg_expiry) {
		rsp_send_to(ctx.pipe, "\r\n#XMQTTMSG: %zu,%zu,%u\r\n", ctx.rx_topic_len,
			    ctx.publish_payload_remaining, ctx.rx_msg_expiry);
		return;
	}
#endif
	rsp_send_to(ctx.pipe, "\r\n#XMQTTMSG: %zu,%zu\r\n", ctx.rx_topic_len,
		    ctx.publish_payload_remaining);
}

/* Event-specific MQTT 5.0 detail, or -1 when there is none to report. */
static int mqtt_evt_detail(const struct mqtt_evt *evt)
{
#if defined(CONFIG_MQTT_VERSION_5_0)
	if (client.protocol_version != MQTT_VERSION_5_0 || evt->result != 0) {
		return -1;
	}

	switch (evt->type) {
	case MQTT_EVT_CONNACK:
		return evt->param.connack.session_present_flag != 0;
	case MQTT_EVT_PUBACK:
		return evt->param.puback.reason_code;
	case MQTT_EVT_PUBREC:
		return evt->param.pubrec.reason_code;
	case MQTT_EVT_PUBREL:
		return evt->param.pubrel.reason_code;
	case MQTT_EVT_PUBCOMP:
		return evt->param.pubcomp.reason_code;
	case MQTT_EVT_SUBACK:
		/* AT#XMQTTSUB subscribes to one topic, so there is one reason code. */
		if (evt->param.suback.return_codes.len == 0) {
			return -1;
		}
		return evt->param.suback.return_codes.data[0];
	case MQTT_EVT_UNSUBACK:
		if (evt->param.unsuback.reason_codes.len == 0) {
			return -1;
		}
		return evt->param.unsuback.reason_codes.data[0];
	default:
		return -1;
	}
#else
	ARG_UNUSED(evt);
	return -1;
#endif
}

/**@brief MQTT client event handler
 */
void mqtt_evt_handler(struct mqtt_client *const c, const struct mqtt_evt *evt)
{
	int ret;
	int detail;

	ret = evt->result;
	if (ret == 0 && evt->type >= MQTT_EVT_PUBACK && evt->type <= MQTT_EVT_UNSUBACK) {
		/* message_id is the first member of every ack parameter struct (C11 6.5.2.3). */
		LOG_DBG("MQTT ack type=%d id=%u", evt->type, evt->param.puback.message_id);
	}

	switch (evt->type) {
	case MQTT_EVT_CONNACK:
		if (evt->result != 0) {
			ctx.connected = false;
		}
#if defined(CONFIG_MQTT_VERSION_5_0)
		if (evt->result == 0 && client.protocol_version == MQTT_VERSION_5_0) {
			mqtt_capabilities_reset();
#if SM_MQTT_TOPIC_ALIASES > 0
			mqtt_alias_connack(&evt->param.connack);
#endif
			ctx.receive_maximum = UINT16_MAX;
			if (evt->param.connack.prop.rx.has_receive_maximum) {
				ctx.receive_maximum = evt->param.connack.prop.receive_maximum;
				if (ctx.receive_maximum == 0) {
					LOG_ERR("Invalid MQTT receive maximum: 0");
					ctx.protocol_error = -EBADMSG;
				}
			}
			if (evt->param.connack.prop.rx.has_maximum_qos) {
				ctx.maximum_qos = evt->param.connack.prop.maximum_qos;
			}
			if (evt->param.connack.prop.rx.has_retain_available) {
				ctx.retain_available =
					evt->param.connack.prop.retain_available != 0;
			}
			if (evt->param.connack.prop.rx.has_wildcard_sub_available) {
				ctx.wildcard_sub_available =
					evt->param.connack.prop.wildcard_sub_available != 0;
			}
			if (evt->param.connack.prop.rx.has_shared_sub_available) {
				ctx.shared_sub_available =
					evt->param.connack.prop.shared_sub_available != 0;
			}
			ctx.maximum_packet_size = UINT32_MAX;
			if (evt->param.connack.prop.rx.has_maximum_packet_size) {
				ctx.maximum_packet_size =
					evt->param.connack.prop.maximum_packet_size;
				if (ctx.maximum_packet_size == 0) {
					LOG_ERR("Invalid MQTT maximum packet size: 0");
					ctx.protocol_error = -EBADMSG;
				} else {
					int err = mqtt_check_packet_size(0);

					if (err != 0) {
						ctx.protocol_error = err;
					}
				}
			}
			if (evt->param.connack.prop.rx.has_server_keep_alive) {
				client.keepalive = evt->param.connack.prop.server_keep_alive;
				LOG_INF("MQTT server keep alive: %u", client.keepalive);
			}
		}
#endif
		break;

	case MQTT_EVT_DISCONNECT:
		ctx.connected = false;
		break;

	case MQTT_EVT_PUBLISH:
		handle_mqtt_publish_evt(c, evt);
		break;

#if defined(CONFIG_MQTT_VERSION_5_0)
	case MQTT_EVT_PUBACK:
	case MQTT_EVT_PUBCOMP:
	case MQTT_EVT_SUBACK:
	case MQTT_EVT_UNSUBACK:
		if (evt->result == 0) {
			/* message_id is the first member of every ack parameter struct. */
			mqtt_inflight_ack(evt->param.puback.message_id, evt->type);
		}
		break;
#endif

	case MQTT_EVT_PUBREC:
		if (evt->result != 0) {
			break;
		}
#if defined(CONFIG_MQTT_VERSION_5_0)
		/* MQTT 5.0 section 4.3.3: no PUBREL after a PUBREC failure reason code. */
		if (evt->param.pubrec.reason_code >= SM_MQTT_REASON_CODE_FAILURE) {
			mqtt_inflight_ack(evt->param.pubrec.message_id, MQTT_EVT_PUBREC);
			LOG_WRN("PUBREC id=%u failed: 0x%02x", evt->param.pubrec.message_id,
				evt->param.pubrec.reason_code);
			break;
		}
#endif
		{
			struct mqtt_pubrel_param param = {.message_id =
								  evt->param.pubrec.message_id};
			ret = mqtt_check_packet_size(2);
			if (ret != 0) {
				ctx.protocol_error = ret;
				break;
			}
			ret = mqtt_publish_qos2_release(&client, &param);
			if (ret) {
				LOG_ERR("QoS2 release failed: %d", ret);
			}
#if defined(CONFIG_MQTT_VERSION_5_0)
			if (ret == 0 && client.protocol_version == MQTT_VERSION_5_0) {
				struct mqtt_inflight *item =
					mqtt_inflight_find(evt->param.pubrec.message_id);

				if (item != NULL && (item->ack_evt == MQTT_EVT_PUBREC ||
						     item->ack_evt == MQTT_EVT_PUBCOMP)) {
					item->ack_evt = MQTT_EVT_PUBCOMP;
				}
			}
#endif
		}
		break;

	case MQTT_EVT_PUBREL:
		if (evt->result != 0) {
			break;
		}
		{
			struct mqtt_pubcomp_param param = {
				.message_id = evt->param.pubrel.message_id
			};
			ret = mqtt_check_packet_size(2);
			if (ret != 0) {
				ctx.protocol_error = ret;
				break;
			}
			ret = mqtt_publish_qos2_complete(&client, &param);
			if (ret) {
				LOG_ERR("QoS2 complete failed: %d", ret);
			}
		}
		break;

	default:
		break;
	}

	/* #XMQTTEVT for MQTT_EVT_PUBLISH is sent after the payload in mqtt_poll_work_handler.
	 * It cannot be buffered here via urc_send_to: sm_at_host_lock() flushes pending URCs,
	 * which would place it ahead of the #XMQTTMSG header and payload.
	 */
	if (evt->type != MQTT_EVT_PUBLISH) {
		detail = mqtt_evt_detail(evt);
		if (detail >= 0) {
			urc_send_to(ctx.pipe, "\r\n#XMQTTEVT: %d,%d,%d\r\n", evt->type, ret,
				    detail);
		} else {
			urc_send_to(ctx.pipe, "\r\n#XMQTTEVT: %d,%d\r\n", evt->type, ret);
		}
	}
}

static void mqtt_poll_work_handler(struct k_work *work);
static void mqtt_keepalive_work_handler(struct k_work *work);
static void mqtt_poll_cb(const struct socket_ncs_pollcb_params *params);

static void mqtt_conn_release(void)
{
#if defined(CONFIG_MQTT_VERSION_5_0)
	while (mqtt_conn != NULL && mqtt_conn->inflight != NULL) {
		mqtt_inflight_remove(mqtt_conn->inflight->message_id);
	}
#endif
	ctx.username.utf8 = NULL;
	ctx.username.size = 0;
	ctx.password.utf8 = NULL;
	ctx.password.size = 0;
	client.user_name = NULL;
	client.password = NULL;
	client.rx_buf = NULL;
	client.tx_buf = NULL;
#if defined(CONFIG_MQTT_LIB_TLS)
	client.transport.tls.config.hostname = NULL;
#endif
	free(mqtt_conn);
	mqtt_conn = NULL;
}

static atomic_t mqtt_poll_revents;
static K_WORK_DEFINE(mqtt_poll_work, mqtt_poll_work_handler);
static K_WORK_DELAYABLE_DEFINE(mqtt_keepalive_work, mqtt_keepalive_work_handler);

static void mqtt_keepalive_reschedule(void)
{
	if (client.keepalive == 0) {
		k_work_cancel_delayable(&mqtt_keepalive_work);
		return;
	}

	k_work_reschedule_for_queue(&sm_work_q, &mqtt_keepalive_work,
				    K_MSEC(mqtt_keepalive_time_left(&client)));
}

static void mqtt_connection_abort(int err)
{
	LOG_ERR("MQTT connection aborted: %d", err);
	(void)mqtt_abort(&client);
	ctx.connected = false;
	if (ctx.rx_topic_len == 0 && ctx.publish_payload_remaining > 0) {
		/* drain_publish_payload spanned multiple poll work invocations:
		 * the topic header was forwarded and the pipe is held locked. Release it.
		 */
		sm_at_host_unlock(ctx.pipe);
	}
	ctx.rx_topic_len = 0;
	ctx.publish_payload_remaining = 0;
	client.broker = NULL;
	mqtt_conn_release();
	k_work_cancel_delayable(&mqtt_keepalive_work);
}

static int mqtt_arm_poll_cb(void)
{
	int fd;

#if defined(CONFIG_MQTT_LIB_TLS)
	fd = (client.transport.type == MQTT_TRANSPORT_SECURE)
		? client.transport.tls.sock
		: client.transport.tcp.sock;
#else
	fd = client.transport.tcp.sock;
#endif

	struct socket_ncs_pollcb pcb = {
		.callback = mqtt_poll_cb,
		.events = ZSOCK_POLLIN,
		.oneshot = true,
	};

	int err = zsock_setsockopt(fd, SOL_SOCKET, SO_POLLCB, &pcb, sizeof(pcb));

	if (err < 0) {
		LOG_ERR("SO_POLLCB error: %d", -errno);
		return -errno;
	}
	return 0;
}

/* Called in IRQ context */
static void mqtt_poll_cb(const struct socket_ncs_pollcb_params *params)
{
	atomic_or(&mqtt_poll_revents, params->revents);
	k_work_submit_to_queue(&sm_work_q, &mqtt_poll_work);
}

static void mqtt_poll_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	uint32_t revents = atomic_clear(&mqtt_poll_revents);
	int err = 0;

	if (!ctx.connected) {
		return;
	}

	if (revents & ZSOCK_POLLIN) {
		if (ctx.rx_topic_len == 0 && ctx.publish_payload_remaining == 0) {
			err = mqtt_input(&client);
			if (err == 0) {
				err = ctx.protocol_error;
			}
			if (err != 0) {
				LOG_ERR("MQTT input failed: %d", err);
				goto abort;
			}
		}

		if (ctx.rx_topic_len > 0 || ctx.publish_payload_remaining > 0) {
			if (ctx.rx_topic_len > 0) {
				sm_at_host_lock(ctx.pipe);
				/* Use rsp_send_to (immediate) so the header precedes the
				 * payload; urc_send_to buffers and would trail the data.
				 */
				mqtt_msg_header_send();
				data_send(ctx.pipe, mqtt_conn->payload_drain, ctx.rx_topic_len);
				data_send(ctx.pipe, "\r\n", 2);
				ctx.rx_topic_len = 0;
			}
			if (drain_publish_payload(&client) == -EAGAIN) {
				(void)mqtt_arm_poll_cb();
				return;
			}
			data_send(ctx.pipe, "\r\n", 2);
			rsp_send_to(ctx.pipe, "\r\n#XMQTTEVT: %d,0\r\n", MQTT_EVT_PUBLISH);
			sm_at_host_unlock(ctx.pipe);
		}

		/* Reschedule keepalive from now after receiving data. */
		mqtt_keepalive_reschedule();
	}

	/* MQTT v3.1.1: Note that a Server is permitted to disconnect a Client that it
	 * determines to be inactive or non-responsive at any time, regardless of the
	 * Keep Alive value provided by that Client.
	 */
	if (revents & ZSOCK_POLLERR) {
		LOG_ERR("ZSOCK_POLLERR");
		err = -EIO;
		goto abort;
	}
	if (revents & ZSOCK_POLLHUP) {
		LOG_ERR("ZSOCK_POLLHUP");
		err = -ECONNRESET;
		goto abort;
	}
	if (revents & ZSOCK_POLLNVAL) {
		if (ctx.disconnect_requested) {
			/* MQTT library closed the socket during disconnect. */
			return;
		}
		LOG_ERR("ZSOCK_POLLNVAL");
		err = -ENOTCONN;
		goto abort;
	}

	/* Re-arm oneshot poll callback for next event. */
	(void)mqtt_arm_poll_cb();
	return;

abort:
	mqtt_connection_abort(err);
}

static void mqtt_keepalive_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	if (!ctx.connected || client.keepalive == 0) {
		return;
	}

	/* MQTT v3.1.1: If a Client does not receive a PINGRESP Packet within a
	 * reasonable amount of time after it has sent a PINGREQ, it SHOULD close
	 * the Network Connection to the Server.
	 */
	if (client.unacked_ping > 1) {
		LOG_ERR("MQTT ping nack: %d", client.unacked_ping);
		mqtt_connection_abort(-ENETRESET);
		return;
	}

	int err = mqtt_check_packet_size(0);

	if (err == 0) {
		err = mqtt_live(&client);
	}

	if (err != 0 && err != -EAGAIN) {
		LOG_ERR("MQTT keepalive failed: %d", err);
		mqtt_connection_abort(err);
		return;
	}

	mqtt_keepalive_reschedule();
}

/**@brief Resolves the configured hostname and
 * initializes the MQTT broker structure
 */
static int broker_init(void)
{
	int err;
	struct sockaddr sa = {
		.sa_family = AF_UNSPEC
	};

	err = util_resolve_host(0, mqtt_conn->broker_url, mqtt_broker_port, ctx.family, &sa);
	if (err) {
		return -EAGAIN;
	}
	if (sa.sa_family == NET_AF_INET) {
		ctx.broker = *(struct sockaddr_in *)&sa;
	} else {
		ctx.broker6 = *(struct sockaddr_in6 *)&sa;
	}

	return 0;
}

static bool mqtt_version_supported(uint16_t version)
{
	return version == MQTT_VERSION_3_1_1 ||
	       (IS_ENABLED(CONFIG_MQTT_VERSION_5_0) && version == MQTT_VERSION_5_0);
}

/**@brief Configure the MQTT client structure
 */
static int do_mqtt_config(uint16_t keep_alive, uint8_t clean_session, uint16_t version,
			  uint32_t session_expiry)
{
	if (ctx.connected) {
		return -EINVAL;
	}
	if (clean_session != 0 && clean_session != 1) {
		return -EINVAL;
	}
	if (!mqtt_version_supported(version) ||
	    (session_expiry != 0 && version != MQTT_VERSION_5_0)) {
		LOG_ERR("Invalid MQTT configuration: version=%u session_expiry=%u", version,
			session_expiry);
		return -EINVAL;
	}

	/* Init MQTT client */
	mqtt_client_init(&client);

	/* Overrides the default of mqtt_client_init(), which depends on library support. */
	client.protocol_version = (uint8_t)version;
	client.evt_cb = mqtt_evt_handler;

	/* MQTT client id configuration */
	client.client_id.utf8 = mqtt_clientid;
	client.client_id.size = strlen(mqtt_clientid);

	/* MQTT buffers are assigned in do_mqtt_connect() once mqtt_conn is allocated. */

	/* MQTT Keep Alive configuration */
	client.keepalive = keep_alive;
	mqtt_configured_keepalive = keep_alive;
	/* MQTT Clean Session configuration */
	client.clean_session = clean_session;
#if defined(CONFIG_MQTT_VERSION_5_0)
	client.prop.session_expiry_interval = session_expiry;
#endif

	return 0;
}

static int do_mqtt_connect(void)
{
	int err;

	ctx.pipe = sm_at_host_get_current_pipe();

	client.rx_buf = mqtt_conn->rx;
	client.rx_buf_size = sizeof(mqtt_conn->rx);
	client.tx_buf = mqtt_conn->tx;
	client.tx_buf_size = sizeof(mqtt_conn->tx);

	/* Init MQTT broker */
	err = broker_init();
	if (err) {
		goto fail;
	}

	/* MQTT client configuration */
	if (ctx.family == NET_AF_INET) {
		client.broker = &ctx.broker;
	} else {
		client.broker = &ctx.broker6;
	}
	client.password = NULL;
	if (ctx.username.size > 0) {
		client.user_name = &ctx.username;
		if (ctx.password.size > 0) {
			client.password = &ctx.password;
		}
	} else {
		client.user_name = NULL;
		/* ignore password if no user_name */
	}
#if defined(CONFIG_MQTT_LIB_TLS)
	if (ctx.sec_tag != SEC_TAG_TLS_INVALID) {
		struct mqtt_sec_config *tls_config;

		tls_config = &(client.transport).tls.config;
		tls_config->peer_verify   = TLS_PEER_VERIFY_REQUIRED;
		tls_config->cipher_list   = NULL;
		tls_config->cipher_count  = 0;
		tls_config->sec_tag_count = 1;
		tls_config->sec_tag_list  = (int *)&ctx.sec_tag;
		tls_config->hostname      = mqtt_conn->broker_url;
		client.transport.type     = MQTT_TRANSPORT_SECURE;
	} else {
		client.transport.type     = MQTT_TRANSPORT_NON_SECURE;
	}
#else
	client.transport.type = MQTT_TRANSPORT_NON_SECURE;
#endif

	/* Connect to MQTT broker */
	client.keepalive = mqtt_configured_keepalive;
	ctx.protocol_error = 0;
#if defined(CONFIG_MQTT_VERSION_5_0)
	ctx.maximum_packet_size = UINT32_MAX;
	ctx.receive_maximum = UINT16_MAX;
	mqtt_capabilities_reset();
#endif
	err = mqtt_connect(&client);
	if (err != 0) {
		LOG_ERR("MQTT connect failed: %d", err);
		goto fail;
	}

	ctx.connected = true;
	ctx.disconnect_requested = false;

	err = mqtt_arm_poll_cb();
	if (err) {
		ctx.connected = false;
		client.broker = NULL;
		goto fail;
	}

	mqtt_keepalive_reschedule();

	return 0;

fail:
	mqtt_conn_release();
	return err;
}

static int do_mqtt_disconnect(void)
{
	int err;

	if (!ctx.connected) {
		return -ENOTCONN;
	}

	ctx.disconnect_requested = true;
	err = mqtt_check_packet_size(0);
	if (err == 0) {
		err = mqtt_disconnect(&client, NULL);
	}
	if (err) {
		/* A graceful DISCONNECT is not always possible. mqtt_disconnect()
		 * checks verify_tx_state(), so it fails with -ENOTCONN whenever the
		 * CONNACK has not arrived yet, and it fails on a broken link. Close
		 * the connection anyway: mqtt_abort() has no such check, and leaving
		 * ctx.connected set would strand the client with no way out.
		 */
		LOG_WRN("MQTT disconnect failed: %d, aborting", err);
		mqtt_connection_abort(err);
		return 0;
	}

	k_work_cancel_delayable(&mqtt_keepalive_work);
	ctx.connected = false;
	client.broker = NULL;
	mqtt_conn_release();

	return 0;
}

static int mqtt_check_publish(size_t msg_len)
{
#if defined(CONFIG_MQTT_VERSION_5_0)
	if (client.protocol_version == MQTT_VERSION_5_0) {
		if (mqtt_conn->pub_param.message.topic.qos > ctx.maximum_qos ||
		    (mqtt_conn->pub_param.retain_flag && !ctx.retain_available)) {
			LOG_ERR("MQTT publish exceeds server limits: max_qos=%u retain=%d",
				ctx.maximum_qos, ctx.retain_available);
			return -ENOTSUP;
		}
		if (mqtt_conn->pub_param.message.topic.qos != MQTT_QOS_0_AT_MOST_ONCE &&
		    mqtt_conn->inflight_count >= ctx.receive_maximum) {
			LOG_WRN("MQTT receive maximum reached: %u", ctx.receive_maximum);
			return -EAGAIN;
		}
	}
#endif
	/* MQTT 5 adds a Property Length byte and the optional Message Expiry Interval
	 * property (identifier + four bytes), the only outgoing property exposed by the AT API.
	 * QoS 1/2 also include a two-byte packet identifier.
	 */
	size_t remaining = 2 + mqtt_conn->pub_param.message.topic.topic.size + msg_len;

	if (mqtt_conn->pub_param.message.topic.qos != MQTT_QOS_0_AT_MOST_ONCE) {
		remaining += 2;
	}
	if (client.protocol_version == MQTT_VERSION_5_0) {
		remaining++;
#if defined(CONFIG_MQTT_VERSION_5_0)
		if (mqtt_conn->pub_param.prop.message_expiry_interval != 0) {
			remaining += 1 + sizeof(uint32_t);
		}
		if (mqtt_conn->pub_param.prop.topic_alias != 0) {
			remaining += 1 + sizeof(uint16_t);
		}
#endif
	}
	return mqtt_check_packet_size(remaining);
}

static int do_mqtt_publish(uint8_t *msg, size_t msg_len)
{
	int err;

	if (!ctx.connected || mqtt_conn == NULL) {
		return -ENOTCONN;
	}

	err = mqtt_check_publish(msg_len);
	if (err != 0) {
		return err;
	}

	/* MQTT client does not store packets, so we will not retransmit packets
	 * that are lacking response. This deviates from MQTT v3.1.1.
	 */
	mqtt_conn->pub_param.message.payload.data = msg;
	mqtt_conn->pub_param.message.payload.len  = msg_len;

#if defined(CONFIG_MQTT_VERSION_5_0)
	bool tracked = false;
	uint16_t message_id = mqtt_conn->pub_param.message_id;

	if (client.protocol_version == MQTT_VERSION_5_0 &&
	    mqtt_conn->pub_param.message.topic.qos != MQTT_QOS_0_AT_MOST_ONCE) {
		uint8_t ack_evt = mqtt_conn->pub_param.message.topic.qos == MQTT_QOS_1_AT_LEAST_ONCE
					  ? MQTT_EVT_PUBACK
					  : MQTT_EVT_PUBREC;

		err = mqtt_inflight_add(message_id, ack_evt);
		if (err != 0) {
			return err;
		}
		tracked = true;
	}
#endif
	err = mqtt_publish(&client, &mqtt_conn->pub_param);
#if SM_MQTT_TOPIC_ALIASES > 0
	if (err == 0) {
		mqtt_alias_commit();
	}
#endif
#if defined(CONFIG_MQTT_VERSION_5_0)
	if (err != 0 && tracked) {
		mqtt_inflight_remove(message_id);
	}
#endif
	return err;
}

static int do_mqtt_subscribe(uint16_t op,
				uint8_t *topic_buf,
				size_t topic_len,
				uint16_t qos)
{
	int err = -EINVAL;
	int message_id;
	struct mqtt_topic subscribe_topic;
	struct mqtt_subscription_list subscription_list = {
		.list = &subscribe_topic,
		.list_count = 1,
	};

	if (qos <= MQTT_QOS_2_EXACTLY_ONCE) {
		subscribe_topic.qos = (uint8_t)qos;
	} else {
		return err;
	}
	subscribe_topic.topic.utf8 = topic_buf;
	subscribe_topic.topic.size = topic_len;

#if defined(CONFIG_MQTT_VERSION_5_0)
	if (client.protocol_version == MQTT_VERSION_5_0 && op == AT_MQTTSUB_SUB) {
		if ((!ctx.shared_sub_available && topic_len >= sizeof("$share/") - 1 &&
		     memcmp(topic_buf, "$share/", sizeof("$share/") - 1) == 0) ||
		    (!ctx.wildcard_sub_available && (memchr(topic_buf, '+', topic_len) != NULL ||
						     memchr(topic_buf, '#', topic_len) != NULL))) {
			LOG_ERR("MQTT subscription exceeds server limits: wildcard=%d shared=%d",
				ctx.wildcard_sub_available, ctx.shared_sub_available);
			return -ENOTSUP;
		}
	}
#endif

	size_t remaining = 2 + 2 + topic_len + (op == AT_MQTTSUB_SUB ? 1 : 0);

	if (client.protocol_version == MQTT_VERSION_5_0) {
		remaining++;
	}
	err = mqtt_check_packet_size(remaining);
	if (err != 0) {
		return err;
	}

	message_id = mqtt_message_id_next();
	if (message_id < 0) {
		return message_id;
	}
	subscription_list.message_id = (uint16_t)message_id;
#if defined(CONFIG_MQTT_VERSION_5_0)
	if (client.protocol_version == MQTT_VERSION_5_0) {
		err = mqtt_inflight_add(subscription_list.message_id,
					op == AT_MQTTSUB_SUB ? MQTT_EVT_SUBACK : MQTT_EVT_UNSUBACK);
		if (err != 0) {
			return err;
		}
	}
#endif

	if (op == AT_MQTTSUB_SUB) {
		err = mqtt_subscribe(&client, &subscription_list);
	} else {
		err = mqtt_unsubscribe(&client, &subscription_list);
	}
#if defined(CONFIG_MQTT_VERSION_5_0)
	if (err != 0) {
		mqtt_inflight_remove(subscription_list.message_id);
	}
#endif

	return err;
}

SM_AT_CMD_CUSTOM(xmqttcfg, "AT#XMQTTCFG", handle_at_mqtt_config);
STATIC int handle_at_mqtt_config(enum at_parser_cmd_type cmd_type, struct at_parser *parser,
				 uint32_t param_count)
{
	int err = -EINVAL;
	uint16_t keep_alive = CONFIG_MQTT_KEEPALIVE;
	uint16_t clean_session = MQTT_CLEAN_SESSION_INIT;
	uint16_t version = SM_MQTT_VERSION_DEFAULT;
	uint32_t session_expiry = 0;

	switch (cmd_type) {
	case AT_PARSER_CMD_TYPE_SET:
		size_t clientid_sz = sizeof(mqtt_clientid);

		err = util_string_get(parser, 1, mqtt_clientid, &clientid_sz);
		if (err) {
			return err;
		}
		if (param_count > 2) {
			err = at_parser_num_get(parser, 2, &keep_alive);
			if (err) {
				return err;
			}
		}
		if (param_count > 3) {
			err = at_parser_num_get(parser, 3, &clean_session);
			if (err) {
				return err;
			}
		}
		if (param_count > 4) {
			err = at_parser_num_get(parser, 4, &version);
			if (err) {
				return err;
			}
		}
		if (param_count > 5) {
			err = at_parser_num_get(parser, 5, &session_expiry);
			if (err) {
				return err;
			}
		}
		err = do_mqtt_config(keep_alive, (uint8_t)clean_session, version, session_expiry);
		break;

	case AT_PARSER_CMD_TYPE_READ:
#if defined(CONFIG_MQTT_VERSION_5_0)
		session_expiry = client.prop.session_expiry_interval;
#endif
		rsp_send("\r\n#XMQTTCFG: \"%s\",%d,%d,%d,%u\r\n", mqtt_clientid,
			 mqtt_configured_keepalive, client.clean_session, client.protocol_version,
			 session_expiry);
		err = 0;
		break;

	case AT_PARSER_CMD_TYPE_TEST:
		rsp_send("\r\n#XMQTTCFG: <client_id>,<keep_alive>,<clean_session>,<version>,"
			 "<session_expiry>\r\n");
		err = 0;
		break;

	default:
		break;
	}

	return err;
}

SM_AT_CMD_CUSTOM(xmqttcon, "AT#XMQTTCON", handle_at_mqtt_connect);
STATIC int handle_at_mqtt_connect(enum at_parser_cmd_type cmd_type, struct at_parser *parser,
				  uint32_t param_count)
{
	int err = -EINVAL;
	uint16_t op;

	switch (cmd_type) {
	case AT_PARSER_CMD_TYPE_SET:
		err = at_parser_num_get(parser, 1, &op);
		if (err) {
			return err;
		}
		if (op == MQTTC_CONNECT || op == MQTTC_CONNECT6)  {
			if (ctx.connected) {
				return -EISCONN;
			}

			if (mqtt_conn != NULL) {
				mqtt_conn_release();  /* Maybe left over after failed I/O */
			}

			mqtt_conn = calloc(1, sizeof(*mqtt_conn));
			if (!mqtt_conn) {
				LOG_ERR("Failed to allocate %s", "MQTT connection");
				return -ENOMEM;
			}

			size_t username_sz = sizeof(mqtt_conn->username);
			size_t password_sz = sizeof(mqtt_conn->password);
			size_t url_sz = sizeof(mqtt_conn->broker_url);

			err = util_string_get(parser, 2, mqtt_conn->username, &username_sz);
			if (err) {
				mqtt_conn_release();
				return err;
			}
			ctx.username.utf8 = mqtt_conn->username;
			ctx.username.size = strlen(mqtt_conn->username);

			err = util_string_get(parser, 3, mqtt_conn->password, &password_sz);
			if (err) {
				mqtt_conn_release();
				return err;
			}
			ctx.password.utf8 = mqtt_conn->password;
			ctx.password.size = strlen(mqtt_conn->password);

			err = util_string_get(parser, 4, mqtt_conn->broker_url, &url_sz);
			if (err) {
				mqtt_conn_release();
				return err;
			}
			err = at_parser_num_get(parser, 5, &mqtt_broker_port);
			if (err) {
				mqtt_conn_release();
				return err;
			}
			ctx.sec_tag = SEC_TAG_TLS_INVALID;
			if (param_count > 6) {
				err = at_parser_num_get(parser, 6, &ctx.sec_tag);
				if (err) {
					mqtt_conn_release();
					return err;
				}
			}
			ctx.family = (op == MQTTC_CONNECT) ? NET_AF_INET : NET_AF_INET6;
			err = do_mqtt_connect();
		} else if (op == MQTTC_DISCONNECT) {
			err = do_mqtt_disconnect();
		} else {
			err = -EINVAL;
		}
		break;

	case AT_PARSER_CMD_TYPE_READ:
		if (ctx.connected) {
			if (ctx.sec_tag != SEC_TAG_TLS_INVALID) {
				rsp_send("\r\n#XMQTTCON: %d,\"%s\",\"%s\",%d,%d\r\n",
					 ctx.connected, mqtt_clientid, mqtt_conn->broker_url,
					 mqtt_broker_port, ctx.sec_tag);
			} else {
				rsp_send("\r\n#XMQTTCON: %d,\"%s\",\"%s\",%d\r\n",
					 ctx.connected, mqtt_clientid, mqtt_conn->broker_url,
					 mqtt_broker_port);
			}
		} else {
			rsp_send("\r\n#XMQTTCON: %d\r\n", ctx.connected);
		}
		err = 0;
		break;

	case AT_PARSER_CMD_TYPE_TEST:
		rsp_send("\r\n#XMQTTCON: (%d,%d,%d),<username>,"
			 "<password>,<url>,<port>,<sec_tag>\r\n",
			 MQTTC_DISCONNECT, MQTTC_CONNECT, MQTTC_CONNECT6);
		err = 0;
		break;

	default:
		break;
	}

	return err;
}

static int mqtt_datamode_callback(uint8_t op, const uint8_t *data, int len, uint8_t flags)
{
	int ret = 0;

	if (op == DATAMODE_SEND) {
		if ((flags & SM_DATAMODE_FLAGS_MORE_DATA) != 0) {
			LOG_ERR("Data mode buffer overflow");
			exit_datamode_handler(sm_at_host_get_current(), -EOVERFLOW);
			return -EOVERFLOW;
		}
		ret = do_mqtt_publish((uint8_t *)data, len);
		if (ret < 0) {
			LOG_ERR("Send failed: %d", ret);
			return ret;
		}
		/* Return the amount of data sent. */
		return len;
	}

	return 0;
}

static int mqtt_prepare_publish(size_t topic_len, uint16_t qos, uint16_t retain,
				uint32_t msg_expiry)
{
	if (qos > MQTT_QOS_2_EXACTLY_ONCE || retain > 1 ||
	    (msg_expiry != 0 && client.protocol_version != MQTT_VERSION_5_0)) {
		return -EINVAL;
	}

	mqtt_conn->pub_param.message.topic.qos = (uint8_t)qos;
	mqtt_conn->pub_param.retain_flag = (uint8_t)retain;
	mqtt_conn->pub_param.message.topic.topic.utf8 = mqtt_conn->pub_topic;
	mqtt_conn->pub_param.message.topic.topic.size = topic_len;
	mqtt_conn->pub_param.dup_flag = 0;
#if defined(CONFIG_MQTT_VERSION_5_0)
	mqtt_conn->pub_param.prop.message_expiry_interval = msg_expiry;
#endif
#if SM_MQTT_TOPIC_ALIASES > 0
	mqtt_alias_select();
#endif

	int message_id = mqtt_message_id_next();

	if (message_id < 0) {
		return message_id;
	}
	mqtt_conn->pub_param.message_id = (uint16_t)message_id;
	return 0;
}

SM_AT_CMD_CUSTOM(xmqttpubempty, "AT#XMQTTPUBEMPTY", handle_at_mqtt_publish_empty);
STATIC int handle_at_mqtt_publish_empty(enum at_parser_cmd_type cmd_type, struct at_parser *parser,
					uint32_t param_count)
{
	int err;
	uint16_t qos = MQTT_QOS_0_AT_MOST_ONCE;
	uint16_t retain = 0;
	uint32_t msg_expiry = 0;
	size_t topic_len = MQTT_MAX_TOPIC_LEN;

	if (cmd_type == AT_PARSER_CMD_TYPE_TEST) {
		rsp_send("\r\n#XMQTTPUBEMPTY: <topic>,(0,1,2),(0,1)[,<msg_expiry>]\r\n");
		return 0;
	}
	if (cmd_type != AT_PARSER_CMD_TYPE_SET || param_count < 2 || param_count > 5) {
		return -EINVAL;
	}
	if (!ctx.connected) {
		return -ENOTCONN;
	}
	err = util_string_get(parser, 1, mqtt_conn->pub_topic, &topic_len);
	if (err != 0) {
		return err;
	}
	if (topic_len == 0) {
		return -EINVAL;
	}
	if (param_count > 2) {
		err = at_parser_num_get(parser, 2, &qos);
		if (err != 0) {
			return err;
		}
	}
	if (param_count > 3) {
		err = at_parser_num_get(parser, 3, &retain);
		if (err != 0) {
			return err;
		}
	}
	if (param_count > 4) {
		err = at_parser_num_get(parser, 4, &msg_expiry);
		if (err != 0) {
			return err;
		}
	}
	err = mqtt_prepare_publish(topic_len, qos, retain, msg_expiry);
	if (err != 0) {
		return err;
	}
	/* Keep a valid pointer even for transports that inspect an empty I/O vector. */
	return do_mqtt_publish(mqtt_conn->pub_topic, 0);
}

SM_AT_CMD_CUSTOM(xmqttpub, "AT#XMQTTPUB", handle_at_mqtt_publish);
STATIC int handle_at_mqtt_publish(enum at_parser_cmd_type cmd_type, struct at_parser *parser,
				  uint32_t param_count)
{
	int err = -EINVAL;

	uint16_t qos = MQTT_QOS_0_AT_MOST_ONCE;
	uint16_t retain = 0;
	size_t topic_sz = MQTT_MAX_TOPIC_LEN;
	const char *pub_msg_ptr = NULL;
	size_t msg_sz = 0;
	int data_len = 0;
	uint32_t msg_expiry = 0;

	if (!ctx.connected) {
		return -ENOTCONN;
	}

	switch (cmd_type) {
	case AT_PARSER_CMD_TYPE_SET:
		err = util_string_get(parser, 1, mqtt_conn->pub_topic, &topic_sz);
		if (err) {
			return err;
		}
		if (param_count > 2) {
			err = at_parser_string_ptr_get(parser, 2, &pub_msg_ptr, &msg_sz);
			if (err) {
				return err;
			}
		}
		if (param_count > 3) {
			err = at_parser_num_get(parser, 3, &qos);
			if (err) {
				return err;
			}
		}
		if (param_count > 4) {
			err = at_parser_num_get(parser, 4, &retain);
			if (err) {
				return err;
			}
		}
		if (param_count > 5) {
			err = at_parser_num_get(parser, 5, &data_len);
			if (err) {
				return err;
			}
			/* The payload must fit in the data mode buffer, as it is
			 * published as a single message.
			 */
			if (data_len < 0 || data_len > CONFIG_SM_DATAMODE_BUF_SIZE) {
				return -EINVAL;
			}
			if (data_len > 0 && msg_sz > 0) {
				/* <data_len> only applies to data mode */
				return -EINVAL;
			}
		}
		if (param_count > 6) {
			err = at_parser_num_get(parser, 6, &msg_expiry);
			if (err) {
				return err;
			}
		}

		err = mqtt_prepare_publish(topic_sz, qos, retain, msg_expiry);
		if (err != 0) {
			return err;
		}
		if (pub_msg_ptr == NULL || msg_sz == 0) {
			/* Publish payload in data mode */
			err = mqtt_check_publish(data_len);
			if (err != 0) {
				return err;
			}
			err = enter_datamode(mqtt_datamode_callback, data_len);
		} else {
			err = do_mqtt_publish((uint8_t *)pub_msg_ptr, msg_sz);
		}
		break;

	case AT_PARSER_CMD_TYPE_TEST:
		rsp_send("\r\n#XMQTTPUB: <topic>,<msg>,(0,1,2),(0,1)"
			 "[,<data_len>[,<msg_expiry>]]\r\n");
		err = 0;
		break;

	default:
		break;
	}

	return err;
}

SM_AT_CMD_CUSTOM(xmqttsub, "AT#XMQTTSUB", handle_at_mqtt_subscribe);
STATIC int handle_at_mqtt_subscribe(enum at_parser_cmd_type cmd_type,
				    struct at_parser *parser, uint32_t param_count)
{
	int err = -EINVAL;
	uint16_t qos;
	char topic[MQTT_MAX_TOPIC_LEN];
	int topic_sz = MQTT_MAX_TOPIC_LEN;

	if (!ctx.connected) {
		return -ENOTCONN;
	}

	switch (cmd_type) {
	case AT_PARSER_CMD_TYPE_SET:
		if (param_count == 3) {
			err = util_string_get(parser, 1, topic, &topic_sz);
			if (err < 0) {
				return err;
			}
			err = at_parser_num_get(parser, 2, &qos);
			if (err < 0) {
				return err;
			}
			err = do_mqtt_subscribe(AT_MQTTSUB_SUB, topic, topic_sz, qos);
		} else {
			return -EINVAL;
		}
		break;

	case AT_PARSER_CMD_TYPE_TEST:
		rsp_send("\r\n#XMQTTSUB: <topic>,(0,1,2)\r\n");
		err = 0;
		break;

	default:
		break;
	}

	return err;
}

SM_AT_CMD_CUSTOM(xmqttunsub, "AT#XMQTTUNSUB", handle_at_mqtt_unsubscribe);
STATIC int handle_at_mqtt_unsubscribe(enum at_parser_cmd_type cmd_type,
				      struct at_parser *parser, uint32_t param_count)
{
	int err = -EINVAL;
	char topic[MQTT_MAX_TOPIC_LEN];
	int topic_sz = MQTT_MAX_TOPIC_LEN;

	if (!ctx.connected) {
		return -ENOTCONN;
	}

	switch (cmd_type) {
	case AT_PARSER_CMD_TYPE_SET:
		if (param_count == 2) {
			err = util_string_get(parser, 1, topic, &topic_sz);
			if (err < 0) {
				return err;
			}
			err = do_mqtt_subscribe(AT_MQTTSUB_UNSUB,
						topic, topic_sz, 0);
		} else {
			return -EINVAL;
		}
		break;

	case AT_PARSER_CMD_TYPE_TEST:
		rsp_send("\r\n#XMQTTUNSUB: <topic>\r\n");
		err = 0;
		break;

	default:
		break;
	}

	return err;
}

static int sm_at_mqtt_init(void)
{
	memset(&ctx, 0, sizeof(ctx));
	ctx.sec_tag = SEC_TAG_TLS_INVALID;

	strcpy(mqtt_clientid, SM_DEFAULT_CID);
	do_mqtt_config(CONFIG_MQTT_KEEPALIVE, MQTT_CLEAN_SESSION_INIT, SM_MQTT_VERSION_DEFAULT, 0);

	return 0;
}
SYS_INIT(sm_at_mqtt_init, APPLICATION, 0);
