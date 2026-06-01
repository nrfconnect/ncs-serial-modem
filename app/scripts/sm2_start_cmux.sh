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

#
# Default parameters
#
MODEM=/dev/ttyACM0
BAUD=115200
IPR_BAUD=0
VERBOSE=0
CHATOPT=""
MODEM_TRACE_FILE="/var/log/nrf91-modem-trace.bin"
TRACE_PID_FILE="/var/run/nrf91-modem-trace.pid"
TRACE=0

usage() {
	echo "Usage: $0 [-s serial_port] [-b baud_rate] [-B new_speed] [-T] [-v] [-h]"
	echo ""
	echo "  -s serial_port : Serial port where the modem is connected (default: $MODEM)"
	echo "  -b baud_rate   : Current baud rate of Serial Modem (default: $BAUD)"
	echo "  -B new_speed   : Use AT+IPR to change baud rate to <new_speed>"
	echo "                   Start with current baud rate and switch to new_speed after modem is"
	echo "                   responsive. If not set, baud rate will not be changed."
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

log_dbg() {
	if [ $VERBOSE -eq 1 ]; then
		echo "$@" >&2
	fi
}

if [[ ! -c $MODEM ]]; then
	echo "Serial port not found: $MODEM"
	exit 1
fi

# Remove stale trace PID file if process is not running
if [ -f "$TRACE_PID_FILE" ]; then
	if ! kill -0 $(cat "$TRACE_PID_FILE" 2>/dev/null) 2>/dev/null; then
		log_dbg "Removing stale trace PID file: $TRACE_PID_FILE"
		rm -f "$TRACE_PID_FILE"
	fi
fi

if find /dev -type c -name 'gsmtty*' | grep -q . ; then
	echo "Error: existing CMUX devices found (/dev/gsmtty*)"
	exit 1
fi

if pgrep ldattach >/dev/null; then
	echo "Error: existing ldattach process found"
	exit 1
fi

cmux_close() {
	printf "\xF9\xF9\xF9\xF9\xF9\xF9\xF9\xF9" > $MODEM
	printf "\xF9\x03\xEF\x05\xC3\x01\xF2\xF9" > $MODEM
	sleep 2
}

cleanup() {
	set +eu
	start-stop-daemon --stop --pidfile $TRACE_PID_FILE --remove-pidfile --oknodo
	pkill ldattach
	printf "\xF9\x03\xEF\x05\xC3\x01\xF2\xF9" > $MODEM
	echo "Failed to start CMUX..."
	exit 1
}

trap cleanup ERR

# Configure serial port
stty -F $MODEM $BAUD pass8 raw crtscts clocal

log_dbg "Wait modem to boot"
if chat -t1 "Ready--" "AT" "OK" <$MODEM >$MODEM; then
	log_dbg "Modem is in AT mode"
else
	log_dbg "Modem not responding, try CMUX close down..."
	cmux_close
	if ! chat -t1 "" "AT" "OK" <$MODEM >$MODEM; then
		echo "Error: Modem not responding"
		exit 1
	fi
fi

if [ $IPR_BAUD -ne 0 ]; then
	log_dbg "Set baud rate on modem to $IPR_BAUD"
	chat $CHATOPT -t1 '' "AT+IPR=$IPR_BAUD" "OK" >$MODEM <$MODEM
	# Reconfigure serial port
	stty -F $MODEM $IPR_BAUD pass8 raw crtscts clocal
fi

log_dbg "Attach CMUX channel to modem..."
chat $CHATOPT -t1 '' "AT+CMUX=0" "OK" >$MODEM <$MODEM
ldattach GSM0710 $MODEM

# AT_CMUX is the channel where setup commands will be sent and converted into PPP
AT_CMUX=$(ls /dev/gsmtty* | sort -V | head -n 1)
# AT_CMUX_USER is the AT channel that host can use after PPP is set up
AT_CMUX_USER=$(ls /dev/gsmtty* | sort -V | head -n 2 | tail -n 1)
log_dbg "AT CMUX:  $AT_CMUX_USER"
log_dbg "PPP CMUX: $AT_CMUX"

MT_CMUX=""
if [ $TRACE -gt 0 ]; then
	MT_CMUX=$(ls /dev/gsmtty* | sort -V | head -n 3 | tail -n 1)
	log_dbg "Trace CMUX: $MT_CMUX"
	echo "Trace file: $MODEM_TRACE_FILE"
	stty -F $MT_CMUX raw clocal -icrnl -ixon -opost
fi
sleep 3

stty -F $AT_CMUX clocal
test -c $AT_CMUX

if [ $TRACE -gt 0 ]; then
	echo "Starting trace collection..."
	chat $CHATOPT -t1 '' 'AT#XCMUXTRACE=3' 'OK' >$AT_CMUX <$AT_CMUX

	# Prefer to use socat, if installed.
	if command -v socat >/dev/null 2>&1; then
		start-stop-daemon --start --pidfile $TRACE_PID_FILE --make-pidfile \
			--background --exec $(command -v socat) -- -u $MT_CMUX,cfmakeraw,clocal=1 \
			CREATE:$MODEM_TRACE_FILE
	else
		start-stop-daemon --start --pidfile $TRACE_PID_FILE --make-pidfile \
		--background --exec /bin/dd -- if=$MT_CMUX of=$MODEM_TRACE_FILE bs=1024
	fi
fi

echo "CMUX started"
