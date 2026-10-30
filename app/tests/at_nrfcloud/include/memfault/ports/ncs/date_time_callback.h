/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/*
 * Minimal test stub for <memfault/ports/ncs/date_time_callback.h>.
 * Only the symbols used by sm_at_nrfcloud.c are provided.
 */

#ifndef STUB_MEMFAULT_PORTS_NCS_DATE_TIME_CALLBACK_H_
#define STUB_MEMFAULT_PORTS_NCS_DATE_TIME_CALLBACK_H_

#include <date_time.h>

void memfault_zephyr_date_time_evt_handler(const struct date_time_evt *evt);

#endif /* STUB_MEMFAULT_PORTS_NCS_DATE_TIME_CALLBACK_H_ */
