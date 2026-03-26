/*
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/kernel.h>
#include <stdio.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_event.h>
#include <zephyr/net/wifi_mgmt.h>
#include <modem/at_parser.h>
#include "sm_util.h"
#include "sm_at_host.h"

LOG_MODULE_REGISTER(sm_wifi, CONFIG_SM_LOG_LEVEL);

#define WIFI_SCAN_MAX_AP 20
#define WIFI_MAC_ADDR_STR_LEN 17

/* Wi-Fi scan state */
static struct wifi_scan_result scan_results[WIFI_SCAN_MAX_AP];
static uint8_t scan_result_count;
static bool scan_in_progress;

/* Work item to send results as URCs after scan completes */
static struct k_work wifi_scan_result_send_work;

/* Forward declaration */
static int wifi_interface_down(void);

static struct net_if *wifi_iface;
static struct net_mgmt_event_callback wifi_mgmt_cb;

static void wifi_scan_result_handle(struct net_mgmt_event_callback *cb)
{
	const struct wifi_scan_result *entry = (const struct wifi_scan_result *)cb->info;

	if (scan_result_count < WIFI_SCAN_MAX_AP) {
		memcpy(&scan_results[scan_result_count], entry, sizeof(struct wifi_scan_result));
		scan_result_count++;

		LOG_DBG("Scan result #%d: SSID %s, MAC %02x:%02x:%02x:%02x:%02x:%02x, RSSI %d",
			scan_result_count,
			entry->ssid,
			entry->mac[0], entry->mac[1], entry->mac[2],
			entry->mac[3], entry->mac[4], entry->mac[5],
			entry->rssi);
	} else {
		LOG_WRN("Scan result buffer full, dropping AP: %02x:%02x:%02x:%02x:%02x:%02x",
			entry->mac[0], entry->mac[1], entry->mac[2],
			entry->mac[3], entry->mac[4], entry->mac[5]);
	}
}

static void wifi_scan_results_sender(struct k_work *)
{
	/* Send results as URCs */
	if (scan_result_count == 0) {
		rsp_send("\r\n#XWIFISCAN: 0\r\n");
	} else {
		/* Header: total count */
		rsp_send("\r\n#XWIFISCAN: %d\r\n", scan_result_count);

		/* One line per AP */
		for (int i = 0; i < scan_result_count; i++) {
			rsp_send("#XWIFISCAN: %d,\"%02x:%02x:%02x:%02x:%02x:%02x\",%d\r\n",
				i + 1,
				scan_results[i].mac[0], scan_results[i].mac[1],
				scan_results[i].mac[2], scan_results[i].mac[3],
				scan_results[i].mac[4], scan_results[i].mac[5],
				scan_results[i].rssi);
		}
	}

	/* Bring down Wi-Fi interface to save power */
	wifi_interface_down();
}

static void wifi_scan_done_handle(struct net_mgmt_event_callback *cb)
{
	const struct wifi_status *status = (const struct wifi_status *)cb->info;

	if (status->status) {
		LOG_ERR("Wi-Fi scan request failed: %d", status->status);
	} else {
		LOG_INF("Wi-Fi scan completed with %d APs found", scan_result_count);
	}

	scan_in_progress = false;
	k_work_submit_to_queue(&sm_work_q, &wifi_scan_result_send_work);
}

static void wifi_mgmt_event_handler(struct net_mgmt_event_callback *cb,
				    uint64_t mgmt_event,
				    struct net_if *iface)
{
	ARG_UNUSED(iface);

	switch (mgmt_event) {
	case NET_EVENT_WIFI_SCAN_RESULT:
		wifi_scan_result_handle(cb);
		break;
	case NET_EVENT_WIFI_SCAN_DONE:
		wifi_scan_done_handle(cb);
		break;
	default:
		break;
	}
}

static int wifi_interface_up(void)
{
	int ret;

	if (!wifi_iface) {
		LOG_ERR("Wi-Fi interface not available");
		return -ENODEV;
	}

	if (!net_if_is_admin_up(wifi_iface)) {
		LOG_DBG("Bringing up Wi-Fi interface");
		ret = net_if_up(wifi_iface);
		if (ret) {
			LOG_ERR("Cannot bring up Wi-Fi interface: %d", ret);
			return ret;
		}
		/* Give interface time to initialize */
		k_sleep(K_MSEC(100));
	}

	return 0;
}

