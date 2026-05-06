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

|SM| outputs application logs over ``UART1`` (VCOM1 on the nRF9151 DK) by default.
The UART is kept suspended at startup and activated at runtime using the ``AT#XLOG=1`` command.
This avoids any UART power overhead when logs are not needed.
See :ref:`SM_AT_trace` for the full command reference.

.. note::
   The negative error codes that are visible in logs are *errno* codes defined in `nrf_errno.h`_.

The default logging level for the |SM| is ``CONFIG_SM_LOG_LEVEL_INF``.
You can get more verbose logs by setting the ``CONFIG_SM_LOG_LEVEL_DBG`` Kconfig option.

TF-M logging must use the same UART as the application. For more details, see `shared TF-M logging`_.

.. _sm_logging_rtt:

Enabling RTT logs
=================

RTT logging is disabled by default.
To switch from UART1 back to SEGGER RTT, add the following to your :file:`prj.conf`::

   # Disable UART logging
   CONFIG_UART_CONSOLE=n
   CONFIG_LOG_BACKEND_UART=n

   # Enable Segger RTT
   CONFIG_USE_SEGGER_RTT=y
   CONFIG_RTT_CONSOLE=y
   CONFIG_LOG_BACKEND_RTT=y
   # Optional: increase the buffer so that boot logs are not lost before RTT Viewer connects.
   CONFIG_SEGGER_RTT_BUFFER_SIZE_UP=2048

You can view the RTT logs with an RTT client such as ``J-Link RTT Viewer``.
See `Testing and optimization`_ for instructions.


.. note::

   `nRF9151 anomaly 36`_ locks the debug port when the application reaches a low power state (<3 uA current consumption).
   This takes place when DTR is deasserted and the RTT client, such as J-Link RTT Viewer, is not connected.
   Since the RTT backend relies on the debug port, the RTT client must be connected before the application enters a low power state to avoid this issue.

.. note::

   Modem traces captured through UART are corrupted if application logs through RTT are simultaneously captured.
   When capturing modem traces through UART with the `Cellular Monitor app`_ and simultaneously capturing RTT logs, for example, with J-Link RTT Viewer, the modem trace misses packets, and captured packets might have incorrect information.

   If you need to capture modem traces and RTT logs at the same time, enable HW flow control for modem trace UART (``-DEXTRA_DTC_OVERLAY_FILE=overlay-uart1-hwfc.overlay``).
   Otherwise, you can choose not to capture RTT logs.
   Having only RTT logs enabled does not cause this issue.

.. _sm_logging_uart_backend:

Shared UART log and trace backend
**********************************

The |SM| application routes Zephyr application logs to ``UART1`` (VCOM1 on the nRF9151 DK) by default.
The baud rate of the shared UART is set to 1000000 to support the high data rate required for modem traces.

Configuration
=============

Application log output over ``UART1`` is included in the default build.

To also enable the modem trace backend (``AT#XTRACE``), build with the Kconfig overlay:

.. code-block:: console

   west build -p -b nrf9151dk/nrf9151/ns -- -DEXTRA_CONF_FILE="overlay-trace-backend-uart.conf"

After flashing, the UART is suspended at startup.
Use ``AT#XLOG=1`` to activate application logs and ``AT#XTRACE=1`` to activate modem traces.
See :ref:`SM_AT_trace` for the full command reference.

.. note::

   Attempting to enable one backend while the other is active returns ``ERROR``.
   Always disable the active backend before enabling the other.

.. note::

   When measuring power consumption, make sure both backends are disabled (``AT#XLOG=0`` and ``AT#XTRACE=0``) so the UART stays suspended.
   An active trace or log backend adds approximately 700 uA overhead.

Collecting traces
=================

To collect modem traces using the shared UART backend:

1. Connect the `Cellular Monitor app`_ to ``UART1`` (VCOM1 on the nRF9151 DK) at 1000000 baud rate.
#. Enable modem trace collection:

   .. code-block:: console

      AT#XTRACE=1
      OK

#. Activate the modem to start generating trace data:

   .. code-block:: console

      AT+CFUN=1
      OK

#. Wait for the modem to register on the network and collect the traces in the `Cellular Monitor app`_.
#. When done, disable the modem trace backend:

   .. code-block:: console

      AT#XTRACE=0
      OK

You can analyze the captured trace file in the `Cellular Monitor app`_ to inspect AT commands, network events, and IP-level details.
If the modem crashes and the crash dump collection was enabled, you can send the trace file to `Nordic Semiconductor support <DevZone_>`_ for further analysis.

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

If the modem crashes and the crash dump collection was enabled, you can send the trace file to `Nordic Semiconductor support <DevZone_>`_ for further analysis.
