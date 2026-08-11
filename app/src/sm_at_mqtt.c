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
} ctx;

static uint16_t mqtt_broker_port;
static char mqtt_clientid[MQTT_MAX_CID_LEN + 1];

struct mqtt_conn {
	uint8_t rx[CONFIG_SM_MQTTC_MESSAGE_BUFFER_LEN];
	uint8_t tx[CONFIG_SM_MQTTC_MESSAGE_BUFFER_LEN];
	uint8_t payload_drain[MQTT_MAX_TOPIC_LEN];
	uint8_t pub_topic[MQTT_MAX_TOPIC_LEN];
	char broker_url[SM_MAX_DNS_LEN + 1];
	char username[SM_MAX_USERNAME + 1];
	char password[SM_MAX_PASSWORD + 1];
	struct mqtt_publish_param pub_param;
};
static struct mqtt_conn *mqtt_conn;

/* The mqtt client struct */
static struct mqtt_client client;

/**@brief Function to handle received publish event.
 */
static void handle_mqtt_publish_evt(struct mqtt_client *const c, const struct mqtt_evt *evt)
{
	/* Send QoS acknowledgments */
	if (evt->param.publish.message.topic.qos == MQTT_QOS_1_AT_LEAST_ONCE) {
		const struct mqtt_puback_param ack = {
			.message_id = evt->param.publish.message_id
		};

		mqtt_publish_qos1_ack(&client, &ack);
	} else if (evt->param.publish.message.topic.qos == MQTT_QOS_2_EXACTLY_ONCE) {
		const struct mqtt_pubrec_param ack = {
			.message_id = evt->param.publish.message_id
		};

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

/**@brief MQTT client event handler
 */
void mqtt_evt_handler(struct mqtt_client *const c, const struct mqtt_evt *evt)
{
	int ret;

	ret = evt->result;
	switch (evt->type) {
	case MQTT_EVT_CONNACK:
		if (evt->result != 0) {
			ctx.connected = false;
		}
		break;

	case MQTT_EVT_DISCONNECT:
		ctx.connected = false;
		break;

	case MQTT_EVT_PUBLISH:
		handle_mqtt_publish_evt(c, evt);
		break;

	case MQTT_EVT_PUBACK:
		if (evt->result == 0) {
			LOG_DBG("PUBACK packet id: %u", evt->param.puback.message_id);
		}
		break;

	case MQTT_EVT_PUBREC:
		if (evt->result != 0) {
			break;
		}
		LOG_DBG("PUBREC packet id: %u", evt->param.pubrec.message_id);
		{
			struct mqtt_pubrel_param param = {
				.message_id = evt->param.pubrel.message_id
			};
			ret = mqtt_publish_qos2_release(&client, &param);
			if (ret) {
				LOG_ERR("mqtt_publish_qos2_release: Fail! %d", ret);
			} else {
				LOG_DBG("Release, id %u", evt->param.pubrec.message_id);
			}
		}
		break;

	case MQTT_EVT_PUBREL:
		if (evt->result != 0) {
			break;
		}
		LOG_DBG("PUBREL packet id %u", evt->param.pubrel.message_id);
		{
			struct mqtt_pubcomp_param param = {
				.message_id = evt->param.pubrel.message_id
			};
			ret = mqtt_publish_qos2_complete(&client, &param);
			if (ret) {
				LOG_ERR("mqtt_publish_qos2_complete Failed:%d", ret);
			} else {
				LOG_DBG("Complete, id %u", evt->param.pubrel.message_id);
			}
		}
		break;

	case MQTT_EVT_PUBCOMP:
		if (evt->result == 0) {
			LOG_DBG("PUBCOMP packet id %u", evt->param.pubcomp.message_id);
		}
		break;

	case MQTT_EVT_SUBACK:
		if (evt->result == 0) {
			LOG_DBG("SUBACK packet id: %u", evt->param.suback.message_id);
		}
		break;

	case MQTT_EVT_UNSUBACK:
		if (evt->result == 0) {
			LOG_DBG("UNSUBACK packet id: %u", evt->param.unsuback.message_id);
		}
		break;

	case MQTT_EVT_PINGRESP:
		if (evt->result == 0) {
			LOG_DBG("PINGRESP packet");
		}
		break;

	default:
		LOG_DBG("default: %d", evt->type);
		break;
	}

	/* #XMQTTEVT for MQTT_EVT_PUBLISH is sent after the payload in mqtt_poll_work_handler.
	 * It cannot be buffered here via urc_send_to: sm_at_host_lock() flushes pending URCs,
	 * which would place it ahead of the #XMQTTMSG header and payload.
	 */
	if (evt->type != MQTT_EVT_PUBLISH) {
		urc_send_to(ctx.pipe, "\r\n#XMQTTEVT: %d,%d\r\n", evt->type, ret);
	}
}

static void mqtt_poll_work_handler(struct k_work *work);
static void mqtt_keepalive_work_handler(struct k_work *work);
static void mqtt_poll_cb(const struct socket_ncs_pollcb_params *params);

static void mqtt_conn_release(void)
{
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

static void mqtt_connection_abort(int err)
{
	LOG_ERR("Abort MQTT connection (error %d)", err);
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
			if (err != 0) {
				LOG_ERR("ERROR: mqtt_input %d", err);
				goto abort;
			}
		}

		if (ctx.rx_topic_len > 0 || ctx.publish_payload_remaining > 0) {
			if (ctx.rx_topic_len > 0) {
				sm_at_host_lock(ctx.pipe);
				/* Use rsp_send_to (immediate) so the header precedes the
				 * payload; urc_send_to buffers and would trail the data.
				 */
				rsp_send_to(ctx.pipe, "\r\n#XMQTTMSG: %zu,%zu\r\n",
					    ctx.rx_topic_len, ctx.publish_payload_remaining);
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
		k_work_reschedule_for_queue(&sm_work_q, &mqtt_keepalive_work,
					    K_MSEC(mqtt_keepalive_time_left(&client)));
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

	if (!ctx.connected) {
		return;
	}

	/* MQTT v3.1.1: If a Client does not receive a PINGRESP Packet within a
	 * reasonable amount of time after it has sent a PINGREQ, it SHOULD close
	 * the Network Connection to the Server.
	 */
	if (client.unacked_ping > 1) {
		LOG_ERR("ERROR: mqtt_ping nack %d", client.unacked_ping);
		mqtt_connection_abort(-ENETRESET);
		return;
	}

	int err = mqtt_live(&client);

	if (err != 0 && err != -EAGAIN) {
		LOG_ERR("ERROR: mqtt_live %d", err);
		mqtt_connection_abort(err);
		return;
	}

	k_work_reschedule_for_queue(&sm_work_q, &mqtt_keepalive_work,
				    K_MSEC(mqtt_keepalive_time_left(&client)));
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

/**@brief Configure the MQTT client structure
 */
static int do_mqtt_config(uint16_t keep_alive, uint8_t clean_session)
{
	if (ctx.connected) {
		return -EINVAL;
	}
	if (clean_session != 0 && clean_session != 1) {
		return -EINVAL;
	}

	/* Init MQTT client */
	mqtt_client_init(&client);

	client.evt_cb = mqtt_evt_handler;

	/* MQTT client id configuration */
	client.client_id.utf8 = mqtt_clientid;
	client.client_id.size = strlen(mqtt_clientid);

	/* MQTT buffers are assigned in do_mqtt_connect() once mqtt_conn is allocated. */

	/* MQTT Keep Alive configuration */
	client.keepalive = keep_alive;
	/* MQTT Clean Session configuration */
	client.clean_session = clean_session;

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
	err = mqtt_connect(&client);
	if (err != 0) {
		LOG_ERR("ERROR: mqtt_connect %d", err);
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

	k_work_reschedule_for_queue(&sm_work_q, &mqtt_keepalive_work,
				    K_MSEC(mqtt_keepalive_time_left(&client)));

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
	err = mqtt_disconnect(&client, NULL);
	if (err) {
		LOG_ERR("ERROR: mqtt_disconnect %d", err);
		return err;
	}

	k_work_cancel_delayable(&mqtt_keepalive_work);
	ctx.connected = false;
	client.broker = NULL;
	mqtt_conn_release();

	return 0;
}

static int do_mqtt_publish(uint8_t *msg, size_t msg_len)
{
	if (!ctx.connected || mqtt_conn == NULL) {
		return -ENOTCONN;
	}

	/* MQTT client does not store packets, so we will not retransmit packets
	 * that are lacking response. This deviates from MQTT v3.1.1.
	 */
	mqtt_conn->pub_param.message.payload.data = msg;
	mqtt_conn->pub_param.message.payload.len  = msg_len;

	return mqtt_publish(&client, &mqtt_conn->pub_param);
}

static int do_mqtt_subscribe(uint16_t op,
				uint8_t *topic_buf,
				size_t topic_len,
				uint16_t qos)
{
	int err = -EINVAL;
	struct mqtt_topic subscribe_topic;
	static uint16_t sub_message_id;

	sub_message_id++;
	if (sub_message_id == UINT16_MAX) {
		sub_message_id = 1;
	}

	const struct mqtt_subscription_list subscription_list = {
		.list = &subscribe_topic,
		.list_count = 1,
		.message_id = sub_message_id
	};

	if (qos <= MQTT_QOS_2_EXACTLY_ONCE) {
		subscribe_topic.qos = (uint8_t)qos;
	} else {
		return err;
	}
	subscribe_topic.topic.utf8 = topic_buf;
	subscribe_topic.topic.size = topic_len;

	if (op == 1) {
		err = mqtt_subscribe(&client, &subscription_list);
	} else if (op == 0) {
		err = mqtt_unsubscribe(&client, &subscription_list);
	}

	return err;
}

SM_AT_CMD_CUSTOM(xmqttcfg, "AT#XMQTTCFG", handle_at_mqtt_config);
STATIC int handle_at_mqtt_config(enum at_parser_cmd_type cmd_type, struct at_parser *parser,
				 uint32_t param_count)
{
	int err = -EINVAL;
	uint16_t keep_alive = CONFIG_MQTT_KEEPALIVE;
	uint16_t clean_session = CONFIG_MQTT_CLEAN_SESSION;

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
		err = do_mqtt_config(keep_alive, (uint8_t)clean_session);
		break;

	case AT_PARSER_CMD_TYPE_READ:
		rsp_send("\r\n#XMQTTCFG: \"%s\",%d,%d\r\n",
			 mqtt_clientid, client.keepalive, client.clean_session);
		err = 0;
		break;

	case AT_PARSER_CMD_TYPE_TEST:
		rsp_send("\r\n#XMQTTCFG: <client_id>,<keep_alive>,<clean_session>\r\n");
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

			mqtt_conn = calloc(1, sizeof(*mqtt_conn));
			if (!mqtt_conn) {
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

	} else if (op == DATAMODE_EXIT) {
		LOG_DBG("MQTT data mode exit");
	}

	return 0;
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

		/* common publish parameters*/
		if (qos <= MQTT_QOS_2_EXACTLY_ONCE) {
			mqtt_conn->pub_param.message.topic.qos = (uint8_t)qos;
		} else {
			return -EINVAL;
		}
		if (retain <= 1) {
			mqtt_conn->pub_param.retain_flag = (uint8_t)retain;
		} else {
			return -EINVAL;
		}
		mqtt_conn->pub_param.message.topic.topic.utf8 = mqtt_conn->pub_topic;
		mqtt_conn->pub_param.message.topic.topic.size = topic_sz;
		mqtt_conn->pub_param.dup_flag = 0;
		mqtt_conn->pub_param.message_id++;
		if (mqtt_conn->pub_param.message_id == UINT16_MAX) {
			mqtt_conn->pub_param.message_id = 1;
		}
		if (pub_msg_ptr == NULL || msg_sz == 0) {
			/* Publish payload in data mode */
			err = enter_datamode(mqtt_datamode_callback, data_len);
		} else {
			err = do_mqtt_publish((uint8_t *)pub_msg_ptr, msg_sz);
		}
		break;

	case AT_PARSER_CMD_TYPE_TEST:
		rsp_send("\r\n#XMQTTPUB: <topic>,<msg>,(0,1,2),(0,1)[,<data_len>]\r\n");
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
	do_mqtt_config(CONFIG_MQTT_KEEPALIVE, CONFIG_MQTT_CLEAN_SESSION);

	return 0;
}
SYS_INIT(sm_at_mqtt_init, APPLICATION, 0);
