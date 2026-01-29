/*
 * Copyright (c) 2023 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include "sm_ppp.h"
#include "sm_at_host.h"
#include "sm_util.h"
#include "sm_defines.h"
#include "sm_ctrl_pin.h"
#include "sm_cmux.h"
#include "sm_uart_handler.h"
#include <modem/lte_lc.h>
#include <zephyr/modem/ppp.h>
#include <zephyr/net/ethernet.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/ppp.h>
#include <zephyr/posix/sys/eventfd.h>
#include <zephyr/posix/sys/socket.h>
#include <zephyr/random/random.h>
#include <zephyr/pm/device.h>
#include <assert.h>
#include <strings.h>

LOG_MODULE_REGISTER(sm_ppp, CONFIG_SM_LOG_LEVEL);

/* This keeps track of whether the user is registered to the CGEV notifications.
 * We need them to know when to start/stop the PPP link, but that should not
 * influence what the user receives, so we do the filtering based on this.
 */
bool sm_fwd_cgev_notifs;

static struct net_if *ppp_iface;
static bool sm_ppp_auto_start;
static uint8_t ppp_data_buf[1500];
static struct sockaddr_ll ppp_zephyr_dst_addr;

static struct k_thread ppp_data_passing_thread_id;
static K_THREAD_STACK_DEFINE(ppp_data_passing_thread_stack, KB(2));
static void ppp_data_passing_thread(void*, void*, void*);

enum ppp_action {
	PPP_START,
	PPP_RESTART,
	PPP_STOP
};

enum ppp_reason {
	PPP_REASON_CMD,			/**< Request is originated from user command */
	PPP_REASON_NETWORK,		/**< Request is originated from network event */
	PPP_REASON_ERROR,		/**< Request is originated from error condition */
	PPP_REASON_PEER_DISCONNECTED,	/**< Request is originated from peer disconnection */
};

struct ppp_event {
	enum ppp_action action;
	enum ppp_reason reason;
};

struct ppp_work {
	struct k_msgq queue;
	struct ppp_event queue_buf[4];
};
static struct ppp_work ppp_work;

static bool ppp_peer_connected;

enum ppp_states {
	PPP_STATE_STOPPED,
	PPP_STATE_STARTING,
	PPP_STATE_RUNNING,
	PPP_STATE_STOPPING
};
static enum ppp_states ppp_state;

MODEM_PPP_DEFINE(ppp_module, NULL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,
		 sizeof(ppp_data_buf), sizeof(ppp_data_buf));
AT_MONITOR(sm_ppp_on_cgev, "CGEV", at_notif_on_cgev, PAUSED);
static struct modem_pipe *ppp_pipe;

/* Default PPP PDN is the default PDP context (CID 0). */
static unsigned int ppp_pdn_cid;

enum {
	EVENT_FD_IDX,  /* Eventfd to signal incoming PPP events (always valid). */
	ZEPHYR_FD_IDX, /* Raw Zephyr socket to pass data to/from the PPP link. */
	MODEM_FD_IDX,  /* Raw modem socket to pass data to/from the LTE link. */
	PPP_FDS_COUNT
};
const char *const ppp_socket_names[PPP_FDS_COUNT] = {
	[EVENT_FD_IDX] = "event",
	[ZEPHYR_FD_IDX] = "Zephyr",
	[MODEM_FD_IDX] = "modem"
};
static int ppp_fds[PPP_FDS_COUNT] = { -1, -1, -1 };

static const char *ppp_action_str(enum ppp_action action)
{
	switch (action) {
	case PPP_START:
		return "start";
	case PPP_RESTART:
		return "restart";
	case PPP_STOP:
		return "stop";
	}

	return "";
}

void sm_ppp_set_auto_start(bool enable)
{
	sm_ppp_auto_start = enable;
}

