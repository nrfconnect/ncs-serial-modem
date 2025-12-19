/*
 * Copyright (c) 2021 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <zephyr/init.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log_ctrl.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/sys/util.h>
#include <zephyr/types.h>
#include <dfu/dfu_target.h>
#include <modem/at_parser.h>
#include <modem/lte_lc.h>
#include <modem/modem_jwt.h>
#include <modem/nrf_modem_lib.h>
#include "nrf_modem.h"
#include "ncs_version.h"

#include "sm_util.h"
#include "sm_ctrl_pin.h"
#include "sm_settings.h"
#include "sm_at_host.h"
#include "sm_at_fota.h"
#include "sm_version.h"
#include "sm_at_nrfcloud.h"

LOG_MODULE_REGISTER(sm_at, CONFIG_SM_LOG_LEVEL);

/** @brief Shutdown modes. */
enum sleep_modes {
	SLEEP_MODE_INVALID,
	SLEEP_MODE_DEEP,
	SLEEP_MODE_IDLE
};

static void go_sleep_wk(struct k_work *);
static struct {
	struct k_work_delayable work;
	uint32_t mode;
} sleep_control = {
	.work = Z_WORK_DELAYABLE_INITIALIZER(go_sleep_wk),
};

bool verify_datamode_control(uint16_t time_limit, uint16_t *time_limit_min);

bool sm_is_modem_functional_mode(enum lte_lc_func_mode mode)
{
	int cfun;
	int rc = sm_util_at_scanf("AT+CFUN?", "+CFUN: %d", &cfun);

	return (rc == 1 && cfun == mode);
}

int sm_power_off_modem(void)
{
	/* "[...] there may be a delay until modem is disconnected from the network."
	 * https://docs.nordicsemi.com/bundle/ps_nrf9151/page/chapters/pmu/doc/operationmodes/system_off_mode.html
	 * This will return once the modem responds, which means it has actually stopped.
	 * This has been observed to take between 1 and 2 seconds when it is not already stopped.
	 */
	return sm_util_at_printf("AT+CFUN=0");
}

SM_AT_CMD_CUSTOM(xsmver, "AT#XSMVER", handle_at_smver);
STATIC int handle_at_smver(enum at_parser_cmd_type cmd_type, struct at_parser *, uint32_t)
{
	int ret = -EINVAL;

	if (cmd_type == AT_PARSER_CMD_TYPE_SET) {
		if (strlen(CONFIG_SM_CUSTOMER_VERSION) > 0) {
			rsp_send("\r\n#XSMVER: %s,%s,\"%s\"\r\n",
				 STRINGIFY(SM_VERSION), STRINGIFY(NCS_VERSION_STRING),
				 CONFIG_SM_CUSTOMER_VERSION);
		} else {
			rsp_send("\r\n#XSMVER: %s,%s\r\n",
				 STRINGIFY(SM_VERSION), STRINGIFY(NCS_VERSION_STRING));
		}
		ret = 0;
	}

	return ret;
}

static void go_sleep_wk(struct k_work *)
{
	if (sleep_control.mode == SLEEP_MODE_IDLE) {
		if (sm_at_host_power_off() == 0) {
			sm_ctrl_pin_enter_idle();
		} else {
			LOG_ERR("failed to power off UART");
		}
	} else if (sleep_control.mode == SLEEP_MODE_DEEP) {
		sm_ctrl_pin_enter_sleep();
	}
}

SM_AT_CMD_CUSTOM(xsleep, "AT#XSLEEP", handle_at_sleep);
STATIC int handle_at_sleep(enum at_parser_cmd_type cmd_type, struct at_parser *parser,
			   uint32_t)
{
	int ret = -EINVAL;

	if (cmd_type == AT_PARSER_CMD_TYPE_SET) {
		ret = at_parser_num_get(parser, 1, &sleep_control.mode);
		if (ret) {
			return -EINVAL;
		}
		ret = sm_ctrl_pin_ready();
		if (ret) {
			return ret;
		}
		if (sleep_control.mode == SLEEP_MODE_DEEP ||
		    sleep_control.mode == SLEEP_MODE_IDLE) {
			k_work_reschedule_for_queue(&sm_work_q, &sleep_control.work,
						    SM_UART_RESPONSE_DELAY);
		} else {
			ret = -EINVAL;
		}
	} else if (cmd_type == AT_PARSER_CMD_TYPE_TEST) {
		rsp_send("\r\n#XSLEEP: (%d,%d)\r\n", SLEEP_MODE_DEEP, SLEEP_MODE_IDLE);
		ret = 0;
	}

	return ret;
}

