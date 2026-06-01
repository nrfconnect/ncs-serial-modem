/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/*
 * Minimal stub for date_time.h used in unit tests.
 * Only declares the symbols needed by sm_at_nrfcloud.c.
 */

#ifndef DATE_TIME_TEST_STUB_H_
#define DATE_TIME_TEST_STUB_H_

#include <stdint.h>

/** Event types reported by the date-time library. */
enum date_time_evt_type {
	DATE_TIME_OBTAINED_MODEM,
	DATE_TIME_OBTAINED_NTP,
	DATE_TIME_OBTAINED_EXT,
	DATE_TIME_NOT_OBTAINED,
};

struct date_time_evt {
	enum date_time_evt_type type;
};

typedef void (*date_time_evt_handler_t)(const struct date_time_evt *evt);

/**
 * @brief Stub: trigger async date/time update.
 *
 * In the test environment this is a no-op stub; the semaphore
 * sem_date_time therefore times out (after K_SECONDS(10)) inside
 * nrfcloud_conn_work_fn.  Tests that exercise the full connect work
 * should give the semaphore manually or wait for the timeout.
 */
int date_time_update_async(date_time_evt_handler_t evt_handler);

#endif /* DATE_TIME_TEST_STUB_H_ */