static bool open_ppp_sockets(void)
{
	int ret;

	ppp_fds[ZEPHYR_FD_IDX] = zsock_socket(AF_PACKET, SOCK_DGRAM | SOCK_NATIVE,
					      htons(ETH_P_ALL));
	if (ppp_fds[ZEPHYR_FD_IDX] < 0) {
		LOG_ERR("Zephyr socket creation failed (%d).", -errno);
		return false;
	}

	ppp_zephyr_dst_addr = (struct sockaddr_ll){
		.sll_family = AF_PACKET,
		.sll_ifindex = net_if_get_by_iface(ppp_iface),
		.sll_protocol = htons(ETH_P_ALL),
	};
	ret = zsock_bind(ppp_fds[ZEPHYR_FD_IDX],
		   (const struct sockaddr *)&ppp_zephyr_dst_addr, sizeof(ppp_zephyr_dst_addr));
	if (ret < 0) {
		LOG_ERR("Failed to bind Zephyr socket (%d).", -errno);
		return false;
	}

	ppp_fds[MODEM_FD_IDX] = zsock_socket(AF_PACKET, SOCK_RAW, 0);
	if (ppp_fds[MODEM_FD_IDX] < 0) {
		LOG_ERR("Modem socket creation failed (%d).", -errno);
		return false;
	}

	/* Bind PPP to PDN */
	int pdn_id = sm_util_pdn_id_get(ppp_pdn_cid);

	if (pdn_id < 0) {
		return false;
	}

	ret = zsock_setsockopt(
		ppp_fds[MODEM_FD_IDX],
		SOL_SOCKET, SO_BINDTOPDN,
		&pdn_id, sizeof(int));
	if (ret == 0) {
		LOG_INF("PPP socket bound to PDN ID %d", pdn_id);
	} else {
		LOG_ERR("Failed to bind PPP to PDN ID %d (%d)", pdn_id, -errno);
		return false;
	}

	return true;
}

static void close_ppp_sockets(void)
{
	/* Close only PPP sockets (Zephyr and modem), not the event FD */
	for (size_t i = EVENT_FD_IDX + 1; i < ARRAY_SIZE(ppp_fds); ++i) {
		if (ppp_fds[i] < 0) {
			continue;
		}
		if (zsock_close(ppp_fds[i])) {
			LOG_WRN("Failed to close %s socket (%d).",
				ppp_socket_names[i], -errno);
		}
		ppp_fds[i] = -1;
	}
}

static bool configure_ppp_link_ip_addresses(struct ppp_context *ctx)
{
	static uint8_t ppp_ll_addr[PPP_INTERFACE_IDENTIFIER_LEN];
	uint8_t ll_addr_len;
	char addr4[INET_ADDRSTRLEN];
	char addr6[INET6_ADDRSTRLEN];

	util_get_ip_addr(ppp_pdn_cid, addr4, addr6);

	if (*addr4) {
		if (zsock_inet_pton(AF_INET, addr4, &ctx->ipcp.my_options.address) != 1) {
			return false;
		}
	} else if (!*addr6) {
		LOG_ERR("No connectivity.");
		return false;
	}

	if (*addr6) {
		struct in6_addr in6;

		if (zsock_inet_pton(AF_INET6, addr6, &in6) != 1) {
			return false;
		}
		/* The interface identifier is the last 64 bits of the IPv6 address. */
		BUILD_ASSERT(sizeof(in6) >= sizeof(ppp_ll_addr));
		ll_addr_len = sizeof(ppp_ll_addr);
		memcpy(ppp_ll_addr, (uint8_t *)(&in6 + 1) - ll_addr_len, ll_addr_len);
	} else {
		/* 00-00-5E-00-53-xx as per RFC 7042, as zephyr/drivers/net/ppp.c does. */
		ll_addr_len = 6;
		ppp_ll_addr[0] = 0x00;
		ppp_ll_addr[1] = 0x00;
		ppp_ll_addr[2] = 0x5E;
		ppp_ll_addr[3] = 0x00;
		ppp_ll_addr[4] = 0x53;
		ppp_ll_addr[5] = sys_rand32_get();
	}
	net_if_set_link_addr(ppp_iface, ppp_ll_addr, ll_addr_len, NET_LINK_UNKNOWN);

	return true;
}

