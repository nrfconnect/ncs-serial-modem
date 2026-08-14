#!/bin/bash -eu
#
# Copyright (c) 2026 Nordic Semiconductor ASA
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause

#
# Script to set up CMUX multiplexing on Serial Modem application 2.0.0 or
# later using standard 3GPP AT commands (AT+CMUX=0).
#
# Use this script to initialize CMUX independently from PPP. Once CMUX is
# running, start and stop PPP using sm2_start_ppp.sh -C and sm_stop_ppp.sh
# without tearing down CMUX each time.
#
# Use sm2_stop_cmux.sh to fully close CMUX and release the serial port.
#
# Uses the following DLC channels in CMUX:
# - 1: PPP data channel
# - 2: AT commands channel
# - 3: Modem trace collection (optional)
#

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=common_functions.sh
source "$SCRIPT_DIR/common_functions.sh"

usage() {
	echo "Usage: $0 [-s serial_port] [-b baud_rate] [-B new_speed] [-T] [-v] [-h]"
	echo ""
	echo "  -s serial_port : Serial port where the modem is connected (default: $MODEM)"
	echo "  -b baud_rate   : Current baud rate of Serial Modem (default: $BAUD)"
	echo "  -B new_speed   : Use AT+IPR to change baud rate to <new_speed>"
	echo "                   Start with current baud rate and switch to new_speed after modem"
	echo "                   is responsive. If not set, baud rate will not be changed."
	echo "  -T             : Enable modem trace collection (file: $MODEM_TRACE_FILE)"
	echo "  -v             : Enable verbose output"
	echo "  -h             : Show this help message"
	echo ""
	exit 0
}

# Parse command line parameters
while getopts s:b:B:Tvh flag
do
	case "${flag}" in
	s) MODEM=${OPTARG};;
	b) BAUD=${OPTARG};;
	B) IPR_BAUD=${OPTARG};;
	T) TRACE=1;;
	v) VERBOSE=1; CHATOPT="-v";;
	h|?) usage;;
	esac
done

require_serial_port_or_exit

remove_stale_pidfile "$TRACE_PID_FILE"

check_no_existing_cmux_or_exit

cleanup() {
	set +eu
	trace_stop
	pkill ldattach
	cmux_close
	log_err "Failed to start CMUX..."
	exit 1
}

trap cleanup ERR

configure_serial_port

wait_modem_ready_or_exit

if [ "$IPR_BAUD" -ne 0 ]; then
	set_modem_baud "$IPR_BAUD"
fi

cmux_attach "AT+CMUX=0" 3

# DLC 1: PPP data channel
# DLC 2: AT command channel for host
# DLC 3: Trace channel
DLC1=${DLCS[0]}
DLC2=${DLCS[1]}
DLC3=${DLCS[2]}

log_inf "DLC 1 (PPP):       $DLC1"
log_inf "DLC 2 (AT):        $DLC2"

if [ "$TRACE" -gt 0 ]; then
	log_inf "DLC 3 (TRACE):     $DLC3"
	# shellcheck disable=SC2086,SC2094 # CHATOPT must not be quoted, it may be empty
	chat $CHATOPT -t1 '' 'AT#XCMUXTRACE=3' 'OK' >"$DLC1" <"$DLC1"
	trace_start "$DLC3"
fi

log_dbg "CMUX started"
