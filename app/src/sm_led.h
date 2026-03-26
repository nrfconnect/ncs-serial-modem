/*
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#pragma once

/*
 * RGB LED status indicator for Thingy:91 X nRF9151 SLM.
 *
 * A background thread polls modem state every second and drives the LEDs:
 *
 *   All off  — CFUN=0 (modem off)
 *   Red      — modem on, no SIM card
 *   Blue     — modem on, SIM present, not yet registered
 *   Green    — registered to LTE network (home or roaming)
 *
 * No public API needed — the module is fully self-contained.
 */
