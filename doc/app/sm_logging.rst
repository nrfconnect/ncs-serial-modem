.. _sm_logging:

Logging and tracing
###################

.. contents::
   :local:
   :depth: 3

The |SM| and the modem produce separate logs with their own methods.
The logs of the |SM| are called application logs, and the logs of the modem are called modem traces.

.. application_logging_start

Application logging
*******************

The |SM| outputs B0, MCUboot, and application logs over |log_uart| during boot.
After the application is initialized, the log backend is disabled, and the UART is suspended.
Use ``AT#XLOG=1`` or ``AT#XLOG=2`` to enable application logs and resume the UART.
This avoids UART power overhead when logs are not needed during normal operation.
See :ref:`SM_AT_trace` for the full command reference.

`Nordic Semiconductor support <DevZone_>`_ might ask you for application logs when investigating an issue.

.. only:: not nrf91m1

   The ``zephyr,console`` and ``ncs,sm-uart`` chosen nodes must point to different UARTs.
   The |SM| enables asynchronous reception on the AT command UART and cannot suspend it.
   The build fails if both nodes point to the same UART.

   TF-M logging must use the same UART as the application. For more details, see `shared TF-M logging`_.

.. note::
   The negative error codes that are visible in logs are *errno* codes defined in `nrf_errno.h`_.

.. _sm_logging_levels:

Log levels
==========

The |SM| uses the following log levels, from the most severe to the most verbose:

.. list-table::
   :header-rows: 1
   :widths: auto

   * - Level
     - Usage
   * - Error (ERR)
     - An operation failed and the |SM| cannot complete it.
   * - Warning (WRN)
     - Something unexpected happened, but the |SM| can continue.
   * - Information (INF)
     - Normal operation. Includes the AT commands, responses, and notifications as text, with sensitive payloads redacted.
   * - Debug (DBG)
     - Detailed internal information for debugging. Includes the AT commands, responses, and notifications as raw hex dumps.

.. only:: not nrf91m1

   The |SM| shows a log message only if its level is enabled.
   Enabling a level also enables all the levels above it in the list.
   The log level is set at build time separately for each software component.

   By default, the |SM| logs at the debug level, which is required for ``AT#XLOG=2``.
   To change the level, set one of the ``CONFIG_SM_LOG_LEVEL_ERR``, ``CONFIG_SM_LOG_LEVEL_WRN``, ``CONFIG_SM_LOG_LEVEL_INF``, or ``CONFIG_SM_LOG_LEVEL_DBG`` Kconfig options.
   The other components, such as Zephyr and the |NCS| libraries, log at the information level unless their level is set explicitly.
   To change the level of a component, set its own Kconfig option, for example, the ``CONFIG_MODEM_CMUX_LOG_LEVEL_DBG`` Kconfig option.
   The ``CONFIG_LOG_DEFAULT_LEVEL`` Kconfig option sets the default level for the components that do not set their own.

You select what the |SM| outputs on the log UART at runtime with the ``AT#XLOG`` command:

.. list-table::
   :header-rows: 1
   :widths: auto

   * - ``AT#XLOG`` mode
     - Log UART
     - AT traffic logged as
   * - ``0``
     - Suspended
     - Not output
   * - ``1``
     - Active
     - Information level text, with sensitive payloads redacted
   * - ``2``
     - Active
     - Debug level hex dump

Use mode ``1`` for normal troubleshooting.
Use mode ``2`` only when you need the exact bytes of the AT traffic, for example when the redaction hides the information that you need.
Mode ``2`` outputs sensitive payloads, such as credentials, so do not share these logs publicly.

.. application_logging_end

.. _sm_logging_rtt:

Enabling RTT logs
=================

RTT logging is disabled by default.
To switch from UART1 back to SEGGER RTT, add the following to your :file:`prj.conf`:

::

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

   If you need to capture modem traces and RTT logs at the same time, enable HW flow control for modem trace UART.
   Otherwise, you can choose not to capture RTT logs.
   Having only RTT logs enabled does not cause this issue.

.. modem_traces_start

.. _sm_logging_modem_traces:

Modem traces
************

The modem outputs modem traces that you can use to analyze the behavior of the modem.
Collect the modem traces for the following purposes:

* Inspecting the IP-level traffic during development.
* Analyzing network issues.
* Investigating modem crashes and suspected modem failures with the crash dump.

