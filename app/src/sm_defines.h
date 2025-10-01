/*
 * Copyright (c) 2021 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef SM_DEFINES_
#define SM_DEFINES_

#include <nrf_socket.h>
#include "sm_trap_macros.h"

#define INVALID_SOCKET       -1
#define INVALID_ROLE         -1
#define INVALID_DTLS_CID     -1

enum {
	/* The command ran successfully and doesn't want the automatic response to be sent. */
	SILENT_AT_COMMAND_RET = __ELASTERROR,
	/* The AT to CMUX change command ran successfully. */
	SILENT_AT_CMUX_COMMAND_RET,
};

/** The maximum allowed length of an AT command/response passed through the Serial Modem */
#define SM_AT_MAX_CMD_LEN   4096
#define SM_AT_MAX_RSP_LEN   2100

/** The maximum allowed length of data send/receive through the Serial Modem */
#define SM_MAX_PAYLOAD_SIZE 1024 /** max size of payload sent in command mode */
#define SM_MAX_MESSAGE_SIZE NRF_SOCKET_TLS_MAX_MESSAGE_SIZE

#define SM_MAX_URL          128  /** max size of URL string */
#define SM_MAX_FILEPATH     128  /** max size of filepath string */
#define SM_MAX_USERNAME     32   /** max size of username in login */
#define SM_MAX_PASSWORD     32   /** max size of password in login */

#define SM_NRF52_BLK_SIZE   4096 /** nRF52 flash block size for write operation */
#define SM_NRF52_BLK_TIME   2000 /** nRF52 flash block write time in millisecond (1.x second) */

#endif