void final_call(void (*func)(void))
{
	/* Delegate the final call to a worker so that the "OK" response is properly sent. */
	static struct k_work_delayable worker;

	k_work_init_delayable(&worker, (k_work_handler_t)func);
	k_work_schedule_for_queue(&sm_work_q, &worker, SM_UART_RESPONSE_DELAY);
}

static void sm_shutdown(void)
{
	sm_at_host_uninit();
	sm_power_off_modem();
	LOG_PANIC();
	sm_ctrl_pin_enter_shutdown();
}

SM_AT_CMD_CUSTOM(xshutdown, "AT#XSHUTDOWN", handle_at_shutdown);
STATIC int handle_at_shutdown(enum at_parser_cmd_type cmd_type, struct at_parser *, uint32_t)
{
	if (cmd_type != AT_PARSER_CMD_TYPE_SET) {
		return -EINVAL;
	}

	final_call(sm_shutdown);
	return 0;
}

FUNC_NORETURN void sm_reset(void)
{
	sm_at_host_uninit();
	sm_power_off_modem();
	LOG_PANIC();
	sys_reboot(SYS_REBOOT_COLD);
}

SM_AT_CMD_CUSTOM(xreset, "AT#XRESET", handle_at_reset);
STATIC int handle_at_reset(enum at_parser_cmd_type cmd_type, struct at_parser *, uint32_t)
{
	if (cmd_type != AT_PARSER_CMD_TYPE_SET) {
		return -EINVAL;
	}

	final_call(sm_reset);
	return 0;
}

static void sm_modemreset(void)
{
	/* The modem must be put in minimal function mode before being shut down. */
	sm_power_off_modem();

	unsigned int step = 1;
	int ret;

	ret = nrf_modem_lib_shutdown();
	if (ret != 0) {
		goto out;
	}
	++step;

#if defined(CONFIG_SM_FULL_FOTA)
	if (sm_modem_full_fota) {
		sm_finish_modem_full_fota();
	}
#endif

	ret = nrf_modem_lib_init();

	if (sm_fota_type & DFU_TARGET_IMAGE_TYPE_ANY_MODEM) {
		sm_fota_post_process();
	}

out:
	if (ret) {
		/* Error; print the step that failed and its error code. */
		rsp_send("\r\n#XMODEMRESET: %u,%d\r\n", step, ret);
	} else {
		rsp_send("\r\n#XMODEMRESET: 0\r\n");
	}
	rsp_send_ok();
}

SM_AT_CMD_CUSTOM(xmodemreset, "AT#XMODEMRESET", handle_at_modemreset);
STATIC int handle_at_modemreset(enum at_parser_cmd_type cmd_type, struct at_parser *, uint32_t)
{
	if (cmd_type != AT_PARSER_CMD_TYPE_SET) {
		return -EINVAL;
	}

	/* Return immediately to allow the custom command handling in libmodem to finish processing,
	 * before restarting libmodem.
	 */
	final_call(sm_modemreset);

	return -SILENT_AT_COMMAND_RET;
}

SM_AT_CMD_CUSTOM(xuuid, "AT#XUUID", handle_at_uuid);
STATIC int handle_at_uuid(enum at_parser_cmd_type cmd_type, struct at_parser *, uint32_t)
{
	int ret;

	if (cmd_type != AT_PARSER_CMD_TYPE_SET) {
		return -EINVAL;
	}

	struct nrf_device_uuid dev = {0};

	ret = modem_jwt_get_uuids(&dev, NULL);
	if (ret) {
		LOG_ERR("Get device UUID error: %d", ret);
	} else {
		rsp_send("\r\n#XUUID: %s\r\n", dev.str);
	}

	return ret;
}