`Nordic Semiconductor support <DevZone_>`_ might ask you for modem traces when investigating an issue.

Enable the modem traces before the problem occurs, because they cannot be collected afterwards.
If the |SM| reports a modem fault with the :ref:`#XMODEM: FAULT <SM_AT_XMODEM>` notification, or if you suspect another modem failure, repeat the scenario with the modem traces enabled.

.. modem_traces_end

.. shared_uart_backend_start

.. _sm_logging_uart_backend:

Modem traces through UART
=========================

The |SM| outputs modem traces over |log_uart|.
The UART is shared with the application logs.
The baud rate of the shared UART is set to 1000000 to support the high data rate required for modem traces.

Configuration
-------------

.. only:: not nrf91m1

   Application log output over ``UART1`` is included in the default build.

   To also enable the modem trace backend (``AT#XTRACE``), build with the Kconfig overlay and the devicetree overlay that defines the modem trace SRAM partition:

   .. code-block:: console

      west build -p -b nrf9151dk/nrf9151/ns -- -DEXTRA_CONF_FILE="trace-backend-uart.conf" -DEXTRA_DTC_OVERLAY_FILE="trace-backend-nrf9151.overlay"

.. only:: nrf91m1

   The application log output over ``UART1`` and the modem trace backend (``AT#XTRACE``) are both included in the |SM|.

After the application is initialized, the UART is suspended.
Use ``AT#XLOG=1`` to activate application logs and ``AT#XTRACE=1`` to activate modem traces.
See :ref:`SM_AT_trace` for the full command reference.

.. note::

   Attempting to enable one backend while the other is active returns ``ERROR``.
   Always disable the active backend before enabling the other.

.. note::

   When measuring power consumption, make sure both backends are disabled (``AT#XLOG=0`` and ``AT#XTRACE=0``) so the UART stays suspended.
   An active trace or log backend adds approximately 700 uA overhead.

Collecting traces
-----------------

The modem traces cannot be enabled while the application logs are enabled, so send ``AT#XLOG=0`` first.

To collect modem traces using the shared UART backend:

1. Capture the trace data from |log_uart| at 1000000 baud rate, for example, with the `Cellular Monitor app`_.
   See the Cellular Monitor app documentation for how to capture and save the trace.
#. Enable modem trace collection with the AT command UART:

   .. code-block:: console

      AT#XTRACE=1
      OK

#. Activate the modem to start generating trace data:

   .. code-block:: console

      AT+CFUN=1
      OK

#. Wait for the modem to register on the network and collect the traces.
#. When done, disable the modem trace backend:

   .. code-block:: console

      AT#XTRACE=0
      OK

.. shared_uart_backend_end

.. _sm_modem_trace_cmux:

Modem traces through CMUX
=========================

The |SM| application supports collecting modem traces through the CMUX multiplexer.
When enabled, modem traces are sent through a dedicated CMUX channel, allowing simultaneous AT commands, PPP data, and trace collection over the same serial port.
The trace CMUX channel is the first channel after the AT command channel and the PPP channel.

.. note::
   Unlike the :ref:`shared UART backend <sm_logging_uart_backend>`, the CMUX trace backend does not use ``AT#XTRACE``.
   When CMUX is started with ``AT#XCMUX``, the trace channel is attached automatically.
   When CMUX is started with ``AT+CMUX``, or to move the trace output to a different CMUX channel, use ``AT#XCMUXTRACE``.

Configuration
-------------

To use the CMUX trace backend, build the |SM| application with the trace backend configuration overlay in addition to the PPP and CMUX overlays, and the devicetree overlay that defines the modem trace SRAM partition:

.. code-block:: console

   west build -p -b nrf9151dk/nrf9151/ns -- -DEXTRA_CONF_FILE="ppp.conf;cmux.conf;trace-backend-cmux.conf" -DEXTRA_DTC_OVERLAY_FILE="trace-backend-nrf9151.overlay"

For optimal throughput and to minimize trace data loss, configure the UART to run at maximum speed:

1. Set the UART speed in your devicetree configuration (for example, 1000000 baud).
#. Use the ``-b`` parameter with the script to match this speed (for example, ``-b 1000000``).

.. note::
   Some trace data will be dropped.
   The amount depends on the UART speed, ongoing modem operations, and the trace level set with ``AT%XMODEMTRACE``.

Setting trace level
-------------------

Configure the modem trace level using the `AT%XMODEMTRACE <xmodemtrace_>`_ command.

The modem trace subsystem automatically sends the ``AT%XMODEMTRACE=1,2`` command at startup.
This provides the most trace data and includes the crash dump collection.
Depending on the drop rate you observe, you might need to select a different ``<set_id>`` to reduce the amount of trace data generated.

Collecting traces through CMUX on Linux
---------------------------------------

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

.. analyzing_traces_start

.. _sm_logging_analyzing_traces:

Analyzing traces
================

You can analyze the captured trace file in the `Cellular Monitor app`_ to inspect AT commands, network events, and IP-level details of the communication between the modem and the cellular network.

If you suspect a modem failure, or if the modem crashes and the trace collection was enabled, you can send the trace file to `Nordic Semiconductor support <DevZone_>`_ for further analysis.

.. analyzing_traces_end

.. observability_start

.. _sm_logging_observability:

nRF Cloud observability
***********************

The |SM| collects diagnostic data about its operation.
The host can ask the |SM| to upload the data to nRF Cloud, so you can use it to investigate crashes of devices in the field.

The |SM| collects the following data:

* Events, such as the reset reason after a restart and a modem fault with its reason and program counter.
* Metrics, which the |SM| collects into a heartbeat every hour.
  The metrics include the LTE metrics and connection statistics.
  See :ref:`#XNRFCLOUDOBSHEARTBEAT <SM_AT_NRFCLOUDOBSHEARTBEAT>` for more information.
* Logs, which are the most recent information, warning, and error messages, with sensitive payloads redacted.
  The |SM| captures logs at these levels even when ``AT#XLOG=0`` disables UART logging.
  With ``AT#XLOG=2``, the |SM| logs AT traffic only as debug-level hex dumps and does not include it in the observability logs.
  After a crash, the logs from before the crash are part of the core dump.
* A core dump, which the |SM| stores in flash when it crashes.

The data does not leave the device on its own.
The host controls when it is sent, and the data reaches the cloud as follows:

1. The |SM| records the data on the device.
   When the |SM| crashes, it stores a core dump in flash and restarts.
   The core dump stays in flash over the restart until it is uploaded.
   The |SM| keeps the other data in RAM, so the data is lost on reset or power loss.
#. The host connects the |SM| to nRF Cloud with the :ref:`#XNRFCLOUD <SM_AT_NRFCLOUD>` command.
   You must onboard and claim the device first.
   See :ref:`SM_AT_PROVISIONING`.
#. The host uploads the buffered data with one of the following commands:

   * :ref:`#XNRFCLOUDOBSUPLOAD <SM_AT_NRFCLOUDOBSUPLOAD>` uploads the data when you issue it.
   * :ref:`#XNRFCLOUDOBSAUTO <SM_AT_NRFCLOUDOBSAUTO>` uploads the data periodically.
     It is disabled by default.

   The upload includes the buffered events, metrics, and logs.
   A stored core dump is included only when core dump upload is enabled with :ref:`#XNRFCLOUDOBSCOREDUMP <SM_AT_NRFCLOUDOBSCOREDUMP>`, which is the default.
#. nRF Cloud receives the data.
   By default, the data goes to the project that nRF Cloud associates with your device.
   To send the data to a specific project, specify the project key of that project in the upload command.
   You can analyze the events, metrics, logs, and core dumps in nRF Cloud.

.. note::
   The core dump storage holds one core dump.
   While a core dump is stored, subsequent crashes do not capture a new one until the stored one has been uploaded.

The core dump covers the crashes of the Serial Modem.
The |SM| records a modem fault only as an event.
The modem fault also drops the network connection, so the host cannot upload the event until the |SM| is connected to nRF Cloud again.
If the host resets the |SM| before the upload, the event is lost.
To investigate a modem fault, collect the modem traces as described in :ref:`sm_logging_modem_traces`.

.. only:: not nrf91m1

   The nRF Cloud observability AT commands require the :ref:`CONFIG_SM_NRF_CLOUD_OBSERVABILITY <CONFIG_SM_NRF_CLOUD_OBSERVABILITY>` Kconfig option, which is enabled by default.
   The ``CONFIG_MEMFAULT_METRICS_HEARTBEAT_INTERVAL_SECS`` Kconfig option sets the heartbeat interval.
   The default build stores the core dump in flash.

See :ref:`SM_AT_NRFCLOUDOBS` for the full command reference.

.. observability_end
