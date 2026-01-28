.. _sm_as_linux_modem:

nRF91 Series SiP as a modem for Linux device
############################################

.. contents::
   :local:
   :depth: 2

Overview
********

You can use the |SM| (SM) application to make an nRF91 Series SiP work as a standalone modem that can be used with a Linux device.
The Linux device can use a standard PPP daemon and ldattach utility to connect to the cellular network through the nRF91 Series SiP.

The setup differentiates from a typical dial-up modem connection as the GSM 0710 multiplexer protocol (CMUX) is used to multiplex multiple data streams over a single serial port.
This allows you to use the same serial port for AT commands, PPP data, and modem traces.

Prerequisites
=============

The Linux device needs to have the following packages installed:

* ``pppd`` from the ppp package
* ``ldattach`` from the util-linux package

These should be available on all standard Linux distributions.

Configuration
=============

You can adjust the serial port baud rate using the devicetree overlay file.
The `baud rate is set to 115200 <Testing and optimization_>`_ by default.
If you change the baud rate in the devicetree, use the ``-b`` parameter with the :file:`scripts/sm_start_ppp.sh` script to specify the matching baud rate.

.. note::
   The standard ``ldattach`` utility sets MRU and MTU to 127 bytes.
   This is hard-coded and cannot be changed.
   If you change the |SM| configuration, make sure that the ``CONFIG_MODEM_CMUX_MTU`` Kconfig option is set to 127 bytes.
   This is already configured in the :file:`overlay-cmux.conf` configuration overlay.

Building and running
====================

To build and program the |SM| application to the nRF91 Series device, use the :file:`overlay-ppp.conf` and the :file:`overlay-cmux.conf` configuration overlays.

Managing the connection
***********************

The start and stop scripts are provided in the :file:`scripts` directory of the |SM| application.

To start the PPP connection, run the :file:`scripts/sm_start_ppp.sh` script.
To stop the PPP connection, run the :file:`scripts/sm_stop_ppp.sh` script.

The start script accepts command-line parameters to configure the connection.
Run ``sm_start_ppp.sh -h`` to see all available options.
By default, the script uses ``/dev/ttyACM0`` at 115200 baud.

The scripts need superuser privileges to run, so use ``sudo``.
The PPP link is set as a default route if there is no existing default route.
The scripts do not manage the DNS settings from the Linux system.
Read the distribution manuals to learn how to configure the DNS settings.

The following example shows how to start the connection and verify its operation with various command-line utilities:

.. code-block:: shell

   $ sudo scripts/sm_start_ppp.sh
   Connect and wait for PPP link...
   PPP link started

   $ ip addr show ppp0
   7: ppp0: <POINTOPOINT,MULTICAST,NOARP,UP,LOWER_UP> mtu 1464 qdisc fq_codel state UNKNOWN group default qlen 3
      link/ppp
      inet 10.139.130.66/32 scope global ppp0
         valid_lft forever preferred_lft forever
      inet6 2001:14bb:69b:50a3:ade3:2fce:6cc:ba3c/64 scope global temporary dynamic
         valid_lft 604720sec preferred_lft 85857sec
      inet6 2001:14bb:69b:50a3:40f9:1c4e:7231:638b/64 scope global dynamic mngtmpaddr
         valid_lft forever preferred_lft forever
      inet6 fe80::40f9:1c4e:7231:638b peer fe80::3c29:6401/128 scope link
         valid_lft forever preferred_lft forever

   $ ping -I ppp0 8.8.8.8 -c5
   PING 8.8.8.8 (8.8.8.8) from 10.139.130.66 ppp0: 56(84) bytes of data.
   64 bytes from 8.8.8.8: icmp_seq=1 ttl=60 time=320 ms
   64 bytes from 8.8.8.8: icmp_seq=2 ttl=60 time=97.6 ms
   64 bytes from 8.8.8.8: icmp_seq=3 ttl=60 time=140 ms
   64 bytes from 8.8.8.8: icmp_seq=4 ttl=60 time=132 ms
   64 bytes from 8.8.8.8: icmp_seq=5 ttl=60 time=145 ms

   --- 8.8.8.8 ping statistics ---
   5 packets transmitted, 5 received, 0% packet loss, time 4007ms
   rtt min/avg/max/mdev = 97.610/166.802/319.778/78.251 ms

   $ iperf3 -c ping.online.net%ppp0 -p 5202
   Connecting to host ping.online.net, port 5202
   [  5] local 10.139.130.66 port 54244 connected to 51.158.1.21 port 5202
   [ ID] Interval           Transfer     Bitrate         Retr  Cwnd
   [  5]   0.00-1.00   sec  0.00 Bytes  0.00 bits/sec    1   17.6 KBytes
   [  5]   1.00-2.00   sec  0.00 Bytes  0.00 bits/sec    0   25.8 KBytes
   [  5]   2.00-3.00   sec  0.00 Bytes  0.00 bits/sec    0   32.5 KBytes
   [  5]   3.00-4.00   sec   128 KBytes  1.05 Mbits/sec    0   35.2 KBytes
   [  5]   4.00-5.00   sec  0.00 Bytes  0.00 bits/sec    0   35.2 KBytes
   [  5]   5.00-6.00   sec  0.00 Bytes  0.00 bits/sec    0   35.2 KBytes
   [  5]   6.00-7.00   sec  0.00 Bytes  0.00 bits/sec    0   35.2 KBytes
   [  5]   7.00-8.00   sec  0.00 Bytes  0.00 bits/sec    0   35.2 KBytes
   [  5]   8.00-9.00   sec  0.00 Bytes  0.00 bits/sec    0   35.2 KBytes
   [  5]   9.00-10.00  sec  0.00 Bytes  0.00 bits/sec    0   35.2 KBytes
   - - - - - - - - - - - - - - - - - - - - - - - - -
   [ ID] Interval           Transfer     Bitrate         Retr
   [  5]   0.00-10.00  sec   128 KBytes   105 Kbits/sec    1             sender
   [  5]   0.00-11.58  sec  89.5 KBytes  63.3 Kbits/sec                  receiver

   $ sudo scripts/sm_stop_ppp.sh
   Stopping PPP link...
   Waiting for Shutdown script to complete...

