.. _SM_AT_trace:

Trace AT commands
*****************

.. contents::
   :local:
   :depth: 2

.. note::

   These AT commands are `Experimental <Software maturity levels_>`_.

This page describes the AT commands for controlling the shared UART trace backend.

The ``AT#XLOG`` command is always available in the default build.
The ``AT#XTRACE`` command requires building with the :file:`overlay-trace-backend-uart.conf` configuration overlay.
See :ref:`sm_logging_uart_backend` for a full description of the feature.

The Zephyr application log backend (``AT#XLOG``) and the modem trace backend (``AT#XTRACE``) share a single UART.
They are mutually exclusive: enabling one while the other is active returns an error.
The trace UART is suspended when neither backend is active and resumed automatically when either backend is enabled.

The trace UART is configured at 1000000 baud rate to support the high data rate required for the modem traces.
Trace data is available through the UART1 interface (VCOM1 on the nRF9151 DK).


Application log AT#XLOG
=======================

The ``AT#XLOG`` command enables or disables the Zephyr application log backend and the shared UART.

Set command
-----------

The set command enables or disables the Zephyr application log backend.

Syntax
~~~~~~

::

   AT#XLOG=<mode>

* The ``<mode>`` parameter is an integer:

  * ``0`` - Disable the application log backend and suspend the UART.
  * ``1`` - Resume the UART and enable the application log backend.


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

* The ``<mode>`` parameter reflects the current state (``0`` = disabled, ``1`` = enabled).

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

* The ``<mode>`` parameter is an integer:

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

* The ``<mode>`` parameter reflects the current state (``0`` = disabled, ``1`` = enabled).

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
