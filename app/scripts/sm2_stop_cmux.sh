#!/bin/bash
#
# Copyright (c) 2026 Nordic Semiconductor ASA
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause

#
# Script to stop CMUX multiplexing started by sm2_start_cmux.sh.
#
# Sends the CMUX CLD (Close Down) frame, kills ldattach, and optionally
# restores the serial port baud rate if AT+IPR was used.
#

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=common_functions.sh
source "$SCRIPT_DIR/common_functions.sh"

usage() {
	echo "Usage: $0 [-s serial_port] [-b baud_rate] [-B original_baud] [-v] [-h]"
	echo ""
	echo "  -s serial_port : Serial port where the modem is connected (default: $MODEM)"
	echo "  -b baud_rate   : Current baud rate of Serial Modem (default: $BAUD)"
	echo "  -B original_baud : Restore modem baud rate to <original_baud> using AT+IPR."
	echo "                     Required if sm2_start_cmux.sh was run with -B."
	echo "  -v             : Enable verbose output"
	echo "  -h             : Show this help message"
	echo ""
	exit 0
}

# Parse command line parameters
while getopts s:b:B:vh flag
do
	case "${flag}" in
	s) MODEM=${OPTARG};;
	b) BAUD=${OPTARG};;
	B) IPR_BAUD=${OPTARG};;
	v) VERBOSE=1; CHATOPT="-v";;
	h|?) usage;;
	esac
done

AT_CMUX=$(cmux_dlc_list | head -n 1)

# Stop modem trace collection if running
trace_stop

# Send CMUX CLD frame via AT command on channel 1 if available
CHAT_ERR=1
if [ -n "$AT_CMUX" ] && [ -c "$AT_CMUX" ]; then
	log_dbg "Sending AT#XCMUXCLD on $AT_CMUX"
	# shellcheck disable=SC2086,SC2094 # CHATOPT must not be quoted, it may be empty
	chat $CHATOPT -t5 '' 'AT#XCMUXCLD' 'OK' >"$AT_CMUX" <"$AT_CMUX"
	CHAT_ERR=$?
fi

log_dbg "Killing ldattach..."
pkill ldattach
sleep 1

# If channel is still open, force-close with raw CMUX CLD frame
if [ $CHAT_ERR -ne 0 ]; then
	log_dbg "Force-closing CMUX with raw CLD frame..."
	cmux_close
fi

if [ "$IPR_BAUD" -ne 0 ] && [ -c "$MODEM" ]; then
	stty -F "$MODEM" "$BAUD"
	restore_modem_baud "$IPR_BAUD"
fi

log_dbg "CMUX stopped"