static void delegate_ppp_event(enum ppp_action action, enum ppp_reason reason)
{
	struct ppp_event event = {.action = action, .reason = reason};

	LOG_DBG("PPP %s, reason: %d", ppp_action_str(event.action), event.reason);

	if (k_msgq_put(&ppp_work.queue, &event, K_NO_WAIT)) {
		LOG_ERR("Failed to queue PPP event.");
		return;
	}

	/* Signal the PPP thread that an event is available */
	if (eventfd_write(ppp_fds[EVENT_FD_IDX], 1) != 0) {
		LOG_ERR("Failed to signal PPP event (%d).", errno);
	}
}

static bool ppp_is_running(void)
{
	return (ppp_state == PPP_STATE_RUNNING);
}

static void send_status_notification(void)
{
	rsp_send("\r\n#XPPP: %u,%u,%u\r\n", !sm_ppp_is_stopped(), ppp_peer_connected, ppp_pdn_cid);
}

static void ppp_start_failure(void)
{
	close_ppp_sockets();
	net_if_down(ppp_iface);
}

static void ppp_retrieve_pdn_info(struct ppp_context *const ctx)
{
	struct sm_pdn_dynamic_info populated_info = {0};
	unsigned int mtu = CONFIG_SM_PPP_FALLBACK_MTU;

	if (!sm_util_pdn_dynamic_info_get(ppp_pdn_cid, &populated_info)) {
		if (populated_info.ipv6_mtu) {
			/* Set the PPP MTU to that of the LTE link. */
			/* IPv6's MTU has more priority on dual-stack.
			 * Because, it must be at least 1280 for IPv6,
			 * while MTU of IPv4 may be less.
			 */
			mtu = MIN(populated_info.ipv6_mtu, sizeof(ppp_data_buf));
		} else if (populated_info.ipv4_mtu) {
			/* Set the PPP MTU to that of the LTE link. */
			mtu = MIN(populated_info.ipv4_mtu, sizeof(ppp_data_buf));
		}

		/* Try to populate DNS addresses from PDN */
		if (populated_info.dns_addr4_primary.s_addr != INADDR_ANY) {
			/* Populate both My address and peer options
			 * as these are wrong way in Zephyr (it offers my_option DNS)
			 */
			ctx->ipcp.peer_options.dns1_address = populated_info.dns_addr4_primary;
			ctx->ipcp.peer_options.dns2_address = populated_info.dns_addr4_secondary;
			ctx->ipcp.my_options.dns1_address = populated_info.dns_addr4_primary;
			ctx->ipcp.my_options.dns2_address = populated_info.dns_addr4_secondary;
#if defined(CONFIG_LTE_LC_DNS_FALLBACK_ADDRESS)
		} else {
			/* Use fallback DNS addresses from LTE_LC module */
			(void)nrf_inet_pton(NRF_AF_INET, CONFIG_LTE_LC_DNS_FALLBACK_ADDRESS,
					    &ctx->ipcp.peer_options.dns1_address);
			ctx->ipcp.my_options.dns1_address = ctx->ipcp.peer_options.dns1_address;
		}
#elif defined(CONFIG_DNS_SERVER1)
		} else {
			/* Use fallback DNS addresses from Zephyr */
			(void)nrf_inet_pton(NRF_AF_INET, CONFIG_DNS_SERVER1,
					    &ctx->ipcp.peer_options.dns1_address);
			ctx->ipcp.my_options.dns1_address = ctx->ipcp.peer_options.dns1_address;
		}
#else
		} else {
			LOG_WRN("No DNS addresses available on PDN and no fallback configured.");
		}
