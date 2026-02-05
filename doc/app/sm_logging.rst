.. _sm_logging:

Logging
#######

.. contents::
   :local:
   :depth: 3

The application MCU and the modem produce separate logs with their own methods.
Logs often refer to the |SM| application logging while the modem logs are referred to as modem traces in many places.

Application logging
*******************

|SM| uses the SEGGER Real-Time Transfer (RTT) for application logging by default.
You can view the RTT logs with an RTT client such as ``J-Link RTT Viewer``.
See `Testing and optimization`_ for instructions on how to view the logs.

.. note::
   The negative error codes that are visible in logs are *errno* codes defined in `nrf_errno.h`_.

By default, the |SM| uses the ``UART0`` for sending and receiving AT commands.
If a different UART is used, the application log can be output through ``UART0`` while AT commands are sent and received through the other UART.
To switch to ``UART0`` output for application logs, change the following options in the :file:`prj.conf` file::

   # Segger RTT
   CONFIG_USE_SEGGER_RTT=n
   CONFIG_RTT_CONSOLE=n
   CONFIG_UART_CONSOLE=y
   CONFIG_LOG_BACKEND_RTT=n
   CONFIG_LOG_BACKEND_UART=y

The default logging level for the |SM| is ``CONFIG_SM_LOG_LEVEL_INF``.
You can get more verbose logs by setting the ``CONFIG_SM_LOG_LEVEL_DBG`` Kconfig option.

TF-M logging must use the same UART as the application. For more details, see `shared TF-M logging`_.

Modem traces
************

To send modem traces over UART on an nRF91 Series DK, configuration must be added for the UART device in the devicetree and Kconfig.
This is done by adding the `modem trace UART snippet`_ (``-Dapp_SNIPPET=nrf91-modem-trace-uart``) when building and programming.

Use the `Cellular Monitor app`_ for capturing and analyzing modem traces.

.. note::

   When measuring power consumption while the modem is active, the modem traces must not be active.
   If the build includes modem traces, the traces must be deactivated with ``AT%XMODEMTRACE=0``, before taking measurements.
   Active traces show approximately 700 uA overhead on the power consumption.

   If ``hw-flow-control`` is enabled for the trace UART in the devicetree (it is disabled by default), the same approximately 700 uA overhead is present even when the traces are deactivated using ``AT%XMODEMTRACE=0``.
   ``hw-flow-control`` prevents the trace UART from sending, when there is no reader for the data.
   This prevents the trace UART from suspending, as it cannot empty its buffers.

.. note::

   Modem traces captured through UART are corrupted if application logs through RTT are simultaneously captured.
   When capturing modem traces through UART with the `Cellular Monitor app`_ and simultaneously capturing RTT logs, for example, with J-Link RTT Viewer, the modem trace misses packets, and captured packets might have incorrect information.

   If you need to capture modem traces and RTT logs at the same time, enable HW flow control for modem trace UART.
   This can be done for the `nRF9151 DK <nrf9151dk_>`_ by adding the following change to :file:`app/boards/nrf9151dk_nrf9151_ns.overlay`:

   .. parsed-literal::
      :class: highlight

      &uart1 {
       hw-flow-control;
      };

   Otherwise, you can choose not to capture RTT logs.
   Having only RTT logs enabled does not cause this issue.

.. _sm_modem_trace_cmux:

Modem traces through CMUX
*************************

The |SM| application supports collecting modem traces through the CMUX multiplexer.
When enabled, modem traces are sent through a dedicated CMUX channel, allowing simultaneous AT commands, PPP data, and trace collection over the same serial port.
The trace CMUX channel is the first channel after the AT command channel and the PPP channel.

Configuration
=============

To use the CMUX trace backend, build the |SM| application with the trace backend configuration overlay in addition to the PPP and CMUX overlays:

.. code-block:: console

   west build -p -b nrf9151dk/nrf9151/ns -- -DEXTRA_CONF_FILE="overlay-ppp.conf;overlay-cmux.conf;overlay-trace-backend-cmux.conf"

For optimal throughput and to minimize trace data loss, configure the UART to run at maximum speed:

1. Set the UART speed in your devicetree configuration (for example, 1000000 baud).
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

Collecting traces through CMUX on Linux
=======================================

The :file:`sm_start_ppp.sh` script for Linux host includes support for collecting modem traces.
Use the ``-T`` flag to enable trace collection.
Traces are saved to the :file:`/var/log/nrf91-modem-trace.bin` file.
The trace collection starts after the CMUX channel is established and continues until you stop the connection with the :file:`sm_stop_ppp.sh` script.

The stop script automatically terminates trace collection and preserves the trace file for later analysis.

.. code-block:: console

   # Start PPP connection with trace collection with baud rate matching the devicetree setting
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