SM_AT_CMD_CUSTOM(xdatactrl, "AT#XDATACTRL", handle_at_datactrl);
STATIC int handle_at_datactrl(enum at_parser_cmd_type cmd_type, struct at_parser *parser,
			      uint32_t)
{
	int ret = 0;
	uint16_t time_limit, time_limit_min;

	switch (cmd_type) {
	case AT_PARSER_CMD_TYPE_SET:
		ret = at_parser_num_get(parser, 1, &time_limit);
		if (ret) {
			return ret;
		}
		if (time_limit > 0 && verify_datamode_control(time_limit, NULL)) {
			sm_datamode_time_limit = time_limit;
		} else {
			return -EINVAL;
		}
		break;

	case AT_PARSER_CMD_TYPE_READ:
		(void)verify_datamode_control(sm_datamode_time_limit, &time_limit_min);
		rsp_send("\r\n#XDATACTRL: %d,%d\r\n", sm_datamode_time_limit, time_limit_min);
		break;

	case AT_PARSER_CMD_TYPE_TEST:
		rsp_send("\r\n#XDATACTRL=<time_limit>\r\n");
		break;

	default:
		break;
	}

	return ret;
}

SM_AT_CMD_CUSTOM(xclac, "AT#XCLAC", handle_at_clac);
STATIC int handle_at_clac(enum at_parser_cmd_type cmd_type, struct at_parser *, uint32_t)
{
	if (cmd_type != AT_PARSER_CMD_TYPE_SET) {
		return -EINVAL;
	}

	/* Use AT_CMD_CUSTOM listing for extracting Serial Modem AT commands. */
	extern struct nrf_modem_at_cmd_custom _nrf_modem_at_cmd_custom_list_start[];
	extern struct nrf_modem_at_cmd_custom _nrf_modem_at_cmd_custom_list_end[];
	size_t cmd_custom_count = _nrf_modem_at_cmd_custom_list_end -
				  _nrf_modem_at_cmd_custom_list_start;
	size_t base_cmd_len[cmd_custom_count];

	memset(base_cmd_len, 0, cmd_custom_count * sizeof(size_t));
	rsp_send("\r\n");
	for (size_t i = 0; i < cmd_custom_count; i++) {
		const char *cmd = _nrf_modem_at_cmd_custom_list_start[i].cmd;
		/* Modem AT comamands start with 'AT+' or AT%. Other commands are
		 * Serial Modem specific'. Skip modem AT commands.
		 */
		if (strncasecmp(cmd, "AT+", strlen("AT+")) == 0 ||
		    strncasecmp(cmd, "AT%%", strlen("AT%%")) == 0) {
			continue;
		}
		/* List commands without operations and list each command only once. */
		base_cmd_len[i] = strcspn(_nrf_modem_at_cmd_custom_list_start[i].cmd, "?=");
		bool duplicate = false;

		for (size_t j = 0; j < i; j++) {
			/* Compare length and command as we have AT commands such as
			 * AT#XSEND/AT#XSENDTO, AT#XBIND="whatever"
			 * and AT#XNRFCLOUD[=?]/AT#XNRFCLOUDPOS.
			 */
			if ((base_cmd_len[i] == base_cmd_len[j]) &&
			    !strncasecmp(_nrf_modem_at_cmd_custom_list_start[i].cmd,
					 _nrf_modem_at_cmd_custom_list_start[j].cmd,
					 base_cmd_len[i])) {
				duplicate = true;
				break;
			}
		}

		if (!duplicate) {
			rsp_send("%.*s\r\n", base_cmd_len[i],
				 _nrf_modem_at_cmd_custom_list_start[i].cmd);
		}
	}

	return 0;
}

SM_AT_CMD_CUSTOM(ate0, "ATE0", handle_ate0);
STATIC int handle_ate0(enum at_parser_cmd_type cmd_type, struct at_parser *, uint32_t)
{
	sm_at_host_echo(false);

	return 0;
}

SM_AT_CMD_CUSTOM(ate1, "ATE1", handle_ate1);
STATIC int handle_ate1(enum at_parser_cmd_type cmd_type, struct at_parser *, uint32_t)
{
	sm_at_host_echo(true);

	return 0;
}