#endif
	} else {
		LOG_DBG("Could not retrieve MTU, using fallback value.");
		BUILD_ASSERT(sizeof(ppp_data_buf) >= CONFIG_SM_PPP_FALLBACK_MTU);
	}
	net_if_set_mtu(ppp_iface, mtu);
	LOG_DBG("MTU set to %u.", mtu);
}

static int drop_at_write(const uint8_t *data, size_t len, bool urc)
{
	ARG_UNUSED(urc);
	LOG_DBG("Drop AT: '%.*s'", len, data);

	/* Drop all data */
	return len;
}

static int ppp_start(void)
{
	int ret;

	if (ppp_state == PPP_STATE_RUNNING) {
		LOG_INF("PPP already running");
		send_status_notification();
		return 0;
	}
	at_monitor_resume(&sm_ppp_on_cgev);

	struct ppp_context *const ctx = net_if_l2_data(ppp_iface);

	if (!configure_ppp_link_ip_addresses(ctx)) {
		ret = -EADDRNOTAVAIL;
		goto error;
	}

	ppp_state = PPP_STATE_STARTING;
	ppp_retrieve_pdn_info(ctx);

	ret = net_if_up(ppp_iface);
	if (ret) {
		LOG_ERR("Failed to bring PPP interface up (%d).", ret);
		goto error;
	}

	if (!open_ppp_sockets()) {
		ppp_start_failure();
		ret = -ENOTCONN;
		goto error;
	}

	send_status_notification();

	if (sm_cmux_is_started()) {
		ppp_pipe = sm_cmux_reserve(CMUX_PPP_CHANNEL);
		modem_ppp_attach(&ppp_module, ppp_pipe);
		/* The pipe opening is managed by CMUX. */
	} else {
		/* Wait for TX buffer to drain */
		k_sleep(K_MSEC(10));
		ppp_pipe = sm_uart_pipe_init(&drop_at_write);
		if (!ppp_pipe) {
			ret = -ENOSYS;
			goto error;
		}

		modem_ppp_attach(&ppp_module, ppp_pipe);
		ret = modem_pipe_open(ppp_pipe, K_SECONDS(CONFIG_SM_MODEM_PIPE_TIMEOUT));
		if (ret) {
			LOG_ERR("Failed to open PPP pipe (%d).", ret);
			ppp_start_failure();
			goto error;
		}
	}

	net_if_carrier_on(ppp_iface);

	ppp_state = PPP_STATE_RUNNING;

	return 0;

error:
	ppp_state = PPP_STATE_STOPPED;

	if (!sm_cmux_is_started() && ppp_pipe) {
		modem_pipe_close(ppp_pipe, K_SECONDS(CONFIG_SM_MODEM_PIPE_TIMEOUT));
		modem_ppp_release(&ppp_module);
		sm_uart_handler_enable();
	}
	ppp_pipe = NULL;
	return ret;
}

bool sm_ppp_is_stopped(void)
{
	return (ppp_state == PPP_STATE_STOPPED);
}