static int wifi_interface_down(void)
{
	int ret;

	if (!wifi_iface) {
		return 0;
	}

	if (net_if_is_admin_up(wifi_iface)) {
		LOG_DBG("Bringing down Wi-Fi interface");
		ret = net_if_down(wifi_iface);
		if (ret) {
			LOG_ERR("Cannot bring down Wi-Fi interface: %d", ret);
			return ret;
		}
	}

	return 0;
}

SM_AT_CMD_CUSTOM(xwifiscan, "AT#XWIFISCAN", handle_at_wifi_scan);
static int handle_at_wifi_scan(enum at_parser_cmd_type cmd_type,
			       struct at_parser *parser, uint32_t param_count)
{
	int err;
	int timeout_sec = 10; /* Default timeout */

	switch (cmd_type) {
	case AT_PARSER_CMD_TYPE_SET:
		/* Optional timeout parameter */
		if (param_count > 1) {
			err = at_parser_num_get(parser, 1, &timeout_sec);
			if (err || timeout_sec <= 0 || timeout_sec > 60) {
				LOG_ERR("Invalid timeout parameter");
				return -EINVAL;
			}
		}

		if (scan_in_progress) {
			LOG_ERR("Scan already in progress");
			return -EBUSY;
		}

		/* Bring up Wi-Fi interface */
		err = wifi_interface_up();
		if (err) {
			return err;
		}

		/* Reset scan state */
		scan_result_count = 0;
		scan_in_progress = true;

		/* Start Wi-Fi scan */
		LOG_INF("Starting Wi-Fi scan (timeout %d s) - returning OK immediately",
			timeout_sec);
		err = net_mgmt(NET_REQUEST_WIFI_SCAN, wifi_iface, NULL, 0);
		if (err) {
			LOG_ERR("Failed to start Wi-Fi scan: %d", err);
			scan_in_progress = false;
			wifi_interface_down();
			return -EFAULT;
		}

		/* Return OK immediately; results arrive as #XWIFISCAN: URCs
		 * sent by wifi_scan_results_sender() when the scan completes. */
		err = 0;
		break;

	case AT_PARSER_CMD_TYPE_TEST:
		rsp_send("\r\n#XWIFISCAN[=<timeout>]\r\n");
		err = 0;
		break;

	default:
		err = -EINVAL;
		break;
	}

	return err;
}

static int sm_at_wifi_init(void)
{
	const struct device *wifi_dev;
	int err;

	LOG_INF("Initializing Wi-Fi AT commands");

	/* Initialize work item for async result reporting */
	k_work_init(&wifi_scan_result_send_work, wifi_scan_results_sender);

	/* Get Wi-Fi device from chosen node */
	wifi_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_wifi));

	if (!device_is_ready(wifi_dev)) {
		LOG_ERR("Wi-Fi device not ready: %s", wifi_dev->name);
		return -ENODEV;
	}

	wifi_iface = net_if_lookup_by_dev(wifi_dev);
	if (!wifi_iface) {
		LOG_ERR("No Wi-Fi interface found for device: %s", wifi_dev->name);
		return -EFAULT;
	}

	LOG_INF("Found Wi-Fi interface: %s", wifi_dev->name);

	/* Register network management callback */
	net_mgmt_init_event_callback(&wifi_mgmt_cb,
				     wifi_mgmt_event_handler,
				     (NET_EVENT_WIFI_SCAN_RESULT |
				      NET_EVENT_WIFI_SCAN_DONE));
	net_mgmt_add_event_callback(&wifi_mgmt_cb);

	/* Start with interface down to save power */
	err = wifi_interface_down();
	if (err) {
		LOG_WRN("Failed to bring down Wi-Fi interface: %d", err);
	}

	LOG_INF("Wi-Fi AT commands initialized successfully");
	return 0;
}

SYS_INIT(sm_at_wifi_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
