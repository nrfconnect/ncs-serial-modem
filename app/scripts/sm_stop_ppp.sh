#!/bin/bash -u
#
# Copyright (c) 2025 Nordic Semiconductor ASA
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause

PIDFILE="/var/run/nrf91-modem.pid"
PPP_PIDFILE="/var/run/ppp-nrf91.pid"

# Request PPPD to terminate
if [ -f $PPP_PIDFILE ]; then
        echo "Stopping PPP link..."
        kill -SIGTERM $(head -1 <$PPP_PIDFILE)
fi


# Wait for Shutdown script to complete
if [ -f $PIDFILE ]; then
        echo "Waiting for Shutdown script to complete..."
        timeout 12s tail --pid=$(head -1 <$PIDFILE) -f /dev/null \
        || echo "Timeout waiting for Shutdown script to stop"
fi
