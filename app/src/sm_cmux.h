/*
 * Copyright (c) 2023 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */
#ifndef SM_CMUX_
#define SM_CMUX_

#include <stdint.h>
#include <stdbool.h>

struct modem_pipe;

/* Default CMUX channels when AT#XCMUX is used */
enum cmux_channel {
	CMUX_AT_CHANNEL = 1,
	CMUX_PPP_CHANNEL = 2,
	CMUX_MODEM_TRACE_CHANNEL = 3,
};

#if CONFIG_SM_CMUX
bool sm_cmux_is_started(void);
#else
static inline bool sm_cmux_is_started(void)
{
	return false;
}
#endif

/**
 * @brief Get a pointer to the modem_pipe associated with a given DLCI address.
 *
 * @param address The DLCI address.
 * @return struct modem_pipe* Pointer to the modem_pipe,
 *	   or NULL if there is no pipe associated with the given address.
 */
struct modem_pipe *sm_cmux_get_dlci(uint8_t address);

#endif