static int ppp_stop(enum ppp_reason reason)
{
	if (ppp_state == PPP_STATE_STOPPED) {
		LOG_INF("PPP already stopped");
		return 0;
	}
	ppp_state = PPP_STATE_STOPPING;

	/* When CMUX is used, PPP connection may recover on the same pipe,
	 * on other cases, it will be closed and pipe is returned to AT mode.
	 */
	if (reason == PPP_REASON_PEER_DISCONNECTED || reason == PPP_REASON_CMD ||
	    !sm_cmux_is_started()) {
		at_monitor_pause(&sm_ppp_on_cgev);
	}

	/* Bring the interface down before releasing pipes and carrier.
	 * This is needed for LCP to notify the remote endpoint that the link is going down.
	 */
	int ret = net_if_down(ppp_iface);

	if (ret) {
		LOG_WRN("Failed to bring PPP interface down (%d).", ret);
	}

	modem_ppp_release(&ppp_module);

	if (sm_cmux_is_started()) {
		sm_cmux_release(CMUX_PPP_CHANNEL);
	} else {
		modem_pipe_close(ppp_pipe, K_SECONDS(CONFIG_SM_MODEM_PIPE_TIMEOUT));
		ret = sm_uart_handler_enable();

		if (ret) {
			LOG_ERR("Failed to enable PPP UART handler (%d).", ret);
		}
		LOG_INF("Returned to AT command mode.");
	}

	net_if_carrier_off(ppp_iface);

	close_ppp_sockets();

	ppp_state = PPP_STATE_STOPPED;
	ppp_pipe = NULL;
	send_status_notification();

	return 0;
}

/* We need to receive CGEV notifications at all times.
 * CGEREP AT commands are intercepted to prevent the user
 * from unsubcribing us and make that behavior invisible.
 */
AT_CMD_CUSTOM(at_cgerep_interceptor, "AT+CGEREP", at_cgerep_callback);

static int at_cgerep_callback(char *buf, size_t len, char *at_cmd)
{
	int ret;
	unsigned int subscribe = 0;
	const bool set_cmd = (sscanf(at_cmd, "%*[^=]=%u", &subscribe) == 1);

	/* The modem interprets AT+CGEREP and AT+CGEREP= as AT+CGEREP=0.
	 * Prevent those forms, only allowing AT+CGEREP=0, for simplicty.
	 */
	if (!set_cmd && (!strcasecmp(at_cmd, "AT+CGEREP") || !strcasecmp(at_cmd, "AT+CGEREP="))) {
		LOG_ERR("The syntax %s is disallowed. Use AT+CGEREP=0 instead.", at_cmd);
		return -EINVAL;
	}
	if (!set_cmd || subscribe) {
		/* Forward the command to the modem only if not unsubscribing. */
		ret = sm_util_at_cmd_no_intercept(buf, len, at_cmd);
		if (ret) {
			return ret;
		}
		/* Modify the output of the read command to reflect the user's
		 * subscription status, not that of the Serial Modem.
		 */
		if (at_cmd[strlen("AT+CGEREP")] == '?') {
			const size_t mode_idx = strlen("+CGEREP: ");

			if (mode_idx < len) {
				/* +CGEREP: <mode>,<bfr> */
				buf[mode_idx] = '0' + sm_fwd_cgev_notifs;
			}
		}
	} else { /* AT+CGEREP=0 */
		snprintf(buf, len, "%s", "OK\r\n");
	}

	if (set_cmd) {
		sm_fwd_cgev_notifs = subscribe;
	}
	return 0;
}

static void subscribe_cgev_notifications(void)
{
	char buf[sizeof("\r\nOK")];

	/* Bypass the CGEREP interception above as it is meant for commands received externally. */
	const int ret = sm_util_at_cmd_no_intercept(buf, sizeof(buf), "AT+CGEREP=1");

	if (ret) {
		LOG_ERR("Failed to subscribe to +CGEV notifications (%d).", ret);
	}
}

static void at_notif_on_cgev(const char *notify)
{
	char *str;
	char *endptr;
	uint8_t cid;
	char cgev_pdn_act[] = "+CGEV: ME PDN ACT";

	if (!sm_ppp_auto_start) {
		/* Auto-start disabled, ignore all notifications */
		return;
	}

	/* +2 for space and a number */
	if (strlen(cgev_pdn_act) + 2 > strlen(notify)) {
		/* Ignore notifications that are not long enough to be what we are interested in */
		return;
	}

	/* Only activation of PPP PDN is monitored here.
	 * Deactivation of PPP PDN or detach from network will cause PPP socket to get closed
	 * from where stopping of PPP is triggered.
	 */
	str = strstr(notify, cgev_pdn_act);
	if (str != NULL) {
		str += strlen(cgev_pdn_act);
		if (*str == ' ') {
			str++;
			cid = (uint8_t)strtoul(str, &endptr, 10);
			if (endptr != str && cid == ppp_pdn_cid) {
				LOG_INF("PPP PDN (%d) activated.", ppp_pdn_cid);
				delegate_ppp_event(PPP_START, PPP_REASON_NETWORK);
			}
		}
	}
}

