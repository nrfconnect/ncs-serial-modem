/*
 * Copyright (c) 2025 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/**
 * @file pm_stubs.c
 * Stub implementations for power management functions
 */

#include <zephyr/device.h>
#include <zephyr/pm/device.h>

/* Stub for pm_device_action_run */
int pm_device_action_run(const struct device *dev, enum pm_device_action action)
{
	/* Stub - return success */
	(void)dev;
	(void)action;
	return 0;
}
