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
#include "sm_at_tcp_proxy.h"
#include "sm_at_udp_proxy.h"
#include "sm_at_socket.h"
#include "sm_at_icmp.h"
#include "sm_at_sms.h"
#include "sm_at_fota.h"
#if defined(CONFIG_SM_NRF_CLOUD)
#include "sm_at_nrfcloud.h"
#endif
#if defined(CONFIG_SM_GNSS)
#include "sm_at_gnss.h"
#endif
#if defined(CONFIG_SM_FTPC)
#include "sm_at_ftp.h"
#endif
#if defined(CONFIG_SM_MQTTC)
#include "sm_at_mqtt.h"
#endif
#if defined(CONFIG_SM_HTTPC)
#include "sm_at_httpc.h"
#endif
#if defined(CONFIG_SM_TWI)
#include "sm_at_twi.h"
#endif
#if defined(CONFIG_SM_GPIO)
#include "sm_at_gpio.h"
#endif
#if defined(CONFIG_SM_CARRIER)
#include "sm_at_carrier.h"
#endif
#if defined(CONFIG_LWM2M_CARRIER_SETTINGS)
#include "sm_at_carrier_cfg.h"
#endif
#if defined(CONFIG_SM_PPP)
#include "sm_ppp.h"
#endif
#if defined(CONFIG_SM_CMUX)
#include "sm_cmux.h"
#endif

LOG_MODULE_REGISTER(sm_at, CONFIG_SM_LOG_LEVEL);

/** @brief Shutdown modes. */
enum sleep_modes {
	SLEEP_MODE_INVALID,
	SLEEP_MODE_DEEP,
	SLEEP_MODE_IDLE
};

static struct {
	struct k_work_delayable work;
	uint32_t mode;
} sleep_control;

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

