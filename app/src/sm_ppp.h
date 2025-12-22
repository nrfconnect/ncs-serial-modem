/*
 * Copyright (c) 2023 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */
#ifndef SM_PPP_
#define SM_PPP_

#include <stdbool.h>
#include <zephyr/modem/pipe.h>

/* Whether to forward CGEV notifications to the Serial Modem UART. */
extern bool sm_fwd_cgev_notifs;

bool sm_ppp_is_stopped(void);

/** Set the modem pipe for PPP communication */
void sm_ppp_attach(struct modem_pipe *pipe);

#endif
