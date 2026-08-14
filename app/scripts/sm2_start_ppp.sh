#!/bin/bash -eu
#
# Copyright (c) 2026 Nordic Semiconductor ASA
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause

#
# Script to start PPP link inside CMUX channel using standard 3GPP AT commands
# using Serial Modem application 2.0.0 or later on nRF91
#
# PPPD process is kept running until killed by sm_stop_ppp.sh or "poff"
# On all failures, except SIGTEM and SIGINT, the script will restart the PPPD
# process until killed.
#
# Connection is closed by running "sm_stop_ppp.sh" script or using "poff" command.
#
# NOTE: This script is equivalent to "sm_start_ppp.sh" with the exception that it
#       uses 3GPP standard commands (AT+CMUX and AT+CGDATA) instead of Nordic's
#       proprietary AT#XCMUX and AT#XPPP. This causes PPPD to use the same CMUX channel
#       for both AT commands and PPP data, while in "sm_start_ppp.sh" PPP data is sent
#       over a separate CMUX channel.
#
# Uses following DLC channels in CMUX:
# - 1: AT commands and PPP
# - 3: Modem trace collection (optional)
#

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
COMMON_FUNCTIONS="$SCRIPT_DIR/common_functions.sh"
# shellcheck source=common_functions.sh
source "$COMMON_FUNCTIONS"

#
# Script specific default parameters
#
TIMEOUT=60
APN="internet"
PDP_TYPE="IPV4V6"
CID=0

usage() {
	echo "Usage: $0 [-s serial_port] [-b baud_rate] [-B new_speed] [-t timeout] [-a APN]"
	echo "          [-f IP|IPV6|IPV4V6] [-p PDN] [-C] [-T] [-v] [-h]"
	echo ""
	echo "  -s serial_port : Serial port where the modem is connected (default: $MODEM)"
	echo "  -b baud_rate   : Current baud rate of Serial Modem (default: $BAUD)"
	echo "  -B new_speed   : Use AT+IPR to change baud rate to <new_speed>"
	echo "                   Start with current baud rate and switch to new_speed after modem"
	echo "                   is responsive. If not set, baud rate will not be changed."
	echo "                   When terminated, baud rate will be switched back to original."
	echo "  -t timeout     : Timeout for dialup commands in seconds (default: $TIMEOUT)"
	echo "  -a APN         : Access Point Name for cellular connection (default: $APN)"
	echo "  -f FAMILY      : PDP_type, one of IP, IPV6, IPV4V6 (default: $PDP_TYPE)"
	echo "  -p PDN         : PDN ID to use (default: $CID), 0 means use default PDN"
	echo "  -C             : Attach to existing CMUX (started with sm2_start_cmux.sh)."
	echo "                   Skips CMUX initialization and teardown."
	echo "  -T             : Enable modem trace collection (file: $MODEM_TRACE_FILE)"
	echo "  -v             : Enable verbose output"
	echo "  -h             : Show this help message"
	echo ""
	exit 0
}

# Parse command line parameters
while getopts s:b:B:t:a::f:p:CThv flag
do
	case "${flag}" in
	s) MODEM=${OPTARG};;
	b) BAUD=${OPTARG};;
	B) IPR_BAUD=${OPTARG};;
	t) TIMEOUT=${OPTARG};;
	a) APN=${OPTARG};;
	p) CID=${OPTARG};;
	f) PDP_TYPE=${OPTARG};;
	C) CMUX_ATTACHED=1;;
	T) TRACE=1;;
	v) VERBOSE=1; CHATOPT="-v"; PPP_DEBUG="debug";;
	h|?) usage;;
	esac
done

# Do not allow starting the trace if attach to existing CMUX
if [ "$TRACE" -gt 0 ] && [ "$CMUX_ATTACHED" -eq 1 ]; then
	log_err "Error: Trace collection is not supported when attaching to existing CMUX."
	log_err "Please start trace collection in sm2_start_cmux.sh."
	exit 1
fi

if [ "$CID" -gt 0 ]; then
	log_dbg "Using PDN ID: $CID on APN: $APN protocol: $PDP_TYPE"
	export APN
	export CID
	export PDP_TYPE
	CHATSCRIPT="$SCRIPT_DIR/sm2_ppp_dial_pdn.chat"
