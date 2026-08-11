/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/**
 * @file eventfd.h
 *
 * Test-specific replacement of <zephyr/posix/sys/eventfd.h>.
 *
 * The eventfd used by sm_ppp.c to wake up its data passing thread is emulated
 * by socket_stubs.c so that the tests can drive the thread deterministically.
 */

#ifndef TEST_ZEPHYR_POSIX_SYS_EVENTFD_H_
#define TEST_ZEPHYR_POSIX_SYS_EVENTFD_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EFD_SEMAPHORE 0x1
#define EFD_NONBLOCK  0x4000

typedef uint64_t eventfd_t;

int eventfd(unsigned int initval, int flags);
int eventfd_read(int fd, eventfd_t *value);
int eventfd_write(int fd, eventfd_t value);

#ifdef __cplusplus
}
#endif

#endif /* TEST_ZEPHYR_POSIX_SYS_EVENTFD_H_ */