/* Notification subscriptions are reset on CFUN=0.
 * We intercept CFUN set commands to automatically subscribe.
 */
AT_CMD_CUSTOM(at_cfun_set_interceptor, "AT+CFUN=", at_cfun_set_callback);

static int at_cfun_set_callback(char *buf, size_t len, char *at_cmd)
{
	unsigned int mode;
	int ret;

	/* sscanf() doesn't match if this is a test command (it also gets intercepted). */
	if (sscanf(at_cmd, "%*[^=]=%u", &mode) == 1) {
		if (mode == LTE_LC_FUNC_MODE_NORMAL || mode == LTE_LC_FUNC_MODE_ACTIVATE_LTE) {
			subscribe_cgev_notifications();
		} else if (mode == LTE_LC_FUNC_MODE_POWER_OFF) {
			/* Unsubscribe the user as would normally happen. */
			sm_fwd_cgev_notifs = false;
		}
	}

	/* Forward AT+CFUN command to the modem. */
	ret = sm_util_at_cmd_no_intercept(buf, len, at_cmd);
	if (ret) {
		return ret;
	}
	return 0;
}

static void ppp_work_fn(void)
{
	struct ppp_event event;
	int err = 0;

	while (k_msgq_get(&ppp_work.queue, &event, K_NO_WAIT) == 0) {

		LOG_INF("PPP %s, reason: %d", ppp_action_str(event.action), event.reason);

		switch (event.action) {
		case PPP_START:
			err = ppp_start();
			break;
		case PPP_RESTART:
			err = ppp_stop(event.reason);
			if (err) {
				break;
			}
			err = ppp_start();
			break;
		case PPP_STOP:
			err = ppp_stop(event.reason);
			break;
		default:
			LOG_ERR("Unknown PPP action: %d.", event.action);
			break;
		}

		LOG_INF("PPP %s %s.", ppp_action_str(event.action),
			(err ? "failed" : "succeeded"));
	}
}

static void ppp_net_mgmt_event_handler(uint64_t mgmt_event, struct net_if *iface, void *info,
				       size_t info_length, void *user_data)
{
	switch (mgmt_event) {
	case NET_EVENT_PPP_PHASE_RUNNING:
		LOG_INF("Peer connected.");
		ppp_peer_connected = true;
		send_status_notification();
		break;
	case NET_EVENT_PPP_PHASE_DEAD:
		LOG_DBG("Peer not connected.");
		/* This event can come without prior NET_EVENT_PPP_PHASE_RUNNING. */
		if (!ppp_peer_connected) {
			break;
		}
		ppp_peer_connected = false;
		/* Also ignore this event when PPP is not running anymore. */
		if (!ppp_is_running()) {
			break;
		}
		send_status_notification();

		LOG_INF("Peer disconnected. %s PPP...", "Stopping");
		delegate_ppp_event(PPP_STOP, PPP_REASON_PEER_DISCONNECTED);

		break;
	default:
		break;
	}
}
NET_MGMT_REGISTER_EVENT_HANDLER(sm_ppp_event_handler,
				NET_EVENT_PPP_PHASE_RUNNING | NET_EVENT_PPP_PHASE_DEAD,
				ppp_net_mgmt_event_handler, NULL);

