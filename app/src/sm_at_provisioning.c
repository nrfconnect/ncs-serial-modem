/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/logging/log.h>
#include <modem/nrf_modem_lib.h>
#include <net/nrf_provisioning.h>
#include "sm_util.h"
#include "sm_at_host.h"

LOG_MODULE_REGISTER(sm_provisioning, CONFIG_SM_LOG_LEVEL);

enum xnrfprov_status {
	XNRFPROV_DONE = 0,                  /* Provisioning successful or no pending commands */
	XNRFPROV_NEED_LTE_DEACTIVATED = 1,  /* Host action required: deactivate LTE */
	XNRFPROV_NEED_LTE_ACTIVATED = 2,    /* Host action required: activate LTE */
	XNRFPROV_FAILED = -1,               /* Generic failure, retry */
	XNRFPROV_FAILED_NOT_CLAIMED = -2,   /* Device not claimed on nRF Cloud */
	XNRFPROV_FAILED_WRONG_ROOT_CA = -3, /* Wrong root CA certificate */
	XNRFPROV_FAILED_NO_DATETIME = -4,   /* No valid modem/network time */
	XNRFPROV_FAILED_TOO_MANY_CMDS = -5, /* Too many commands for the device to handle */
	XNRFPROV_FATAL_ERROR = -6,          /* Fatal / irrecoverable error */
};

static void xnrfprov_send_status(int status)
{
	urc_send("\r\n#XNRFPROV: %d\r\n", status);
}

static void xnrfprov_log_failed(const char *reason)
{
	LOG_ERR("Provisioning failed: %s", reason);
}

static void nrf_provisioning_callback(const struct nrf_provisioning_callback_data *event)
{
	switch (event->type) {
	case NRF_PROVISIONING_EVENT_START:
		LOG_INF("Provisioning: started");
		break;
	case NRF_PROVISIONING_EVENT_STOP:
		LOG_INF("Provisioning: stopped");
		break;
	case NRF_PROVISIONING_EVENT_NEED_LTE_DEACTIVATED:
		LOG_INF("Provisioning requires device to deactivate network");
		xnrfprov_send_status(XNRFPROV_NEED_LTE_DEACTIVATED);
		break;
	case NRF_PROVISIONING_EVENT_NEED_LTE_ACTIVATED:
		LOG_INF("Provisioning requires device to activate network");
		xnrfprov_send_status(XNRFPROV_NEED_LTE_ACTIVATED);
		break;
	case NRF_PROVISIONING_EVENT_FAILED_TOO_MANY_COMMANDS:
		xnrfprov_log_failed("too many commands for the device to handle");
		xnrfprov_send_status(XNRFPROV_FAILED_TOO_MANY_CMDS);
		break;
	case NRF_PROVISIONING_EVENT_NO_COMMANDS:
		LOG_INF("Provisioning done, no commands received from the server");
		xnrfprov_send_status(XNRFPROV_DONE);
		break;
	case NRF_PROVISIONING_EVENT_FAILED:
		xnrfprov_log_failed("try again...");
		xnrfprov_send_status(XNRFPROV_FAILED);
		break;
	case NRF_PROVISIONING_EVENT_FAILED_DEVICE_NOT_CLAIMED:
		LOG_WRN("Provisioning failed: %s", "device not claimed");
		LOG_WRN("Claim the device using the device's attestation token on "
			"nrfcloud.com");

		if (IS_ENABLED(CONFIG_NRF_PROVISIONING_PROVIDE_ATTESTATION_TOKEN)) {
			LOG_WRN("Attestation token:\r\n\n%.*s.%.*s\r\n", event->token->attest_sz,
				event->token->attest, event->token->cose_sz, event->token->cose);
		}

		xnrfprov_send_status(XNRFPROV_FAILED_NOT_CLAIMED);
		break;
	case NRF_PROVISIONING_EVENT_FAILED_WRONG_ROOT_CA:
		xnrfprov_log_failed("wrong root CA certificate for nRF Cloud");
		xnrfprov_send_status(XNRFPROV_FAILED_WRONG_ROOT_CA);
		break;
	case NRF_PROVISIONING_EVENT_FAILED_NO_VALID_DATETIME:
		xnrfprov_log_failed("no valid modem/network time");
		xnrfprov_send_status(XNRFPROV_FAILED_NO_DATETIME);
		break;
	case NRF_PROVISIONING_EVENT_FATAL_ERROR:
		xnrfprov_log_failed("irrecoverable");
		xnrfprov_send_status(XNRFPROV_FATAL_ERROR);
		break;
	case NRF_PROVISIONING_EVENT_SCHEDULED_PROVISIONING:
		LOG_INF("Provisioning scheduled, next attempt in %lld seconds",
			event->next_attempt_time_seconds);
		urc_send("\r\n#XNRFPROV: %lld\r\n", event->next_attempt_time_seconds);
		break;
	case NRF_PROVISIONING_EVENT_DONE:
		LOG_INF("Provisioning: done");
		xnrfprov_send_status(XNRFPROV_DONE);
		break;
	default:
		LOG_WRN("Unknown event type: %d", event->type);
		break;
	}
}

/* AT#XNRFPROV - Trigger provisioning immediately */
SM_AT_CMD_CUSTOM(xnrfprov, "AT#XNRFPROV", handle_at_provision);
STATIC int handle_at_provision(enum at_parser_cmd_type cmd_type, struct at_parser *parser,
			       uint32_t param_count)
{
	ARG_UNUSED(parser);
	ARG_UNUSED(param_count);

	switch (cmd_type) {
	case AT_PARSER_CMD_TYPE_SET: {
		int ret = nrf_provisioning_trigger_manually();

		if (ret) {
			LOG_ERR("Failed to trigger provisioning: %d", ret);
			return ret;
		}
		return 0;
	}
	case AT_PARSER_CMD_TYPE_TEST:
		rsp_send("\r\n#XNRFPROV\r\n");
		return 0;
	default:
		return -EINVAL;
	}
}

static void sm_provisioning_init(int ret, void *ctx)
{
	int err;

	err = nrf_provisioning_init(nrf_provisioning_callback);
	if (err) {
		LOG_ERR("Failed to initialize provisioning client");
		sm_init_failed = true;
	}
}

NRF_MODEM_LIB_ON_INIT(sm_provisioning_init_hook, sm_provisioning_init, NULL);
