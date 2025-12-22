/*
 * Copyright (c) 2023 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */
#ifndef SM_CMUX_
#define SM_CMUX_

#include <stdbool.h>

struct modem_pipe;

/* Default CMUX channels. */
enum cmux_channel {
	CMUX_AT_CHANNEL = 1,
#if defined(CONFIG_SM_PPP)
	CMUX_PPP_CHANNEL = 2,
#endif
#if defined(CONFIG_SM_MODEM_TRACE_BACKEND_CMUX)
	CMUX_MODEM_TRACE_CHANNEL = 3,
#endif
	CMUX_USER_CHANNEL_0,
	CMUX_USER_CHANNEL_1,
	CMUX_EXT_CHANNEL_COUNT
};

#if CONFIG_SM_CMUX
bool sm_cmux_is_started(void);
#else
static inline bool sm_cmux_is_started(void)
{
	return false;
}
#endif

#endif
