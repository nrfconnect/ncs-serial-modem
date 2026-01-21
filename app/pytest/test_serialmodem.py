# Copyright (c) 2026 Nordic Semiconductor ASA
#
# SPDX-License-Identifier: Apache-2.0

#
# Simple smoke tests for Serial Modem using pytest and Twister Harness.
# Requires Linux host with sudo privileges to run PPP tests.
#
# Run using:
# twister -p nrf9151dk/nrf9151/ns --device-testing --device-serial /dev/ttyACM0 -T . -t pytest -S
#
# optionally add: -c -vv --inline-logs --log-level debug
#

import logging
import re
import subprocess
import time
import pytest

from twister_harness import DeviceAdapter
from twister_harness.exceptions import TwisterHarnessTimeoutException

logger = logging.getLogger(__name__)

def at(dut: DeviceAdapter, cmd: str, expect: str = r"^OK$", timeout: float | None = None):
    """Helper function to send AT command and check for expected response."""
    logger.info(f'Send: {cmd}')
    dut.write(cmd.encode() + b'\r\n')
    lines = dut.readlines_until(regex=expect, timeout=timeout)
    if expect != r"^OK$" and not any(re.match(r"^OK$", line) for line in lines):
        # Flush any remaining lines until OK
        try:
            dut.readlines_until("^OK$", timeout=0.5)
        except TwisterHarnessTimeoutException:
            pass
    return lines

@pytest.hookimpl(tryfirst=True)
def test_Ready(dut: DeviceAdapter):
    """ Verify that DUT outputs 'Ready' on startup. """
    dut.readlines_until(regex='Ready')

def test_AT(dut: DeviceAdapter):
    """ Basic AT command test. """
    at(dut, "AT")

def test_XSMVER(dut: DeviceAdapter):
    """ Test AT#XSMVER command to get Serial Modem version. """
    lines = at(dut, "AT#XSMVER")
    version_line = [line for line in lines if line.startswith('#XSMVER:')]
    assert version_line, 'No +XSMVER response received'
    version = version_line[0].split(':')[1].strip()
    logger.info(f'Serial Modem Version: {version}')
    assert version, 'Empty version string received'

def test_XRESET(dut: DeviceAdapter):
    """ Test AT#XRESET command to reset the app. """
    at(dut, "AT#XRESET", expect="Ready", timeout=5)

@pytest.fixture(scope='module')
def connected(dut: DeviceAdapter):
    """Fixture that connects the DUT to network before tests.
        After the tests, send AT+CFUN=0 to disconnect.
    """
    at(dut, "AT+CGEREP=1")
    at(dut, "AT+CFUN=1", expect=r'\+CGEV: ME PDN ACT', timeout=30)
    yield
    at(dut, "AT+CFUN=0")

def test_tcp(dut: DeviceAdapter, connected):
    """ Test TCP connection using AT#XSOCKET commands.
        Try to connect to example.com on port 80 and send a simple HTTP GET request.
    """
    lines = at(dut, "AT#XSOCKET=1,1,0")
    line = [line for line in lines if line.startswith('#XSOCKET:')]
    assert line
    socket_id = line[0].split(':')[1].strip().split(',')[0]
    logger.info(f'Opened TCP socket with ID: {socket_id}')
    at(dut, f'AT#XCONNECT=0,"example.com",80', expect='#XCONNECT: 0,1', timeout=10)
    at(dut, f'AT#XSEND=0,0,0,"HEAD / HTTP/1.1"')
    at(dut, f'AT#XSEND=0,1,0,"0d0a"')
    at(dut, f'AT#XSEND=0,0,0,"Host: example.com"')
    at(dut, f'AT#XSEND=0,1,0,"0d0a"')
    at(dut, f'AT#XSEND=0,0,0,"Connection: close"')
    at(dut, f'AT#XSEND=0,1,0,"0d0a"')
    at(dut, f'AT#XSEND=0,1,0,"0d0a"')

    lines = at(dut, 'AT#XRECV=0,0,0,1024', timeout=10)
    assert any("HTTP/1.1 200 OK" in line for line in lines)

    at(dut, 'AT#XCLOSE=0', expect='#XCLOSE: 0,0', timeout=5)

def test_ppp(dut: DeviceAdapter):
    """ Test PPP connection using external scripts.
        This test assumes that the scripts 'sm_start_ppp.sh' and 'sm_stop_ppp.sh'
        are available and user executing the test has sudo privileges.

        Serial connection to the DUT will be closed during the PPP session and reopened afterwards.
    """
    result = subprocess.run(['sudo', 'true'])
    if result.returncode != 0:
        pytest.skip("User cannot execute sudo commands")

    dut.close()
    result = subprocess.run(['sudo ./scripts/sm_start_ppp.sh -v -t20'], shell=True)
    assert result.returncode == 0

    result = subprocess.run(['ip link show ppp0'], shell=True)
    assert result.returncode == 0

    result = subprocess.run(['ping -I ppp0 google.com -c 3'], shell=True)
    assert result.returncode == 0

    result = subprocess.run(['sudo ./scripts/sm_stop_ppp.sh'], shell=True)
    assert result.returncode == 0

    time.sleep(1)

    # Reopen the DUT serial connection for further tests
    dut._device_run.set()
    dut._start_reader_thread()
    dut.connect()
    logger.info('Reopened DUT serial connection after PPP session')
    dut.write(b'AT\r\n')
    dut.readlines_until(regex='OK')
