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
#include <zephyr/sys/sys_heap.h>
#include <zephyr/debug/thread_analyzer.h>
#include <sys_malloc.h>
#include <dfu/dfu_target.h>
#include <tfm/tfm_ioctl_api.h>
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
#include "sm_log.h"

LOG_MODULE_REGISTER(sm_at, CONFIG_SM_LOG_LEVEL);

/* Upper bound on the number of custom AT commands AT#XCLAC can list. The actual
 * count is a link-time constant well below this; the bound only exists so the
 * bookkeeping array has a fixed, diagnosable size.
 */
#define CLAC_MAX_COMMANDS 256

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
			rsp_send("\r\n#XSMVER: \"%s\",\"%s\",\"%s\"\r\n",
				 SM_VERSION, NCS_VERSION_STRING,
				 CONFIG_SM_CUSTOMER_VERSION);
		} else {
			rsp_send("\r\n#XSMVER: \"%s\",\"%s\"\r\n",
				 SM_VERSION, NCS_VERSION_STRING);
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
	sm_power_off_modem();
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
	sm_power_off_modem();
	sm_log_flush();
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
	if (sm_fota_type == SM_FOTA_TYPE_FULL_MFW) {
		sm_finish_modem_full_fota();
	}
#endif

	ret = nrf_modem_lib_init();

	if (sm_fota_type == SM_FOTA_TYPE_MFW || sm_fota_type == SM_FOTA_TYPE_FULL_MFW) {
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
	/* Fixed-size instead of a VLA: the count is a link-time constant, but a
	 * stack array sized from a runtime expression gives no diagnosable bound.
	 */
	size_t base_cmd_len[CLAC_MAX_COMMANDS] = {0};

	if (cmd_custom_count > ARRAY_SIZE(base_cmd_len)) {
		LOG_ERR("Custom AT command count %zu exceeds CLAC_MAX_COMMANDS (%zu); "
			"listing is truncated.", cmd_custom_count, ARRAY_SIZE(base_cmd_len));
		cmd_custom_count = ARRAY_SIZE(base_cmd_len);
	}

	rsp_send("\r\n");
	for (size_t i = 0; i < cmd_custom_count; i++) {
		const char *cmd = _nrf_modem_at_cmd_custom_list_start[i].cmd;
		/* Modem AT commands start with 'AT+' or AT%. Other commands are
		 * Serial Modem specific'. Skip modem AT commands.
		 * Exceptions that are implemented in Serial Modem:
		 *	* AT+IPR
		 *	* AT+CMUX
		 *	* AT+CGDATA
		 */
		if ((strncasecmp(cmd, "AT+", strlen("AT+")) == 0 &&
		     strncasecmp(cmd, "AT+IPR", strlen("AT+IPR")) != 0 &&
		     strncasecmp(cmd, "AT+CMUX", strlen("AT+CMUX")) != 0 &&
		     strncasecmp(cmd, "AT+CGDATA", strlen("AT+CGDATA")) != 0) ||
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
			/* The %.*s precision argument must have type int per the
			 * C standard; base_cmd_len[] is size_t, so cast explicitly
			 * rather than relying on size_t == unsigned int (true on
			 * this 32-bit target, but not on a 64-bit host build).
			 */
			rsp_send("%.*s\r\n", (int)base_cmd_len[i],
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

/** @brief Operations for AT#XBOOTINFO. */
enum xbootinfo_op {
	XBOOTINFO_OP_VERSION = 0,
	XBOOTINFO_OP_SLOT    = 1,
};

SM_AT_CMD_CUSTOM(xbootinfo, "AT#XBOOTINFO", handle_at_xbootinfo);
STATIC int handle_at_xbootinfo(enum at_parser_cmd_type cmd_type, struct at_parser *parser,
			       uint32_t param_count)
{
	ARG_UNUSED(param_count);

	switch (cmd_type) {
	case AT_PARSER_CMD_TYPE_SET: {
		uint32_t op = 0;
		int err = at_parser_num_get(parser, 1, &op);

		if (err) {
			return -EINVAL;
		}

		if (op == XBOOTINFO_OP_VERSION) {
			uint32_t version = 0;

			err = sm_util_mcuboot_active_version(&version);
			if (err) {
				return err;
			}
			rsp_send("\r\n#XBOOTINFO: %u\r\n", version);
		} else if (op == XBOOTINFO_OP_SLOT) {
			int ret = sm_util_mcuboot_active_slot();

			if (ret < 0) {
				return ret;
			}
			rsp_send("\r\n#XBOOTINFO: %u\r\n", ret);
		} else {
			return -EINVAL;
		}

		return 0;
	}
	case AT_PARSER_CMD_TYPE_TEST:
		rsp_send("\r\n#XBOOTINFO: (%d,%d)\r\n", XBOOTINFO_OP_VERSION, XBOOTINFO_OP_SLOT);
		return 0;
	default:
		return -EINVAL;
	}
}

#if defined(CONFIG_SM_DEBUG_STATS_HEAP)

/* The feature only works if logging is enabled and the log level is at least INF. */
BUILD_ASSERT(IS_ENABLED(CONFIG_LOG), "AT#XDBGSTATSMEM requires CONFIG_LOG");
BUILD_ASSERT(CONFIG_SM_LOG_LEVEL >= 3,
	     "AT#XDBGSTATSMEM requires CONFIG_SM_LOG_LEVEL_INF or CONFIG_SM_LOG_LEVEL_DBG");

extern struct sys_heap _system_heap;

SM_AT_CMD_CUSTOM(xmemstats, "AT#XDBGSTATSMEM", handle_at_memstats);
STATIC int handle_at_memstats(enum at_parser_cmd_type cmd_type, struct at_parser *, uint32_t)
{
	int ret;
	struct sys_memory_stats kernel_stats;
	struct sys_memory_stats malloc_stats;

	if (cmd_type != AT_PARSER_CMD_TYPE_SET) {
		return -EINVAL;
	}

	/* System heap stats */
	ret = malloc_runtime_stats_get(&malloc_stats);
	if (ret) {
		LOG_WRN("Failed to read system heap stats, error: %d", ret);
	} else {
		LOG_INF("System heap stats:");
		LOG_INF("  free:           %6d", malloc_stats.free_bytes);
		LOG_INF("  allocated:      %6d", malloc_stats.allocated_bytes);
		LOG_INF("  max. allocated: %6d", malloc_stats.max_allocated_bytes);
	}

	/* Kernel heap stats */
	ret = sys_heap_runtime_stats_get(&_system_heap, &kernel_stats);
	if (ret) {
		LOG_WRN("Failed to read kernel heap stats, error: %d", ret);
	} else {
		LOG_INF("Kernel heap stats:");
		LOG_INF("  free:           %6d", kernel_stats.free_bytes);
		LOG_INF("  allocated:      %6d", kernel_stats.allocated_bytes);
		LOG_INF("  max. allocated: %6d", kernel_stats.max_allocated_bytes);
	}
#if defined(CONFIG_SYS_HEAP_INFO)
	LOG_INF("Kernel heap block information:");
	sys_heap_print_info(&_system_heap, true);
#endif

	/* Thread stack usage statistics */
#if defined(CONFIG_SM_DEBUG_STATS_THREAD_STACK)
	LOG_INF("Thread stack stats:");
	thread_analyzer_print(0);
#endif

	return 0;
}
#endif
