/*
 * Copyright (c) 2023 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */
#ifndef SM_CMUX_
#define SM_CMUX_

#include <stdbool.h>

struct modem_pipe;

/* CMUX channels that are used by other modules. */
enum cmux_channel {
#if defined(CONFIG_SM_PPP)
	CMUX_PPP_CHANNEL,
#endif
#if defined(CONFIG_SM_GNSS_OUTPUT_NMEA_ON_CMUX_CHANNEL)
	CMUX_GNSS_CHANNEL,
#endif
	CMUX_USER_CHANNEL_0,
	CMUX_USER_CHANNEL_1,
	CMUX_EXT_CHANNEL_COUNT
};

#endif
