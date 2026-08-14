# shellcheck shell=bash
#
# Copyright (c) 2026 Nordic Semiconductor ASA
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause

#
# Common functions shared by the Serial Modem host scripts.
#
# This file is meant to be sourced, not executed:
#
#     SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
#     source "$SCRIPT_DIR/common_functions.sh"
#
# Sourcing has no side effects other than assigning default values to the
# shared parameters below. Any variable already set by the caller is kept,
# so defaults can be overridden before sourcing.
#
# The functions do not rely on "set -e" being active. Failures are reported
# through the return value unless the function name ends with "_or_exit".
#

if [ -n "${SM_COMMON_FUNCTIONS_SOURCED:-}" ]; then
	return 0 2>/dev/null || exit 0
fi
SM_COMMON_FUNCTIONS_SOURCED=1

#
# Shared default parameters
#
: "${MODEM:=/dev/ttyACM0}"
: "${BAUD:=115200}"
: "${IPR_BAUD:=0}"
: "${VERBOSE:=0}"
: "${CHATOPT:=}"
: "${PPP_DEBUG:=}"
: "${PIDFILE:=/var/run/nrf91-modem.pid}"
: "${PPP_PIDFILE:=/var/run/ppp-nrf91.pid}"
: "${MODEM_TRACE_FILE:=/var/log/nrf91-modem-trace.bin}"
: "${TRACE_PID_FILE:=/var/run/nrf91-modem-trace.pid}"
: "${TRACE:=0}"
# Set to 1 when attaching to a CMUX started by another script. In that case
# CMUX and trace collection are owned by that script and are not torn down here.
: "${CMUX_ATTACHED:=0}"

# CMUX DLC devices, populated by cmux_attach() / cmux_dlc_discover()
DLCS=()

#
# AT commands to close down CMUX and the cellular link
#
# shellcheck disable=SC2034 # used by the scripts sourcing this file
SM_SHUTDOWN_SCRIPT="
\d\dAT+CFUN=0 OK
AT#XCMUXCLD OK
"

#
# Logging
#

# Log a message only when verbose output is enabled.
log_dbg() {
	if [ "$VERBOSE" -eq 1 ]; then
		echo "$@" >&2
		logger --id=$$ "$@"
	fi
}

# Log an informational message to stderr and to syslog.
log_inf() {
	echo "$@" >&2
	logger --id=$$ "$@"
}

# Log an error message to stderr and to syslog.
log_err() {
	echo "$@" >&2
	logger --id=$$ -p user.err "$@"
}

#
# Serial port and PID file helpers
#

# Exit with an error if the given serial port (default: $MODEM) is missing.
# shellcheck disable=SC2120 # all arguments are optional
require_serial_port_or_exit() {
	local port=${1:-$MODEM}

	if [ ! -c "$port" ]; then
		log_err "Error: serial port not found: $port"
		exit 1
	fi
}

# Remove a PID file if the process it refers to is no longer running.
remove_stale_pidfile() {
	local pidfile=$1
	local pid

	[ -f "$pidfile" ] || return 0

	pid=$(head -1 "$pidfile" 2>/dev/null)
	if [ -z "$pid" ] || ! kill -0 "$pid" 2>/dev/null; then
		log_dbg "Removing stale PID file: $pidfile"
		rm -f "$pidfile"
	fi
}

# Configure the serial port for CMUX traffic.
# shellcheck disable=SC2120 # all arguments are optional
configure_serial_port() {
	local port=${1:-$MODEM}
	local baud=${2:-$BAUD}

	stty -F "$port" "$baud" pass8 raw crtscts clocal -hupcl
}

#
# CMUX helpers
#

# Force CMUX close down by sending a raw CLD frame to the serial port.
# shellcheck disable=SC2120 # all arguments are optional
cmux_close() {
	local port=${1:-$MODEM}

	if [ -c "$port" ]; then
		printf "\xF9\xF9\xF9\xF9\xF9\xF9\xF9\xF9" > "$port"
		printf "\xF9\x03\xEF\x05\xC3\x01\xF2\xF9" > "$port"
		sleep 2
	fi
}

