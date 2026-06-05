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

#
# Default parameters
#
MODEM=/dev/ttyACM0
BAUD=115200
IPR_BAUD=0
TIMEOUT=60
APN="internet"
PDP_TYPE="IPV4V6"
CID=0
VERBOSE=0
CHATOPT=""
PPP_DEBUG=""
PIDFILE="/var/run/nrf91-modem.pid"
PPP_PIDFILE="/var/run/ppp-nrf91.pid"
MODEM_TRACE_FILE="/var/log/nrf91-modem-trace.bin"
TRACE_PID_FILE="/var/run/nrf91-modem-trace.pid"
TRACE=0
CMUX_ATTACHED=0

usage() {
    echo "Usage: $0 [-s serial_port] [-b baud_rate] [-B new_speed] [-t timeout] [-a APN]"
    echo "          [-f IP|IPV6|IPV4V6] [-p PDN] [-C] [-T] [-v] [-h]"
    echo ""
    echo "  -s serial_port : Serial port where the modem is connected (default: $MODEM)"
    echo "  -b baud_rate   : Current baud rate of Serial Modem (default: $BAUD)"
    echo "  -B new_speed   : Use AT+IPR to change baud rate to <new_speed>"
    echo "                   Start with current baud rate and switch to new_speed after modem is"
    echo "                   responsive. If not set, baud rate will not be changed."
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

log_dbg() {
    if [ $VERBOSE -eq 1 ]; then
        echo "$@" >&2
        logger --id=$$ "$@"
    fi
}

log_inf() {
    echo "$@" >&2
    logger --id=$$ "$@"
}

# Do not allow starting the trace if attach to existing CMUX
if [ $TRACE -gt 0 ] && [ $CMUX_ATTACHED -eq 1 ]; then
	log_inf "Error: Trace collection is not supported when attaching to existing CMUX."
	log_inf "Please start trace collection in sm2_start_cmux.sh."
	exit 1
fi

if [ $CID -gt 0 ]; then
	log_dbg "Using PDN ID: $CID on APN: $APN protocol: $PDP_TYPE"
	export APN
	export CID
	export PDP_TYPE
	export CHATSCRIPT="$(dirname "$0")/sm2_ppp_dial_pdn.chat"
else
	export CHATSCRIPT="$(dirname "$0")/sm2_ppp_dial.chat"
fi

#
# AT commands to close down CMUX and cellular link
#
SHUTDOWN_SCRIPT="
\d\dAT+CFUN=0 OK
AT#XCMUXCLD OK
"

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


if [[ ! -c $MODEM ]]; then
	log_inf "Serial port not found: $MODEM"
	exit 1
fi

# Remove stale PID files if processes are not running
if [ -f "$PIDFILE" ]; then
	if ! kill -0 $(cat "$PIDFILE" 2>/dev/null) 2>/dev/null; then
		log_dbg "Removing stale PID file: $PIDFILE"
		rm -f "$PIDFILE"
	fi
fi

if [ $TRACE -gt 0 ] && [ -f "$TRACE_PID_FILE" ]; then
	if ! kill -0 $(cat "$TRACE_PID_FILE" 2>/dev/null) 2>/dev/null; then
		log_dbg "Removing stale trace PID file: $TRACE_PID_FILE"
		rm -f "$TRACE_PID_FILE"
	fi
fi

if [ $CMUX_ATTACHED -eq 0 ]; then
	if find /dev -type c -name 'gsmtty*' | grep -q . ; then
		log_inf "Error: existing CMUX devices found (/dev/gsmtty*)"
		exit 1
	fi

	if pgrep ldattach >/dev/null; then
		log_inf "Error: existing ldattach process found"
		exit 1
	fi
else
	if ! find /dev -type c -name 'gsmtty*' | grep -q . ; then
		log_inf "Error: no CMUX devices found (/dev/gsmtty*). Run sm2_start_cmux.sh first."
		exit 1
	fi
fi

cmux_close() {
	printf "\xF9\xF9\xF9\xF9\xF9\xF9\xF9\xF9" > $MODEM
	printf "\xF9\x03\xEF\x05\xC3\x01\xF2\xF9" > $MODEM
	sleep 2
}

stop_trace() {
	if [ $TRACE -gt 0 ]; then
		start-stop-daemon --stop --pidfile $TRACE_PID_FILE --remove-pidfile --oknodo \
				  --retry 1
	fi
}

cleanup() {
	set +eu
	pkill pppd
	if [ $CMUX_ATTACHED -eq 0 ]; then
		stop_trace
		pkill ldattach
		cmux_close
	fi
	log_inf "Failed to start..."
	exit 1
}

trap cleanup ERR

if [ $CMUX_ATTACHED -eq 0 ]; then
	# Configure serial port
	stty -F $MODEM $BAUD pass8 raw crtscts clocal

	log_dbg "Wait modem to boot"
	if chat -t1 "Ready--" "AT" "OK" <$MODEM >$MODEM; then
		log_dbg "Modem is in AT mode"
	else
		log_dbg "Modem not responding, try CMUX close down..."
		cmux_close
		if ! chat -t1 "" "AT" "OK" <$MODEM >$MODEM; then
			log_inf "Error: Modem not responding"
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
	sleep 3
fi

# DLC 1: PPP data channel
# DLC 2: AT command channel for host
# DLC 3: Trace channel
DLC1=$(ls /dev/gsmtty* | sort -V | head -n 1)
DLC2=$(ls /dev/gsmtty* | sort -V | head -n 2 | tail -n 1)
DLC3=$(ls /dev/gsmtty* | sort -V | head -n 3 | tail -n 1)

check_devices_or_exit() {
	# Verify that UART devices are still present
	if [ ! -c $DLC1 ] || [ ! -c $DLC2 ] || [ ! -c $DLC3 ] || [ ! -c $MODEM ]; then
		log_dbg "UART or DLC devices not found."
		log_inf "Error: UART devices not found, exiting..."
		if [ $CMUX_ATTACHED -eq 0 ]; then
			stop_trace
			pkill ldattach
		fi
		exit 1
	fi
}

check_devices_or_exit

log_inf "DLC 1 (PPP):       $DLC1"
log_inf "DLC 2 (AT):        $DLC2"

stty -F $DLC1 clocal

if [ $TRACE -gt 0 ]; then
	log_inf "DLC 3 (TRACE):     $DLC3"
	log_inf "Starting trace collection to $MODEM_TRACE_FILE"
	stty -F $DLC3 raw clocal -icrnl -ixon -opost
	chat $CHATOPT -t1 '' 'AT#XCMUXTRACE=3' 'OK' >$DLC1 <$DLC1

	# Prefer to use socat, if installed.
	if command -v socat >/dev/null 2>&1; then
		start-stop-daemon --start --pidfile $TRACE_PID_FILE --make-pidfile \
			--background --exec $(command -v socat) -- -u $DLC3,cfmakeraw,clocal=1 \
			CREATE:$MODEM_TRACE_FILE
	else
		start-stop-daemon --start --pidfile $TRACE_PID_FILE --make-pidfile \
		--background --exec /bin/dd -- if=$DLC3 of=$MODEM_TRACE_FILE bs=1024
	fi
fi

shutdown_modem() {
	set +eu
	check_devices_or_exit
	if [ $CMUX_ATTACHED -eq 0 ]; then
		stop_trace
	fi
	chat $CHATOPT -t5 '' $SHUTDOWN_SCRIPT >$DLC1 <$DLC1
	CHAT_ERR=$?
	if [ $CMUX_ATTACHED -eq 0 ]; then
		pkill ldattach
	fi
	sleep 1
	if [ "$CHAT_ERR" -ne 0 ]; then
		cmux_close
		chat $CHATOPT -t5 '' $SHUTDOWN_SCRIPT >$MODEM <$MODEM
	fi
	if [ $IPR_BAUD -ne 0 ]; then
		# Restore baud rate on modem
		chat $CHATOPT -t1 '' "AT+IPR=$BAUD" "OK" >$MODEM <$MODEM
		stty -F $MODEM $BAUD
	fi
}

ppp_start() {
	set +eu
	set -x
	check_devices_or_exit
	pppd $DLC1 ${PPP_OPTIONS} connect "${CONNECT_CMD}"
	if [ $CMUX_ATTACHED -eq 0 ]; then
		log_inf "pppd terminated, shutting down modem..."
		shutdown_modem
	else
		log_inf "pppd terminated"
	fi
	test -O $PIDFILE && rm -f $PIDFILE
}

export DLC1
export CMUX_ATTACHED
export MODEM
export SHUTDOWN_SCRIPT
export PPP_OPTIONS
export CONNECT_CMD
export PIDFILE
export CHATOPT
export BAUD
export IPR_BAUD
export TRACE
export TRACE_PID_FILE
export TIMEOUT
export VERBOSE
export -f ppp_start
export -f shutdown_modem
export -f cmux_close
export -f stop_trace
export -f check_devices_or_exit

log_inf "Connect and wait for PPP link..."

# Start PPPD in a subshell
# Logs go to syslog so redirect output to /dev/null
setsid bash -c ppp_start  </dev/null >/dev/null 2>&1 &
echo $! > $PIDFILE

# Wait for PPPD to start
for i in $(seq 1 $TIMEOUT); do
	if [ -f $PPP_PIDFILE ]; then
		if grep "ppp[0-9]" $PPP_PIDFILE >/dev/null; then
			log_inf "PPP link started"
			log_dbg "Interface $(cat $PPP_PIDFILE| tail -1)"
			exit 0
		fi
	fi
	sleep 1
done

log_inf "Failed to start PPP link"
cleanup
