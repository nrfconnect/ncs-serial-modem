#!/bin/bash -u
#
# Copyright (c) 2025 Nordic Semiconductor ASA
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause

#
# Script to stop PPP link inside CMUX channel
# using Serial Modem
#

MODEM=/dev/ttyACM0
AT_CMUX=$(ls /dev/gsmtty* | sort -V | head -n 1)
CHATOPT="-vs"
TIMEOUT=30

# Parse command line parameters
while getopts s:b:t:h flag
do
    case "${flag}" in
	s) MODEM=${OPTARG};;
	t) TIMEOUT=${OPTARG};;
	h|?) echo "Usage: $0 [-s serial_port] [-t timeout]"; exit 0;;
    esac
done

if [[ ! -c $AT_CMUX ]]; then
	echo "AT CMUX channel not found: $AT_CMUX"
	pkill pppd
	pkill ldattach
	exit 1
fi

test -f /var/run/ppp-nrf91.pid && kill -SIGTERM $(head -1 </var/run/ppp-nrf91.pid)

chat $CHATOPT -t$TIMEOUT "" "AT+CFUN=0" "OK" "AT#XCMUXCLD" "OK" >$AT_CMUX <$AT_CMUX

sleep 1
test -f /var/run/ppp-nrf91.pid && kill -KILL $(head -1 </var/run/ppp-nrf91.pid)
pkill ldattach
