/*
 * Copyright (c) 2023 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */
#ifndef SM_CMUX_
#define SM_CMUX_

#include <stdbool.h>

struct modem_pipe;

/** @brief Initialize the CMUX subsystem. */
void sm_cmux_init(void);

/** @brief Uninitialize the CMUX subsystem. */
void sm_cmux_uninit(void);

/* CMUX channels that are used by other modules. */
enum cmux_channel {
#if defined(CONFIG_SM_PPP)
	CMUX_PPP_CHANNEL,
#endif
#if defined(CONFIG_SM_GNSS_OUTPUT_NMEA_ON_CMUX_CHANNEL)
	CMUX_GNSS_CHANNEL,
#endif
	CMUX_EXT_CHANNEL_COUNT
};
struct modem_pipe *sm_cmux_reserve(enum cmux_channel);
void sm_cmux_release(enum cmux_channel, bool fallback);

#endif
