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

#
# Default parameters
#
MODEM=/dev/ttyACM0
BAUD=115200
TIMEOUT=60
APN="internet"
TYPE="IPV4V6"
PDN=0
VERBOSE=0
CHATOPT=""
PPP_DEBUG=""
PIDFILE="/var/run/nrf91-modem.pid"
PPP_PIDFILE="/var/run/ppp-nrf91.pid"

usage() {
    echo "Usage: $0 [-s serial_port] [-b baud_rate] [-t timeout] [-a APN] [-f IP|IPV6|IPV4V6]"
    echo ""
    echo "  -s serial_port : Serial port where the modem is connected (default: $MODEM)"
    echo "  -b baud_rate   : Baud rate for serial communication (default: $BAUD)"
    echo "  -t timeout     : Timeout for dialup commands in seconds (default: $TIMEOUT)"
    echo "  -a APN         : Access Point Name for cellular connection (default: $APN)"
    echo "  -f FAMILY      : PDP_type, one of IP, IPV6, IPV4V6 (default: $TYPE)"
    echo "  -p PDN         : PDN ID to use (default: $PDN), 0 means use default PDN"
    echo "  -v             : Enable verbose output"
    echo "  -h             : Show this help message"
    echo ""
    exit 0
}

# Parse command line parameters
while getopts s:b:t:a::f:p:hv flag
do
    case "${flag}" in
	s) MODEM=${OPTARG};;
	b) BAUD=${OPTARG};;
	t) TIMEOUT=${OPTARG};;
	a) APN=${OPTARG};;
	p) PDN=${OPTARG};;
	f) TYPE=${OPTARG};;
	v) VERBOSE=1; CHATOPT="-v"; PPP_DEBUG="debug";;
	h|?) usage;;
    esac
done

log_dbg() {
    if [ $VERBOSE -eq 1 ]; then
	echo "$@" >&2
    fi
}

if [ $PDN -gt 0 ]; then
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
$PPP_DEBUG
"

if [[ ! -c $MODEM ]]; then
	echo "Serial port not found: $MODEM"
	exit 1
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
	pkill pppd
	pkill ldattach
	printf "\xF9\x03\xEF\x05\xC3\x01\xF2\xF9" > $MODEM
	echo "Failed to start..."
	exit 1
}

trap cleanup ERR

# Configure serial port
stty -F $MODEM $BAUD pass8 raw crtscts clocal

log_dbg "Wait modem to boot"
if chat -t1 "" "AT" "OK" <$MODEM >$MODEM; then
	log_dbg "Modem is in AT mode"
else
	log_dbg "Modem not responding, try CMUX Close down..."
	cmux_close
fi

log_dbg "Attach CMUX channel to modem..."
ldattach -c $'\rAT#XCMUX=1\r' GSM0710 $MODEM

AT_CMUX=$(ls /dev/gsmtty* | sort -V | head -n 1)
PPP_CMUX=$(ls /dev/gsmtty* | sort -V | head -n 2 | tail -n 1)
log_dbg "AT CMUX:  $AT_CMUX"
log_dbg "PPP CMUX: $PPP_CMUX"

sleep 1
stty -F $AT_CMUX clocal

echo "Connect and wait for PPP link..."
test -c $AT_CMUX
chat $CHATOPT -t$TIMEOUT "${CHAT_SCRIPT[@]}" >$AT_CMUX <$AT_CMUX

shutdown_modem() {
	set +eu
	chat $CHATOPT -t5 '' $SHUTDOWN_SCRIPT >$AT_CMUX <$AT_CMUX
	CHAT_ERR=$?
	pkill ldattach
	sleep 1
	if [ "$CHAT_ERR" -ne 0 ]; then
		cmux_close
		chat $CHATOPT -t5 '' $SHUTDOWN_SCRIPT >$MODEM <$MODEM
	fi
}

ppp_start() {
	set +eu
	set -x
	pppd $PPP_CMUX $PPP_OPTIONS
	if [ "$?" -eq 5 ]; then
		echo "pppd terminated with signal, shutting down modem..."
		shutdown_modem
		test -O $PIDFILE && rm -f $PIDFILE
		exit 0
	fi
	sleep 1
	# restart PPP
	ppp_start
}

export PPP_CMUX
export AT_CMUX
export MODEM
export SHUTDOWN_SCRIPT
export PPP_OPTIONS
export PIDFILE
export CHATOPT
export -f ppp_start
export -f shutdown_modem
export -f cmux_close

# Start PPPD in a subshell
# Logs go to syslog so redirect output to /dev/null
setsid bash -c ppp_start  </dev/null >/dev/null 2>&1 &
echo $! > $PIDFILE

# Wait for PPPD to start
for i in {1..5}; do
	if [ -f $PPP_PIDFILE ]; then
		if grep "ppp[0-9]" $PPP_PIDFILE >/dev/null; then
			echo "PPP link started"
			log_dbg "Interface $(cat $PPP_PIDFILE| tail -1)"
			exit 0
		fi
	fi
	sleep 1
done

echo "Failed to start PPP link"
exit 1
