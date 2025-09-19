/*
 * Copyright (c) 2023 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */
#ifndef SM_PPP_
#define SM_PPP_

#include <stdbool.h>

/* Whether to forward CGEV notifications to the Serial Modem UART. */
extern bool sm_fwd_cgev_notifs;

/** @retval 0 on success. */
int sm_ppp_init(void);

bool sm_ppp_is_stopped(void);

#endif
