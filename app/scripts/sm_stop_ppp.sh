#!/bin/bash -u
#
# Copyright (c) 2025 Nordic Semiconductor ASA
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause

#
# Script to stop the PPP link started by sm_start_ppp.sh or sm2_start_ppp.sh.
#

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=common_functions.sh
source "$SCRIPT_DIR/common_functions.sh"

# Stop trace collection
if [ -f "$TRACE_PID_FILE" ]; then
	log_inf "Stopping trace collection..."
	trace_stop
fi

# Request PPPD to terminate
stop_ppp_link

# Wait for the shutdown script to complete
wait_for_shutdown_script