SM_AT_CMD_CUSTOM(xslmver, "AT#XSLMVER", handle_at_slmver);
static int handle_at_slmver(enum at_parser_cmd_type cmd_type, struct at_parser *, uint32_t)
{
	int ret = -EINVAL;

	if (cmd_type == AT_PARSER_CMD_TYPE_SET) {
		char *libmodem = nrf_modem_build_version();

		if (strlen(CONFIG_SM_CUSTOMER_VERSION) > 0) {
			rsp_send("\r\n#XSLMVER: %s,\"%s\",\"%s\"\r\n",
				 STRINGIFY(NCS_VERSION_STRING), libmodem,
				 CONFIG_SM_CUSTOMER_VERSION);
		} else {
			rsp_send("\r\n#XSLMVER: %s,\"%s\"\r\n",
				 STRINGIFY(NCS_VERSION_STRING), libmodem);
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
static int handle_at_sleep(enum at_parser_cmd_type cmd_type, struct at_parser *parser,
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
			k_work_reschedule(&sleep_control.work, SM_UART_RESPONSE_DELAY);
		} else {
			ret = -EINVAL;
		}
	} else if (cmd_type == AT_PARSER_CMD_TYPE_TEST) {
		rsp_send("\r\n#XSLEEP: (%d,%d)\r\n", SLEEP_MODE_DEEP, SLEEP_MODE_IDLE);
		ret = 0;
	}

	return ret;
}

static void final_call(void (*func)(void))
{
	/* Delegate the final call to a worker so that the "OK" response is properly sent. */
	static struct k_work_delayable worker;

	k_work_init_delayable(&worker, (k_work_handler_t)func);
	k_work_schedule(&worker, SM_UART_RESPONSE_DELAY);
}

static void sm_shutdown(void)
{
	sm_at_host_uninit();
	sm_power_off_modem();
	LOG_PANIC();
	sm_ctrl_pin_enter_shutdown();
}

SM_AT_CMD_CUSTOM(xshutdown, "AT#XSHUTDOWN", handle_at_shutdown);
static int handle_at_shutdown(enum at_parser_cmd_type cmd_type, struct at_parser *, uint32_t)
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
static int handle_at_reset(enum at_parser_cmd_type cmd_type, struct at_parser *, uint32_t)
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
static int handle_at_modemreset(enum at_parser_cmd_type cmd_type, struct at_parser *, uint32_t)
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
static int handle_at_uuid(enum at_parser_cmd_type cmd_type, struct at_parser *, uint32_t)
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
static int handle_at_datactrl(enum at_parser_cmd_type cmd_type, struct at_parser *parser,
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
static int handle_at_clac(enum at_parser_cmd_type cmd_type, struct at_parser *, uint32_t)
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
		/* Serial Modem at commands start with AT#X. */
		if (strncasecmp(_nrf_modem_at_cmd_custom_list_start[i].cmd, "AT#X",
				strlen("AT#X"))) {
			continue;
		}
		/* List commands without operations and list each command only once. */
		base_cmd_len[i] = strcspn(_nrf_modem_at_cmd_custom_list_start[i].cmd, "?=");
		bool duplicate = false;

		for (size_t j = 0; j < i; j++) {
			/* Compare length and command as we have AT commands such as
			 * AT#XSEND/AT#XSENDTO, AT#XFTP="whatever"
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
static int handle_ate0(enum at_parser_cmd_type cmd_type, struct at_parser *, uint32_t)
{
	sm_at_host_echo(false);

	return 0;
}

SM_AT_CMD_CUSTOM(ate1, "ATE1", handle_ate1);
static int handle_ate1(enum at_parser_cmd_type cmd_type, struct at_parser *, uint32_t)
{
	sm_at_host_echo(true);

	return 0;
}

int sm_at_init(void)
{
	int err;

	k_work_init_delayable(&sleep_control.work, go_sleep_wk);

	err = sm_at_tcp_proxy_init();
	if (err) {
		LOG_ERR("%s initialization failed (%d).", "TCP Server", err);
		return -EFAULT;
	}
	err = sm_at_udp_proxy_init();
	if (err) {
		LOG_ERR("%s initialization failed (%d).", "UDP Server", err);
		return -EFAULT;
	}
	err = sm_at_socket_init();
	if (err) {
		LOG_ERR("%s initialization failed (%d).", "Socket", err);
		return -EFAULT;
	}
	err = sm_at_icmp_init();
	if (err) {
		LOG_ERR("%s initialization failed (%d).", "ICMP", err);
		return -EFAULT;
	}
#if defined(CONFIG_SM_SMS)
	err = sm_at_sms_init();
	if (err) {
		LOG_ERR("%s initialization failed (%d).", "SMS", err);
		return -EFAULT;
	}
#endif
	err = sm_at_fota_init();
	if (err) {
		LOG_ERR("%s initialization failed (%d).", "FOTA", err);
		return -EFAULT;
	}
#if defined(CONFIG_SM_NRF_CLOUD)
	err = sm_at_nrfcloud_init();
	if (err) {
		/* Allow nRF Cloud initialization to fail as sometimes JWT is missing
		 * especially during development.
		 */
		LOG_ERR("%s initialization failed (%d).", "nRF Cloud", err);
		err = 0;
	}
#endif
#if defined(CONFIG_SM_GNSS)
	err = sm_at_gnss_init();
	if (err) {
		LOG_ERR("%s initialization failed (%d).", "GNSS", err);
		return -EFAULT;
	}
#endif
#if defined(CONFIG_SM_FTPC)
	err = sm_at_ftp_init();
	if (err) {
		LOG_ERR("%s initialization failed (%d).", "FTP", err);
		return -EFAULT;
	}
#endif
#if defined(CONFIG_SM_MQTTC)
	err = sm_at_mqtt_init();
	if (err) {
		LOG_ERR("%s initialization failed (%d).", "MQTT", err);
		return -EFAULT;
	}
#endif
#if defined(CONFIG_SM_HTTPC)
	err = sm_at_httpc_init();
	if (err) {
		LOG_ERR("%s initialization failed (%d).", "HTTP", err);
		return -EFAULT;
	}
#endif
#if defined(CONFIG_SM_GPIO)
	err = sm_at_gpio_init();
	if (err) {
		LOG_ERR("%s initialization failed (%d).", "GPIO", err);
		return -EFAULT;
	}
#endif
#if defined(CONFIG_SM_TWI)
	err = sm_at_twi_init();
	if (err) {
		LOG_ERR("%s initialization failed (%d).", "TWI", err);
		return -EFAULT;
	}
#endif
#if defined(CONFIG_SM_CARRIER)
	err = sm_at_carrier_init();
	if (err) {
		LOG_ERR("%s initialization failed (%d).", "LwM2M carrier", err);
		return -EFAULT;
	}
#endif
#if defined(CONFIG_LWM2M_CARRIER_SETTINGS)
	err = sm_at_carrier_cfg_init();
	if (err) {
		LOG_ERR("%s initialization failed (%d).", "LwM2M carrier", err);
		return -EFAULT;
	}
#endif
#if defined(CONFIG_SM_CMUX)
	sm_cmux_init();
#endif
#if defined(CONFIG_SM_PPP)
	err = sm_ppp_init();
	if (err) {
		LOG_ERR("%s initialization failed (%d).", "PPP", err);
		return err;
	}
#endif
	return err;
}

void sm_at_uninit(void)
{
	int err;

	err = sm_at_tcp_proxy_uninit();
	if (err) {
		LOG_WRN("%s uninitialization failed (%d).", "TCP Server", err);
	}
	err = sm_at_udp_proxy_uninit();
	if (err) {
		LOG_WRN("%s uninitialization failed (%d).", "UDP Server", err);
	}
	err = sm_at_socket_uninit();
	if (err) {
		LOG_WRN("%s uninitialization failed (%d).", "Socket", err);
	}
	err = sm_at_icmp_uninit();
	if (err) {
		LOG_WRN("%s uninitialization failed (%d).", "ICMP", err);
	}
#if defined(CONFIG_SM_SMS)
	err = sm_at_sms_uninit();
	if (err) {
		LOG_WRN("%s uninitialization failed (%d).", "SMS", err);
	}
#endif
	err = sm_at_fota_uninit();
	if (err) {
		LOG_WRN("%s uninitialization failed (%d).", "FOTA", err);
	}
#if defined(CONFIG_SM_NRF_CLOUD)
	err = sm_at_nrfcloud_uninit();
	if (err) {
		LOG_WRN("%s uninitialization failed (%d).", "nRF Cloud", err);
	}
#endif
#if defined(CONFIG_SM_GNSS)
	err = sm_at_gnss_uninit();
	if (err) {
		LOG_WRN("%s uninitialization failed (%d).", "GNSS", err);
	}
#endif
#if defined(CONFIG_SM_FTPC)
	err = sm_at_ftp_uninit();
	if (err) {
		LOG_WRN("%s uninitialization failed (%d).", "FTP", err);
	}
#endif
#if defined(CONFIG_SM_MQTTC)
	err = sm_at_mqtt_uninit();
	if (err) {
		LOG_WRN("%s uninitialization failed (%d).", "MQTT", err);
	}
#endif
#if defined(CONFIG_SM_HTTPC)
	err = sm_at_httpc_uninit();
	if (err) {
		LOG_WRN("%s uninitialization failed (%d).", "HTTP", err);
	}
#endif
#if defined(CONFIG_SM_TWI)
	err = sm_at_twi_uninit();
	if (err) {
		LOG_WRN("%s uninitialization failed (%d).", "TWI", err);
	}
#endif
#if defined(CONFIG_SM_GPIO)
	err = sm_at_gpio_uninit();
	if (err) {
		LOG_WRN("%s uninitialization failed (%d).", "GPIO", err);
	}
#endif
#if defined(CONFIG_SM_CARRIER)
	err = sm_at_carrier_uninit();
	if (err) {
		LOG_WRN("%s uninitialization failed (%d).", "LwM2M carrier", err);
	}
#endif
#if defined(CONFIG_SM_CMUX)
	sm_cmux_uninit();
#endif
}