# Return 0 if at least one CMUX character device exists.
cmux_devices_exist() {
	find /dev -maxdepth 1 -type c -name 'gsmtty*' | grep -q .
}

# Exit with an error if CMUX is already up. Also removes leftover non-character
# 'gsmtty*' files, which can be a residue of a previously failed run, for
# example 'chat PARAMS... >/dev/gsmtty1 </dev/gsmtty1' after the device
# disappeared.
check_no_existing_cmux_or_exit() {
	if cmux_devices_exist; then
		log_err "Error: existing CMUX devices found (/dev/gsmtty*)"
		exit 1
	fi

	if pgrep ldattach >/dev/null; then
		log_err "Error: existing ldattach process found"
		exit 1
	fi

	if [ -n "$(find /dev -maxdepth 1 ! -type c -name 'gsmtty*' -print -delete)" ]; then
		log_inf "Warning: invalid CMUX devices found (/dev/gsmtty*), removed"
	fi
}

# Exit with an error if CMUX is not up. Used when attaching to an existing CMUX.
check_existing_cmux_or_exit() {
	if ! cmux_devices_exist; then
		log_err "Error: no CMUX devices found (/dev/gsmtty*). Run sm2_start_cmux.sh first."
		exit 1
	fi
}

# Wait for the modem to boot and verify that it responds to AT commands.
# Falls back to a forced CMUX close down if the modem does not respond.
# shellcheck disable=SC2120 # all arguments are optional
wait_modem_ready_or_exit() {
	local port=${1:-$MODEM}

	log_dbg "Wait modem to boot"
	# shellcheck disable=SC2094 # chat needs the same device for input and output
	if chat -t1 "Ready--" "AT" "OK" <"$port" >"$port"; then
		log_dbg "Modem is in AT mode"
		return 0
	fi

	log_dbg "Modem not responding, try CMUX close down..."
	cmux_close "$port"
	# shellcheck disable=SC2094
	if ! chat -t1 "" "AT" "OK" <"$port" >"$port"; then
		log_err "Error: Modem not responding"
		exit 1
	fi
}

# Change the modem baud rate with AT+IPR and reconfigure the serial port.
set_modem_baud() {
	local new_baud=$1
	local port=${2:-$MODEM}

	log_dbg "Set baud rate on modem to $new_baud"
	# shellcheck disable=SC2086,SC2094 # CHATOPT must not be quoted, it may be empty
	chat $CHATOPT -t1 '' "AT+IPR=$new_baud" "OK" >"$port" <"$port"
	configure_serial_port "$port" "$new_baud"
}

# Restore the modem baud rate with AT+IPR and reconfigure the serial port.
# Ignores AT command failures, the port may already be gone.
restore_modem_baud() {
	local orig_baud=$1
	local port=${2:-$MODEM}

	[ -c "$port" ] || return 0

	log_dbg "Restoring baud rate on modem to $orig_baud"
	# shellcheck disable=SC2086,SC2094
	chat $CHATOPT -t1 '' "AT+IPR=$orig_baud" "OK" >"$port" <"$port" || true
	stty -F "$port" "$orig_baud"
}

# Start CMUX on the modem with the given AT command and attach the line
# discipline. Waits for the CMUX channels to appear, then populates and
# configures them, see cmux_dlc_discover().
#
#   cmux_attach <at_command> [min_dlc_count]
#
cmux_attach() {
	local at_cmd=$1
	local min_dlcs=${2:-2}
	local port=${MODEM}

	log_dbg "Attach CMUX channel to modem..."
	# shellcheck disable=SC2086,SC2094
	chat $CHATOPT -t1 '' "$at_cmd" "OK" >"$port" <"$port"
	ldattach GSM0710 "$port"
	log_dbg "Wait for CMUX to open"
	sleep 3
	log_dbg "continue"

	cmux_dlc_discover "$min_dlcs"
}

