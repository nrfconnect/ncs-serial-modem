/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <string.h>
#include <zephyr/logging/log.h>
#include <modem/at_parser.h>
#include <modem/lte_lc.h>
#include <nrf_softsim.h>
#include "sm_util.h"
#include "sm_at_host.h"

LOG_MODULE_REGISTER(sm_softsim, CONFIG_SM_LOG_LEVEL);

/* Headroom over the full SoftSIM profile (about 410 characters with SMSP, PIN and SMSC). */
#define SOFTSIM_PROFILE_MAX_LEN 512

SM_AT_CMD_CUSTOM(xsoftsimprovision, "AT#XSOFTSIMPROVISION", handle_at_softsimprovision);
STATIC int handle_at_softsimprovision(enum at_parser_cmd_type cmd_type, struct at_parser *parser,
				      uint32_t param_count)
{
	static char profile[SOFTSIM_PROFILE_MAX_LEN + 1];
	size_t len = sizeof(profile);
	int err;

	switch (cmd_type) {
	case AT_PARSER_CMD_TYPE_SET:
		if (param_count != 2) {
			return -EINVAL;
		}
		if (!sm_is_modem_functional_mode(LTE_LC_FUNC_MODE_POWER_OFF)) {
			LOG_ERR("SoftSIM provisioning requires AT+CFUN=0");
			return -EBUSY;
		}
		err = util_string_get(parser, 1, profile, &len);
		if (err) {
			memset(profile, 0, sizeof(profile));
			return err;
		}

		err = nrf_softsim_provision((uint8_t *)profile, len);
		memset(profile, 0, sizeof(profile));
		if (err) {
			LOG_ERR("SoftSIM provisioning failed: %d", err);
			return -EFAULT;
		}
		LOG_INF("SoftSIM provisioned, len=%zu", len);
		return 0;

	case AT_PARSER_CMD_TYPE_READ:
		rsp_send("\r\n#XSOFTSIMPROVISION: %d\r\n", nrf_softsim_check_provisioned());
		return 0;

	case AT_PARSER_CMD_TYPE_TEST:
		rsp_send("\r\n#XSOFTSIMPROVISION: <profile>\r\n");
		return 0;

	default:
		return -EINVAL;
	}
}
