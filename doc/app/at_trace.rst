.. _SM_AT_trace:

Trace AT commands
*****************

.. contents::
   :local:
   :depth: 1

This page describes the AT commands for controlling the shared UART trace backend.

The ``AT#XLOG`` command is always available in the default build.
The ``AT#XTRACE`` command requires building with the :file:`trace-backend-uart.conf` configuration overlay.
See :ref:`sm_logging_uart_backend` for a full description of the feature.

The Zephyr application log backend (``AT#XLOG``) and the modem trace backend (``AT#XTRACE``) share a single UART.
They are mutually exclusive: enabling one while the other is active returns an error.
During boot, the UART outputs logs from B0, MCUboot, and the application.
After initialization, the UART is suspended when no backend is active and resumed when at least one backend is enabled.

The trace UART is configured at 1000000 baud rate to support the high data rate required for the modem traces.
Trace data is available through the UART1 interface (VCOM1 on the nRF9151 DK).


Application log AT#XLOG
=======================

The ``AT#XLOG`` command enables or disables the Zephyr application log backend and the shared UART.

.. note::
   Regardless of the logging mode, nRF Cloud Observability collects information, warning, and error level logs (with sensitive payloads redacted) on crash, even when the UART is silent.
   You can only view these logs once uploaded to nRF Cloud.
   See :ref:`SM_AT_NRFCLOUDOBS`.

Set command
-----------

The set command enables or disables the Zephyr application log backend.

Syntax
~~~~~~

::

   AT#XLOG=<mode>

The parameters and their defined values are the following:

<mode>
  * ``0`` - Suspend the UART and disable the application log backend, no logs are shown.
    Logs are still collected by nRF Cloud Observability.
  * ``1`` - Resume the UART and enable the application log backend.
    AT commands, responses, and URCs are logged as strings at information log level, with sensitive payloads redacted.
  * ``2`` - Resume the UART and enable the application log backend.
    AT commands, responses, and URCs are logged as raw hex dumps at debug level only.
    These hex dumps are not collected by nRF Cloud Observability.
    Requires the ``CONFIG_SM_LOG_LEVEL_DBG`` Kconfig option.

.. note::
   Returns ``ERROR`` if ``AT#XTRACE=1`` has been issued.

Read command
------------

The read command returns the current state of the application log backend.

Syntax
~~~~~~

::

   AT#XLOG?

Response syntax
~~~~~~~~~~~~~~~

::

   #XLOG: <mode>

The parameters and their defined values are the following:

<mode>
   The current state.

   * ``0`` - Disabled.
   * ``1`` - Logging with sensitive AT command payloads redacted.
   * ``2`` - Logging with sensitive AT command and response payloads logged as DBG hex dump only.
     Only available if the ``CONFIG_SM_LOG_LEVEL_DBG`` Kconfig option is enabled.

Test command
------------

The test command returns the supported parameter range.

Syntax
~~~~~~

::

   AT#XLOG=?

Response syntax
~~~~~~~~~~~~~~~

::

   #XLOG: (0,1,2)

The ``2`` option is only listed if the ``CONFIG_SM_LOG_LEVEL_DBG`` Kconfig option is enabled.
Otherwise, the response is the following::

   #XLOG: (0,1)

Example
~~~~~~~

::

   AT#XLOG=1
   OK

   AT#XLOG?
   #XLOG: 1
   OK

   AT#XLOG=0
   OK


Modem trace AT#XTRACE
=====================

The ``AT#XTRACE`` command enables or disables the modem trace backend on the shared UART.

When enabled, the UART is resumed and the modem is instructed to generate full-level traces (``AT%XMODEMTRACE=1,2``).
When disabled, the modem is instructed to stop generating traces (``AT%XMODEMTRACE=0``) and the UART is suspended.

Set command
-----------

The set command enables or disables the modem trace backend.

Syntax
~~~~~~

::

   AT#XTRACE=<mode>

The parameters and their defined values are the following:

<mode>
   * ``0`` - Disable modem traces and suspend the UART.
   * ``1`` - Resume the UART and enable the modem trace backend.

.. note::
   Returns ``ERROR`` if ``AT#XLOG=1`` has been issued.


Read command
------------

The read command returns the current state of the modem trace backend.

Syntax
~~~~~~

::

   AT#XTRACE?

Response syntax
~~~~~~~~~~~~~~~

::

   #XTRACE: <mode>

The parameters and their defined values are the following:

<mode>
   The current state.

   * ``0`` - Disabled.
   * ``1`` - Enabled.

Test command
------------

The test command returns the supported parameter range.

Syntax
~~~~~~

::

   AT#XTRACE=?

Response syntax
~~~~~~~~~~~~~~~

::

   #XTRACE: (0,1)

Example
~~~~~~~

::

   AT#XTRACE=1
   OK

   AT#XTRACE?
   #XTRACE: 1
   OK

   AT#XTRACE=0
   OK
