#!/bin/bash -u
#
# Copyright (c) 2025 Nordic Semiconductor ASA
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause

test -f /var/run/ppp-nrf91.pid && kill -SIGTERM $(head -1 </var/run/ppp-nrf91.pid)
