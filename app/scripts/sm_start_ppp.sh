#!/bin/bash -eu
#
# Copyright (c) 2025 Nordic Semiconductor ASA
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause

#
# Script to start PPP link inside CMUX channel
# using Serial Modem application on nRF91
#
# PPPD process is kept running until killed by sm_stop_ppp.sh or "poff"
# On all failures, except SIGTEM and SIGINT, the script will restart the PPPD
# process until killed.
#
# You can copy this script into /etc/ppp/ppp_on_boot and then the connection
# will be started by "pon" command.
# NOTE: Modern Linux systems don't start pppd, so /etc/ppp/ppp_on_boot is only used
# by legacy "pon" command.
#
# Connection is closed by running "sm_stop_ppp.sh" script or using "poff" command.
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
TYPE="IPV4V6"
PDN=0

usage() {
	echo "Usage: $0 [-s serial_port] [-b baud_rate] [-B new_speed] [-t timeout] [-a APN]"
	echo "          [-f IP|IPV6|IPV4V6] [-p PDN] [-T] [-v] [-h]"
	echo ""
	echo "  -s serial_port : Serial port where the modem is connected (default: $MODEM)"
	echo "  -b baud_rate   : Current baud rate of Serial Modem (default: $BAUD)"
	echo "  -B new_speed   : Use AT+IPR to change baud rate to <new_speed>"
	echo "                   Start with current baud rate and switch to new_speed after modem"
	echo "                   is responsive. If not set, baud rate will not be changed."
	echo "                   When terminated, baud rate will be switched back to original."
	echo "  -t timeout     : Timeout for dialup commands in seconds (default: $TIMEOUT)"
	echo "  -a APN         : Access Point Name for cellular connection (default: $APN)"
	echo "  -f FAMILY      : PDP_type, one of IP, IPV6, IPV4V6 (default: $TYPE)"
	echo "  -p PDN         : PDN ID to use (default: $PDN), 0 means use default PDN"
	echo "  -T             : Enable modem trace collection (file: $MODEM_TRACE_FILE)"
	echo "  -v             : Enable verbose output"
	echo "  -h             : Show this help message"
	echo ""
	exit 0
}

# Parse command line parameters
while getopts s:b:B:t:a::f:p:Thv flag
do
	case "${flag}" in
	s) MODEM=${OPTARG};;
	b) BAUD=${OPTARG};;
	B) IPR_BAUD=${OPTARG};;
	t) TIMEOUT=${OPTARG};;
	a) APN=${OPTARG};;
	p) PDN=${OPTARG};;
	f) TYPE=${OPTARG};;
	T) TRACE=1;;
	v) VERBOSE=1; CHATOPT="-v"; PPP_DEBUG="debug";;
	h|?) usage;;
	esac
done

if [ "$PDN" -gt 0 ]; then
	log_dbg "Using PDN ID: $PDN on APN: $APN"
	PDN_CMD="AT+CGDCONT=$PDN,\"$TYPE\",\"$APN\" OK"
	PDN_DIAL="AT+CGACT=1,$PDN OK"
	PPP_DIAL="AT#XPPP=1,1 #XPPP:\s1,0,1"
else
	PDN_CMD=""
	PDN_DIAL=""
	PPP_DIAL="AT#XPPP=1 #XPPP:\s1,0"
fi

#
# Dial up Chat script for nRF91 Serial Modem
#
# Connects to the cellular network and starts PPP link
# on separate CMUX channel.
#
CHAT_SCRIPT=(
ABORT ERROR
ABORT +CME\sERROR:
ABORT +CEER:
ABORT +CNEC_EMM:
ABORT +CNEC_ESM:
''
AT+CFUN=4 OK
$PDN_CMD
AT+CGEREP=1 OK
AT+CFUN=1 '+CGEV: ME PDN ACT 0'
$PDN_DIAL
$PPP_DIAL
)

#
# PPPD options
#
PPP_OPTIONS="
linkname nrf91
local
passive
persist
holdoff 5
nodetach
noauth
novj
nodeflate
nobsdcomp
noipdefault
+ipv6
noremoteip
defaultroute
defaultroute-metric -1
lcp-echo-interval 0
asyncmap 0xffffffff
$PPP_DEBUG
"

require_serial_port_or_exit

# Remove stale PID files if processes are not running
remove_stale_pidfile "$PIDFILE"
remove_stale_pidfile "$TRACE_PID_FILE"

check_no_existing_cmux_or_exit

cleanup() {
	set +eu
	trace_stop
	pkill pppd
	pkill ldattach
	cmux_close
	log_err "Failed to start..."
	exit 1
}

trap cleanup ERR

configure_serial_port

wait_modem_ready_or_exit

if [ "$IPR_BAUD" -ne 0 ]; then
	set_modem_baud "$IPR_BAUD"
fi

cmux_attach "AT#XCMUX=1" 2

# DLC 1: AT command channel for host
# DLC 2: PPP data channel
# DLC 3: Trace channel
AT_CMUX=${DLCS[0]}
PPP_CMUX=${DLCS[1]}
log_inf "DLC 1 (AT):        $AT_CMUX"
log_inf "DLC 2 (PPP):       $PPP_CMUX"

if [ "$TRACE" -gt 0 ]; then
	if [ ${#DLCS[@]} -lt 3 ]; then
		log_err "Error: CMUX trace device (/dev/gsmtty*) not found"
		exit 1
	fi
	MT_CMUX=${DLCS[2]}
	log_inf "DLC 3 (TRACE):     $MT_CMUX"
	trace_start "$MT_CMUX"
fi

log_inf "Connect and wait for PPP link..."
# shellcheck disable=SC2086,SC2094 # CHATOPT must not be quoted, it may be empty
chat $CHATOPT -t"$TIMEOUT" "${CHAT_SCRIPT[@]}" >"$AT_CMUX" <"$AT_CMUX"

shutdown_modem() {
	set +eu
	check_devices_or_exit "$AT_CMUX" "$PPP_CMUX"
	trace_stop
	# shellcheck disable=SC2086,SC2094 # CHATOPT must not be quoted, it may be empty
	chat $CHATOPT -t5 '' $SM_SHUTDOWN_SCRIPT >"$AT_CMUX" <"$AT_CMUX"
	CHAT_ERR=$?
	pkill ldattach
	sleep 1
	if [ "$CHAT_ERR" -ne 0 ]; then
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
	check_devices_or_exit "$AT_CMUX" "$PPP_CMUX"
	# shellcheck disable=SC2086 # PPP_OPTIONS is a list of options
	pppd "$PPP_CMUX" $PPP_OPTIONS
	if [ "$?" -eq 5 ]; then
		log_inf "pppd terminated with signal, shutting down modem..."
		shutdown_modem
		test -O "$PIDFILE" && rm -f "$PIDFILE"
		exit 0
	fi
	sleep 1
	# restart PPP
	ppp_start
}

export PPP_CMUX
export AT_CMUX
export MODEM
export PPP_OPTIONS
export PIDFILE
export PPP_PIDFILE
export CHATOPT
export BAUD
export IPR_BAUD
export TRACE_PID_FILE
export VERBOSE
export COMMON_FUNCTIONS
export -f ppp_start
export -f shutdown_modem

# Start PPPD in a subshell
# Logs go to syslog so redirect output to /dev/null
setsid bash -c "source \"\$COMMON_FUNCTIONS\"; ppp_start" </dev/null >/dev/null 2>&1 &
echo $! > "$PIDFILE"

# Wait for PPPD to start
if wait_for_ppp 5; then
	exit 0
fi

log_err "Failed to start PPP link"
cleanup
