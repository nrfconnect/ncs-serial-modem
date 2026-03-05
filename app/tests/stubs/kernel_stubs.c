/*
 * Copyright (c) 2025 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/**
 * @file kernel_stubs.c
 * Stub implementations for kernel memory allocation functions
 */

#include <zephyr/kernel.h>
#include <stdlib.h>

/* Stub for k_malloc - use standard malloc for testing */
void *k_malloc(size_t size)
{
	return malloc(size);
}

/* Stub for k_free - use standard free for testing */
void k_free(void *ptr)
{
	free(ptr);
}