else
	CHATSCRIPT="$SCRIPT_DIR/sm2_ppp_dial.chat"
fi
export CHATSCRIPT

#
# PPPD options
#
PPP_OPTIONS="
linkname nrf91
local
passive
persist
holdoff 10
nodetach
noauth
noipdefault
+ipv6
noremoteip
defaultroute
defaultroute-metric -1
lcp-echo-interval 0
$PPP_DEBUG
"

CONNECT_CMD="/usr/sbin/chat $CHATOPT -E -f $CHATSCRIPT"

require_serial_port_or_exit

# Remove stale PID files if processes are not running
remove_stale_pidfile "$PIDFILE"
if [ "$TRACE" -gt 0 ]; then
	remove_stale_pidfile "$TRACE_PID_FILE"
fi

if [ "$CMUX_ATTACHED" -eq 0 ]; then
	check_no_existing_cmux_or_exit
else
	check_existing_cmux_or_exit
fi

cleanup() {
	set +eu
	pkill pppd
	if [ "$CMUX_ATTACHED" -eq 0 ]; then
		trace_stop
		pkill ldattach
		cmux_close
	fi
	log_err "Failed to start..."
	exit 1
}

trap cleanup ERR

if [ "$CMUX_ATTACHED" -eq 0 ]; then
	configure_serial_port

	wait_modem_ready_or_exit

	if [ "$IPR_BAUD" -ne 0 ]; then
		set_modem_baud "$IPR_BAUD"
	fi

	cmux_attach "AT+CMUX=0" 3
else
	cmux_dlc_discover 3
fi

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

shutdown_modem() {
	set +eu
	check_devices_or_exit "$DLC1" "$DLC2" "$DLC3"
	if [ "$CMUX_ATTACHED" -eq 0 ]; then
		trace_stop
	fi
	# shellcheck disable=SC2086,SC2094 # CHATOPT must not be quoted, it may be empty
	chat $CHATOPT -t5 '' $SM_SHUTDOWN_SCRIPT >"$DLC1" <"$DLC1"
	CHAT_ERR=$?
	if [ "$CMUX_ATTACHED" -eq 0 ]; then
		pkill ldattach
	fi
	sleep 1
	if [ "$CHAT_ERR" -ne 0 ] && [ -c "$MODEM" ]; then
		cmux_close
		# shellcheck disable=SC2086,SC2094
		chat $CHATOPT -t5 '' $SM_SHUTDOWN_SCRIPT >"$MODEM" <"$MODEM"
	fi
	if [ "$IPR_BAUD" -ne 0 ]; then
		restore_modem_baud "$BAUD"
	fi
}

ppp_start() {
	set +eu
	set -x
	check_devices_or_exit "$DLC1" "$DLC2" "$DLC3"
	# shellcheck disable=SC2086 # PPP_OPTIONS is a list of options
	pppd "$DLC1" ${PPP_OPTIONS} connect "${CONNECT_CMD}"
	if [ "$CMUX_ATTACHED" -eq 0 ]; then
		log_inf "pppd terminated, shutting down modem..."
		shutdown_modem
	else
		log_inf "pppd terminated"
	fi
	test -O "$PIDFILE" && rm -f "$PIDFILE"
}

export CMUX_ATTACHED
export DLC1
export DLC2
export DLC3
export MODEM
export PPP_OPTIONS
export CONNECT_CMD
export PIDFILE
export PPP_PIDFILE
export CHATOPT
export BAUD
export IPR_BAUD
export TRACE
export TRACE_PID_FILE
export MODEM_TRACE_FILE
export TIMEOUT
export VERBOSE
export COMMON_FUNCTIONS
export -f ppp_start
export -f shutdown_modem

log_inf "Connect and wait for PPP link..."

# Start PPPD in a subshell
# Logs go to syslog so redirect output to /dev/null
setsid bash -c "source \"\$COMMON_FUNCTIONS\"; ppp_start" </dev/null >/dev/null 2>&1 &
echo $! > "$PIDFILE"

if wait_for_ppp "$TIMEOUT"; then
	exit 0
fi

log_err "Failed to start PPP link"
cleanup
