/*
 * Copyright (c) 2021 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef SM_DEFINES_
#define SM_DEFINES_

#include "sm_trap_macros.h"
#include <stdbool.h>

#define INVALID_SOCKET       -1

enum {
	/** The command ran successfully and doesn't want the automatic response to be sent. */
	SILENT_AT_COMMAND_RET = __ELASTERROR,
	/** The AT command continues to execute on background */
	AT_COMMAND_CONTINUE_RET,
};

#define SM_MAX_DNS_LEN      128  /** max size of DNS name */
#define SM_MAX_USERNAME     32   /** max size of username in login */
#define SM_MAX_PASSWORD     32   /** max size of password in login */
#define SM_MAX_URL          4096 /** max size of URL string */

#define SM_SYNC_STR     "Ready\r\n"
#define SM_SYNC_ERR_STR "INIT ERROR\r\n"
#define OK_STR		 "\r\nOK\r\n"
#define ERROR_STR	 "\r\nERROR\r\n"
#define CRLF_STR	 "\r\n"
#define CR		 '\r'
#define LF		 '\n'

/** Global flag indicating if Serial Modem initialization failed.
 * May be set by any of the modules during SYS_INIT().
 */
extern bool sm_init_failed;

#endif