# Populate the global DLCS array with the CMUX DLC devices, in channel order,
# then verify and configure the first <min_dlc_count> (default 2) channels.
# Exits if fewer than <min_dlc_count> channels are present.
#
# The array is global instead of printed so that the caller keeps the exit
# status, and because Bash cannot export arrays.
cmux_dlc_discover() {
	local min_dlcs=${1:-2}
	local dev

	mapfile -t DLCS < <(cmux_dlc_list)

	if [ ${#DLCS[@]} -lt "$min_dlcs" ]; then
		log_err "Error: CMUX devices (/dev/gsmtty*) not found"
		exit 1
	fi

	check_devices_or_exit "${DLCS[@]:0:$min_dlcs}"

	for dev in "${DLCS[@]:0:$min_dlcs}"; do
		stty -F "$dev" clocal -hupcl
	done
}

# Print the CMUX DLC devices, one per line, in channel order.
cmux_dlc_list() {
	find /dev -maxdepth 1 -type c -name 'gsmtty*' | sort -V
}

# Verify that the given devices, and the modem serial port, are still present.
# Stops trace collection and ldattach and exits on failure. CMUX owned by
# another script ($CMUX_ATTACHED=1) is left untouched.
check_devices_or_exit() {
	local dev

	for dev in "$@" "$MODEM"; do
		if [ ! -c "$dev" ]; then
			log_err "Error: UART devices not found, exiting..."
			if [ "$CMUX_ATTACHED" -eq 0 ]; then
				trace_stop
				pkill ldattach || true
			fi
			exit 1
		fi
	done
}

#
# Modem trace helpers
#

# Start modem trace collection from the given DLC into $MODEM_TRACE_FILE.
trace_start() {
	local dlc=$1

	log_inf "Starting trace collection to $MODEM_TRACE_FILE"
	stty -F "$dlc" raw clocal -icrnl -ixon -opost -hupcl

	# Prefer to use socat, if installed.
	if command -v socat >/dev/null 2>&1; then
		start-stop-daemon --start --pidfile "$TRACE_PID_FILE" --make-pidfile \
			--background --exec "$(command -v socat)" -- \
			-u "$dlc,cfmakeraw,clocal=1,hupcl=0" \
			"CREATE:$MODEM_TRACE_FILE"
	else
		start-stop-daemon --start --pidfile "$TRACE_PID_FILE" --make-pidfile \
			--background --exec /bin/dd -- \
			"if=$dlc" "of=$MODEM_TRACE_FILE" bs=1024
	fi
}

# Stop modem trace collection, if running.
trace_stop() {
	start-stop-daemon -q --stop --pidfile "$TRACE_PID_FILE" --remove-pidfile --oknodo --retry 1
}

#
# PPP helpers
#

# Wait up to <timeout> seconds for pppd to bring the PPP interface up.
# Returns 0 when the link is up, 1 on timeout.
wait_for_ppp() {
	local timeout=$1
	local i

	for ((i = 0; i < timeout; i++)); do
		if [ -f "$PPP_PIDFILE" ] && grep -q "ppp[0-9]" "$PPP_PIDFILE"; then
			log_inf "PPP link started"
			log_dbg "Interface $(tail -1 "$PPP_PIDFILE")"
			return 0
		fi
		sleep 1
	done

	return 1
}

# Request pppd to terminate, if running.
stop_ppp_link() {
	if [ -f "$PPP_PIDFILE" ]; then
		log_inf "Stopping PPP link..."
		start-stop-daemon --stop --pidfile "$PPP_PIDFILE"
	fi
}

# Wait for the modem shutdown script started by the start scripts to complete.
# shellcheck disable=SC2120 # all arguments are optional
wait_for_shutdown_script() {
	local timeout=${1:-12}

	if [ -f "$PIDFILE" ]; then
		log_inf "Waiting for shutdown script to complete..."
		timeout "${timeout}s" tail --pid="$(head -1 "$PIDFILE")" -f /dev/null \
			|| log_inf "Timeout waiting for shutdown script to stop"
	fi
}
