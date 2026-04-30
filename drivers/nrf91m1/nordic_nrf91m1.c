#include <zephyr/drivers/modem/modem_cellular.h>
#include <zephyr/device.h>

MODEM_CHAT_MATCH_DEFINE(ok_match, "OK", "", NULL);
MODEM_CHAT_MATCH_DEFINE(connect_match, "CONNECT", "", NULL);
MODEM_CHAT_MATCHES_DEFINE(abort_matches, MODEM_CHAT_MATCH("ERROR", "", NULL));

MODEM_CHAT_SCRIPT_CMDS_DEFINE(init_chat_script_cmds,
			      MODEM_CHAT_SCRIPT_CMD_RESP("AT#XCMUXURC=2", ok_match),
			      MODEM_CHAT_SCRIPT_CMD_RESP("AT+CEREG?", ok_match),
			      MODEM_CHAT_SCRIPT_CMD_RESP("AT+CEREG=1", ok_match),
#if defined(CONFIG_MODEM_NRF91M1_ENABLE_UART_LOGGING)
			      MODEM_CHAT_SCRIPT_CMD_RESP("AT#XLOG=1", ok_match),
#endif
#if defined(CONFIG_MODEM_NRF91M1_EDRX_NO)
			      MODEM_CHAT_SCRIPT_CMD_RESP("AT+CEDRXS=0,4", ok_match),
#elif defined(CONFIG_MODEM_NRF91M1_EDRX_512)
			      MODEM_CHAT_SCRIPT_CMD_RESP("AT+CEDRXS=1,4,\"0000\"", ok_match),
#elif defined(CONFIG_MODEM_NRF91M1_EDRX_1024)
			      MODEM_CHAT_SCRIPT_CMD_RESP("AT+CEDRXS=1,4,\"0001\"", ok_match),
#elif defined(CONFIG_MODEM_NRF91M1_EDRX_2048)
			      MODEM_CHAT_SCRIPT_CMD_RESP("AT+CEDRXS=1,4,\"0010\"", ok_match),
#elif defined(CONFIG_MODEM_NRF91M1_EDRX_MANUAL)
			      MODEM_CHAT_SCRIPT_CMD_RESP("AT+CEDRXS=1,4,\""
				STRINGIFY(CONFIG_MODEM_NRF91M1_EDRX_MANUAL_VALUE) "\"", ok_match),
#endif

#if defined(CONFIG_MODEM_NRF91M1_PTW_128S)
			      MODEM_CHAT_SCRIPT_CMD_RESP("AT%XPTW=4,\"0000\"", ok_match),
#elif defined(CONFIG_MODEM_NRF91M1_PTW_256S)
			      MODEM_CHAT_SCRIPT_CMD_RESP("AT%XPTW=4,\"0001\"", ok_match),
#elif defined(CONFIG_MODEM_NRF91M1_PTW_512S)
			      MODEM_CHAT_SCRIPT_CMD_RESP("AT%XPTW=4,\"0010\"", ok_match),
#elif defined(CONFIG_MODEM_NRF91M1_PTW_1024S)
			      MODEM_CHAT_SCRIPT_CMD_RESP("AT%XPTW=4,\"0011\"", ok_match),
#elif defined(CONFIG_MODEM_NRF91M1_PTW_MANUAL)
			      MODEM_CHAT_SCRIPT_CMD_RESP("AT%XPTW=4,\""
				STRINGIFY(CONFIG_MODEM_NRF91M1_PTW_MANUAL_VALUE) "\"", ok_match),
#endif

#if !defined(CONFIG_MODEM_NRF91M1_PSM_NO)
#if defined(CONFIG_MODEM_NRF91M1_PSM_TAU_1H)
#define TAU_VALUE "00100001"
#elif defined(CONFIG_MODEM_NRF91M1_PSM_TAU_6H)
#define TAU_VALUE "00100110"
#elif defined(CONFIG_MODEM_NRF91M1_PSM_TAU_12H)
#define TAU_VALUE "00101100"
#endif
#endif /* CONFIG_MODEM_NRF91M1_PSM_NO */

#if defined(CONFIG_MODEM_NRF91M1_PSM_NO)
			      MODEM_CHAT_SCRIPT_CMD_RESP("AT+CPSMS=0", ok_match),
#elif defined(CONFIG_MODEM_NRF91M1_PSM_10S)
			      MODEM_CHAT_SCRIPT_CMD_RESP("AT+CPSMS=1,,,\"" TAU_VALUE
							 "\", \"00000101\"", ok_match),
#elif defined(CONFIG_MODEM_NRF91M1_PSM_20S)
			      MODEM_CHAT_SCRIPT_CMD_RESP("AT+CPSMS=1,,,\"" TAU_VALUE
							 "\", \"00001010\"", ok_match),
#elif defined(CONFIG_MODEM_NRF91M1_PSM_1M)
			      MODEM_CHAT_SCRIPT_CMD_RESP("AT+CPSMS=1,,,\"" TAU_VALUE
							 "\", \"00100001\"", ok_match),
#endif
			      MODEM_CHAT_SCRIPT_CMD_RESP("AT+CMUX=0", ok_match));

MODEM_CHAT_SCRIPT_DEFINE(init_chat_script, init_chat_script_cmds, abort_matches,
			 modem_cellular_chat_callback_handler, 2);

MODEM_CHAT_SCRIPT_CMDS_DEFINE(dial_chat_script_cmds,
			      MODEM_CHAT_SCRIPT_CMD_RESP("AT+CFUN=1", ok_match),
			      MODEM_CHAT_SCRIPT_CMD_RESP("AT+CGDATA", connect_match));

MODEM_CHAT_SCRIPT_DEFINE(dial_chat_script, dial_chat_script_cmds, abort_matches,
			 modem_cellular_chat_callback_handler, 60);

MODEM_CHAT_SCRIPT_CMDS_DEFINE(shutdown_chat_script_cmds,
			      MODEM_CHAT_SCRIPT_CMD_RESP("AT+CEREG=0", ok_match),
			      MODEM_CHAT_SCRIPT_CMD_RESP("AT+CFUN=0", ok_match));

MODEM_CHAT_SCRIPT_DEFINE(shutdown_chat_script, shutdown_chat_script_cmds, abort_matches,
			 modem_cellular_chat_callback_handler, 10);

#define NRF91M1_DEVICE(inst)                                                                       \
	MODEM_DT_INST_PPP_DEFINE(inst, MODEM_CELLULAR_INST_NAME(ppp, inst), NULL, 98, 1500, 64);   \
                                                                                                   \
	static struct modem_cellular_data MODEM_CELLULAR_INST_NAME(data, inst) = {                 \
		.chat_delimiter = "\r",                                                            \
		.chat_filter = "\n",                                                               \
		.ppp = &MODEM_CELLULAR_INST_NAME(ppp, inst),                                       \
	};                                                                                         \
                                                                                                   \
	MODEM_CELLULAR_DEFINE_AND_INIT_USER_PIPES(inst, (user_pipe_0, 3), (user_pipe_1, 4))        \
                                                                                                   \
	MODEM_CELLULAR_DEFINE_INSTANCE(inst, 0, 500, 5000, 100, false, NULL,                       \
				       &init_chat_script, &dial_chat_script, NULL,		   \
				       &shutdown_chat_script)

#define DT_DRV_COMPAT nordic_nrf91m1
DT_INST_FOREACH_STATUS_OKAY(NRF91M1_DEVICE)
#undef DT_DRV_COMPAT
