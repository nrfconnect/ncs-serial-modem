/*
 * Copyright (c) 2023 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */
#ifndef SM_CMUX_
#define SM_CMUX_

#include <stdbool.h>

struct modem_pipe;

/* CMUX channels. Does not include AT-channel. */
enum cmux_channel {
#if defined(CONFIG_SM_PPP)
	CMUX_PPP_CHANNEL,
#endif
#if defined(CONFIG_SM_MODEM_TRACE_BACKEND_CMUX)
	CMUX_MODEM_TRACE_CHANNEL,
#endif
#if defined(CONFIG_SM_GNSS_OUTPUT_NMEA_ON_CMUX_CHANNEL)
	CMUX_GNSS_CHANNEL,
#endif
	CMUX_EXT_CHANNEL_COUNT
};
struct modem_pipe *sm_cmux_reserve(enum cmux_channel channel);
void sm_cmux_release(enum cmux_channel channel);
bool sm_cmux_dlci_is_open(enum cmux_channel channel);

#if CONFIG_SM_CMUX
bool sm_cmux_is_started(void);
#else
static inline bool sm_cmux_is_started(void)
{
	return false;
}
#endif

#endif
