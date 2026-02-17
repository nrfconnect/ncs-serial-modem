#!/bin/bash -u
#
# Copyright (c) 2025 Nordic Semiconductor ASA
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause

PIDFILE="/var/run/nrf91-modem.pid"
PPP_PIDFILE="/var/run/ppp-nrf91.pid"
TRACE_PID_FILE="/var/run/nrf91-modem-trace.pid"

# Request PPPD to terminate
if [ -f $PPP_PIDFILE ]; then
        echo "Stopping PPP link..."
        kill -SIGTERM $(head -1 <$PPP_PIDFILE)
fi

# Stop trace collection
if [ -f $TRACE_PID_FILE ]; then
        echo "Stopping trace collection..."
        kill $(cat $TRACE_PID_FILE) 2>/dev/null || true
        rm -f $TRACE_PID_FILE
fi

# Wait for Shutdown script to complete
if [ -f $PIDFILE ]; then
        echo "Waiting for Shutdown script to complete..."
        timeout 12s tail --pid=$(head -1 <$PIDFILE) -f /dev/null \
        || echo "Timeout waiting for Shutdown script to stop"
fi