static int sm_ppp_init(void)
{
	/* Initialize event message queue */
	k_msgq_init(&ppp_work.queue, (char *)&ppp_work.queue_buf, sizeof(struct ppp_event),
		    sizeof(ppp_work.queue_buf) / sizeof(struct ppp_event));

	/* Create event eventfd for signaling events to the PPP thread */
	ppp_fds[EVENT_FD_IDX] = eventfd(0, EFD_NONBLOCK);
	if (ppp_fds[EVENT_FD_IDX] < 0) {
		LOG_ERR("Failed to create event eventfd (%d).", errno);
		sm_init_failed = true;
		return -errno;
	}

	/* Start the PPP thread which will handle events and data passing */
	k_thread_create(&ppp_data_passing_thread_id, ppp_data_passing_thread_stack,
			K_THREAD_STACK_SIZEOF(ppp_data_passing_thread_stack),
			ppp_data_passing_thread, NULL, NULL, NULL,
			K_PRIO_COOP(10), 0, K_NO_WAIT);
	k_thread_name_set(&ppp_data_passing_thread_id, "ppp_data_passing");

	ppp_iface = modem_ppp_get_iface(&ppp_module);

	net_if_flag_set(ppp_iface, NET_IF_POINTOPOINT);

	LOG_DBG("PPP initialized.");
	return 0;
}
SYS_INIT(sm_ppp_init, APPLICATION, 0);

SM_AT_CMD_CUSTOM(xppp, "AT#XPPP", handle_at_ppp);
static int handle_at_ppp(enum at_parser_cmd_type cmd_type, struct at_parser *parser,
			 uint32_t param_count)
{
	int ret;
	unsigned int op;
	enum {
		OP_STOP,
		OP_START,
		OP_COUNT
	};

	if (cmd_type == AT_PARSER_CMD_TYPE_READ) {
		send_status_notification();
		return 0;
	}
	if (cmd_type != AT_PARSER_CMD_TYPE_SET || param_count < 2 || param_count > 3) {
		return -EINVAL;
	}

	ret = at_parser_num_get(parser, 1, &op);
	if (ret) {
		return ret;
	} else if (op >= OP_COUNT) {
		return -EINVAL;
	}

	if (op == OP_STOP && param_count != 2) {
		return -EINVAL;
	}

	/* Send "OK" first in case stopping PPP results in the CMUX AT channel switching. */
	rsp_send_ok();
	if (op == OP_START) {
		sm_ppp_set_auto_start(true);
		ppp_pdn_cid = 0;
		/* Store PPP PDN if given */
		at_parser_num_get(parser, 2, &ppp_pdn_cid);
		delegate_ppp_event(PPP_START, PPP_REASON_CMD);
	} else {
		sm_ppp_set_auto_start(false);
		delegate_ppp_event(PPP_STOP, PPP_REASON_CMD);
	}
	return -SILENT_AT_COMMAND_RET;
}