.. _sm_modem_trace_cmux:

Collecting modem traces through CMUX backend
********************************************

The |SM| application supports collecting modem traces through the CMUX multiplexer.
When enabled, modem traces are sent through a dedicated CMUX channel, allowing simultaneous AT commands, PPP data, and trace collection over the same serial port.
The trace CMUX channel is the first channel after the AT command channel and the PPP channel.

Configuration
=============

To use the CMUX trace backend, build the |SM| application with the trace backend configuration overlay in addition to the PPP and CMUX overlays:

.. code-block:: console

   west build -p -b nrf9151dk/nrf9151/ns -- -DEXTRA_CONF_FILE="overlay-ppp.conf;overlay-cmux.conf;overlay-trace-backend-cmux.conf"

For optimal throughput and to minimize trace data loss, configure the UART to run at maximum speed:

1. Set the UART speed in your device tree configuration (for example, 1000000 baud).
#. Use the ``-b`` parameter with the script to match this speed (for example, ``-b 1000000``).

.. note::
   Some trace data will be dropped.
   The amount depends on the UART speed, ongoing modem operations, and the trace level set with ``AT%XMODEMTRACE``.

Setting trace level
===================

Configure the modem trace level using the `AT%XMODEMTRACE <xmodemtrace_>`_ command.

The modem trace subsystem automatically sends the ``AT%XMODEMTRACE=1,2`` command at startup.
This provides the most trace data and includes the crash dump collection.
Depending on the drop rate you observe, you might need to select a different ``<set_id>`` to reduce the amount of trace data generated.

Collecting traces with the script
=================================

The :file:`sm_start_ppp.sh` script includes support for collecting modem traces.
Use the ``-T`` flag to enable trace collection.
Traces are saved to the :file:`/var/log/nrf91-modem-trace.bin` file.
The trace collection starts after the CMUX channel is established and continues until you stop the connection with the :file:`sm_stop_ppp.sh` script.

The stop script automatically terminates trace collection and preserves the trace file for later analysis.

.. code-block:: console

   # Start PPP connection with trace collection with baud rate matching the device tree setting
   $ sudo scripts/sm_start_ppp.sh -b 1000000 -T
   Trace file: /var/log/nrf91-modem-trace.bin
   Connect and wait for PPP link...
   PPP link started

   # Check that the trace is being collected
   $ ls -la /var/log/nrf91-modem-trace.bin
   -rw-r--r-- 1 root root 3467306 Jan 16 12:13 /var/log/nrf91-modem-trace.bin

   # Stop PPP connection (also stops trace collection)
   $ sudo scripts/sm_stop_ppp.sh
   Stopping PPP link...
   Waiting for Shutdown script to complete...
   Stopping trace collection...


You can open the :file:`/var/log/nrf91-modem-trace.bin` file using the `Cellular Monitor app`_ for analysis.
This allows you to see the AT commands, network, and IP-level details of the communication between the modem and the cellular network.

If the modem crashes and the crash dump collection was enabled, you can send the trace file to Nordic Semiconductor support for further analysis.
