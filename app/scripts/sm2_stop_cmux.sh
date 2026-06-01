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

#
# Default parameters
#
MODEM=/dev/ttyACM0
BAUD=115200
IPR_BAUD=0
VERBOSE=0
CHATOPT=""
TRACE_PID_FILE="/var/run/nrf91-modem-trace.pid"

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

log_dbg() {
	if [ $VERBOSE -eq 1 ]; then
		logger --id=$$ -s "$@"
	fi
}

AT_CMUX=$(ls /dev/gsmtty* 2>/dev/null | sort -V | head -n 1 || true)

# Stop modem trace collection if running
start-stop-daemon -q --stop --pidfile $TRACE_PID_FILE --remove-pidfile --oknodo --retry 1

# Send CMUX CLD frame via AT command on channel 1 if available
if [ -n "$AT_CMUX" ] && [ -c "$AT_CMUX" ]; then
	log_dbg "Sending AT#XCMUXCLD on $AT_CMUX"
	chat $CHATOPT -t5 '' 'AT#XCMUXCLD' 'OK' >$AT_CMUX <$AT_CMUX
	CHAT_ERR=$?
else
	CHAT_ERR=1
fi

log_dbg "Killing ldattach..."
pkill ldattach || true
sleep 1

cmux_close() {
	printf "\xF9\xF9\xF9\xF9\xF9\xF9\xF9\xF9" > $MODEM
	printf "\xF9\x03\xEF\x05\xC3\x01\xF2\xF9" > $MODEM
	sleep 2
}

# If channel is still open, force-close with raw CMUX CLD frame
if [ $CHAT_ERR -ne 0 ]; then
	log_dbg "Force-closing CMUX with raw CLD frame..."
	cmux_close
fi

if [ $IPR_BAUD -ne 0 ]; then
	log_dbg "Restoring baud rate on modem to $IPR_BAUD"
	stty -F $MODEM $BAUD
	chat $CHATOPT -t1 '' "AT+IPR=$IPR_BAUD" "OK" >$MODEM <$MODEM || true
	stty -F $MODEM $IPR_BAUD
fi

log_dbg "CMUX stopped"