static void ppp_data_passing_thread(void*, void*, void*)
{
	struct zsock_pollfd fds[PPP_FDS_COUNT];
	size_t mtu = 0;

	while (true) {
		int nfds = 0;

		/* Always poll the event FD for incoming events */
		fds[nfds].fd = ppp_fds[EVENT_FD_IDX];
		fds[nfds].events = ZSOCK_POLLIN;
		nfds++;

		/* When PPP is running, also poll the PPP data sockets */
		if (ppp_is_running()) {
			if (mtu == 0) {
				mtu = net_if_get_mtu(ppp_iface);
			}

			fds[nfds].fd = ppp_fds[ZEPHYR_FD_IDX];
			fds[nfds].events = ZSOCK_POLLIN;
			nfds++;

			fds[nfds].fd = ppp_fds[MODEM_FD_IDX];
			fds[nfds].events = ZSOCK_POLLIN;
			nfds++;
		} else {
			mtu = 0;
		}

		const int poll_ret = zsock_poll(fds, nfds, -1);

		if (poll_ret <= 0) {
			LOG_ERR("Sockets polling failed (%d, %d).", poll_ret, -errno);
			if (ppp_is_running()) {
				ppp_state = PPP_STATE_STARTING;
				delegate_ppp_event(PPP_RESTART, PPP_REASON_ERROR);
			}
			k_sleep(K_SECONDS(1));
			continue;
		}

		for (int i = 0; i < nfds; ++i) {
			const short revents = fds[i].revents;
			const int fd = fds[i].fd;

			if (!revents) {
				continue;
			}

			/* Check if this is the event FD */
			if (fd == ppp_fds[EVENT_FD_IDX]) {
				if (revents & ZSOCK_POLLIN) {
					eventfd_t value;
					/* Read the eventfd to clear it */
					if (eventfd_read(ppp_fds[EVENT_FD_IDX], &value) == 0) {
						LOG_DBG("Processing PPP events.");
						/* Process all queued events */
						ppp_work_fn();
					} else {
						LOG_ERR("Failed to read eventfd (%d).", errno);
					}
				}
				continue;
			}

			/* Determine the source index for PPP data sockets */
			size_t src;

			if (fd == ppp_fds[ZEPHYR_FD_IDX]) {
				src = ZEPHYR_FD_IDX;
			} else if (fd == ppp_fds[MODEM_FD_IDX]) {
				src = MODEM_FD_IDX;
			} else {
				continue;
			}

			if (!(revents & ZSOCK_POLLIN)) {
				/* ZSOCK_POLLERR comes when the connection goes down (AT+CFUN=0). */
				if (revents ^ ZSOCK_POLLERR) {
					LOG_WRN("Unexpected event 0x%x on %s socket. Stop.",
						revents, ppp_socket_names[src]);
				} else {
					LOG_DBG("Connection down. Stop.");
				}
				delegate_ppp_event(PPP_STOP, PPP_REASON_NETWORK);
				continue;
			}

			/* Networks can send packets larger than the MTU, so use the buffer size. */
			const ssize_t len = zsock_recv(fds[src].fd, ppp_data_buf,
						       sizeof(ppp_data_buf), ZSOCK_MSG_DONTWAIT);

			if (len <= 0) {
				if (len != -1 || (errno != EAGAIN && errno != EWOULDBLOCK)) {
					LOG_ERR("Failed to receive data from %s socket (%d, %d).",
						ppp_socket_names[src], len, -errno);
				}
				continue;
			}
			ssize_t send_ret;
			const size_t dst = (src == ZEPHYR_FD_IDX) ? MODEM_FD_IDX : ZEPHYR_FD_IDX;
			void *dst_addr = (dst == MODEM_FD_IDX) ? NULL : &ppp_zephyr_dst_addr;
			socklen_t addrlen = (dst == MODEM_FD_IDX) ? 0 : sizeof(ppp_zephyr_dst_addr);

			if (dst == ZEPHYR_FD_IDX) {
				uint8_t type = ppp_data_buf[0] & 0xf0;

				if (type == 0x60) {
					ppp_zephyr_dst_addr.sll_protocol = htons(ETH_P_IPV6);
				} else if (type == 0x40) {
					ppp_zephyr_dst_addr.sll_protocol = htons(ETH_P_IP);
				} else {
					/* Not IP traffic, ignore. */
					continue;
				}
			}

			send_ret =
				zsock_sendto(fds[dst].fd, ppp_data_buf, len, 0, dst_addr, addrlen);
			if (send_ret == -1) {
				LOG_ERR("Failed to send %zd bytes to %s socket (%d).",
					len, ppp_socket_names[dst], -errno);
			} else if (send_ret != len) {
				LOG_ERR("Only sent %zd out of %zd bytes to %s socket.",
					send_ret, len, ppp_socket_names[dst]);
			} else {
				LOG_DBG("Forwarded %zd bytes to %s socket.",
					send_ret, ppp_socket_names[dst]);
			}
		}
	}
}
