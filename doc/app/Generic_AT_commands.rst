.. _SM_AT_gen:

Generic AT commands
*******************

.. contents::
   :local:
   :depth: 2

This page describes generic AT commands.

|SM| echo E[0|1]
================

The ``E`` command enables or disables the AT command echo feature of the |SM| application.

Set command
-----------

The set command enables or disables the AT command echo feature.
While enabled, the |SM| application echoes back all the characters that are received in the AT command mode.

.. note::

   When working with a terminal, you should disable local echo to avoid double echoing of characters.

Syntax
~~~~~~

::

   E1 - Enable AT command echo.
   E0 - Disable AT command echo (default).

Example
~~~~~~~

Set the AT command echo:

::

   ATE1
   OK

   AT
   AT    // Echoed command
   OK

   ATE0
   ATE0  // Echoed command
   OK

   AT
   OK

Read command
------------

The read command is not supported.

Test command
------------

The test command is not supported.

|SM| version #XSLMVER
=====================

The ``#XSLMVER`` command return the versions of the |NCS| in which the |SM| application is built.
It also returns the version of the modem library that |SM| uses to communicate with the modem.

Set command
-----------

The set command returns the versions of the |NCS| and the modem library.

Syntax
~~~~~~

::

   #XSLMVER

Response syntax
~~~~~~~~~~~~~~~

::

   #XSLMVER: <ncs_version>,<libmodem_version>[,<customer_version>]

The ``<ncs_version>`` value is a string containing the version of the |NCS|.

The ``<libmodem_version>`` value is a string containing the version of the modem library.

The ``<customer_version>`` value is the :ref:`CONFIG_SM_CUSTOMER_VERSION <CONFIG_SM_CUSTOMER_VERSION>` string, if defined.

Example
~~~~~~~

The following command example reads the versions:

::

   AT#XSLMVER
   #XSLMVER: "2.5.0","2.5.0-lte-5ccd2d4dd54c"
   OK

   AT#XSLMVER
   #XSLMVER: "2.5.99","2.5.0-lte-5ccd2d4dd54c","Onomondo 2.1.0"
   OK

Read command
------------

The read command is not supported.

Test command
------------

The test command is not supported.

SM proprietary command list #XCLAC
==================================

The ``#XCLAC`` command requests the list of the proprietary |SM| commands.

Set command
-----------

The set command requests the list of the proprietary |SM| commands.
It is an add-on for ``AT+CLAC``, which lists all modem AT commands.

Syntax
~~~~~~

::

   #XCLAC

Response syntax
~~~~~~~~~~~~~~~

::

   <command list>

The ``<command list>`` value returns a list of values representing all the ``#X*`` commands followed by <CR><LF>.

Example
~~~~~~~

::

   AT#XCLAC
   AT#XSLMVER
   AT#XSLEEP
   AT#XCLAC
   AT#XSOCKET
   AT#XBIND
   AT#XCONNECT
   AT#XSEND
   AT#XRECV
   AT#XSENDTO
   AT#XRECVFROM
   AT#XPING
   AT#XGNSS
   OK

Read command
------------

The read command is not supported.

Test command
------------

The test command is not supported.

Power saving #XSLEEP
====================

The ``#XSLEEP`` command makes the |SM| application to enter idle or sleep mode.

.. note::

   The ``#XSLEEP`` command is intended for experimentation and power consumption measurements and must not be used in production.

   You can use the DTR (Data Terminal Ready) and RI (Ring Indicator) signals to control the power state of the UART between the |SM| and the host.
   See the :ref:`sm_dtr_ri` section for more information about DTR and RI.

.. note::

   If you want to do power measurements on the nRF91 Series development kit while running the |SM| application, remember to disable unused peripherals.


Set command
-----------

The set command makes the |SM| application enter either Idle or Sleep mode.

Syntax
~~~~~~

::

   #XSLEEP=<sleep_mode>

The ``<sleep_mode>`` parameter accepts only the following integer values:

* ``1`` - Enter Sleep.
  In this mode, both the |SM| service and the LTE connection are terminated.

  |SM| can be woken up using the DTR pin (``dtr-gpios``).

* ``2`` - Enter Idle.
  In this mode, both the |SM| service and the LTE connection are maintained, but the UART is disabled to save power.
  Received data is buffered and sent to the host after idle mode is exited.

  |SM| can exit the idle mode using the DTR pin (``dtr-gpios``).
  When the |SM| is in idle mode, and there is data to be read by the host, the RI pin (``ri-gpios``) is asserted for a short period of time to notify the host.
  The host can then deassert and assert DTR to exit idle mode and read the data.

The DTR pin is defined either in the :file:`boards/*_ns.overlay` overlay file matching your board or in the :file:`overlay-external-mcu.overlay` overlay file, if it is included.

.. note::

   * If the modem is on, entering Sleep mode (by issuing ``AT#XSLEEP=1`` ) sends a ``+CFUN=0`` command to the modem, which causes a write to non-volatile memory (NVM).
     Take the NVM wear into account, or put the modem in flight mode by issuing ``AT+CFUN=4`` before Sleep mode.

Examples
~~~~~~~~

::

   AT#XSLEEP=0
   ERROR

::

   AT#XSLEEP=1
   OK

See the following for an example of when the modem is on:

::

   AT+CFUN=4
   OK

   AT#XSLEEP=1
   OK

::

   AT#XSLEEP=2
   OK

Read command
------------

The read command is not supported.

Test command
------------

The test command tests the existence of the AT command and provides information about the type of its subparameters.

Syntax
~~~~~~

::

   #XSLEEP=?

Response syntax
~~~~~~~~~~~~~~~

::

   #XSLEEP: <list of shutdown_mode>

Example
~~~~~~~

::

   #XSLEEP: (1,2)
   OK

Power off #XSHUTDOWN
====================

The ``#XSHUTDOWN`` command makes the nRF91 Series SiP enter System OFF mode, which is the deepest power saving mode.

Set command
-----------

The set command makes the nRF91 Series SiP enter System OFF mode.

Syntax
~~~~~~

::

   #XSHUTDOWN

.. note::

   In this case the nRF91 Series SiP cannot be woken up using the DTR pin (``dtr-gpios``).

Example
~~~~~~~~

::

   AT#XSHUTDOWN
   OK


Read command
------------

The read command is not supported.

Test command
------------

The test command is not supported.

Reset #XRESET
=============

The ``#XRESET`` command performs a soft reset of the nRF91 Series SiP.

Set command
-----------

The set command resets the nRF91 Series SiP.

Syntax
~~~~~~

::

   #XRESET

Example
~~~~~~~~

::

   AT#XRESET
   OK
   Ready

Read command
------------

The read command is not supported.

Test command
------------

The test command is not supported.

Modem reset #XMODEMRESET
========================

The ``#XMODEMRESET`` command performs a reset of the modem.

The modem is set to minimal function mode (via ``+CFUN=0``) before being reset.
The |SM| application is not restarted.
After the command returns, the modem will be in minimal function mode.

Set command
-----------

The set command resets the modem.

Syntax
~~~~~~

::

   #XMODEMRESET

Response syntax
~~~~~~~~~~~~~~~

::

   #XMODEMRESET: <result>[,<error_code>]

* The ``<result>`` parameter is an integer indicating the result of the command.
  It can have the following values:

  * ``0`` - Success.
  * *Positive value* - On failure, indicates the step that failed.

* The ``<error_code>`` parameter is an integer.
  It is only printed when the modem reset was not successful and is the error code indicating the reason for the failure.

Example
~~~~~~~~

::

   AT#XMODEMRESET

   #XMODEMRESET: 0

   OK

Read command
------------

The read command is not supported.

Test command
------------

The test command is not supported.

Device UUID #XUUID
==================

The ``#XUUID`` command requests the device UUID.

Set command
-----------

The set command returns the device UUID.

Syntax
~~~~~~

::

   #XUUID

Response syntax
~~~~~~~~~~~~~~~

::

   #XUUID: <device-uuid>

The ``<device-uuid>`` value returns a string indicating the UUID of the device.

Example
~~~~~~~

::

  AT#XUUID

  #XUUID: 50503041-3633-4261-803d-1e2b8f70111a

  OK

Read command
------------

The read command is not supported.

Test command
------------

The test command is not supported.

Modem fault #XMODEM
===================

The application monitors the modem status.
When the application detects a *modem fault*, it sends the ``#XMODEM`` unsolicited notification.

Unsolicited notification
------------------------

The application sends the following unsolicited notification when it detects a modem fault:

::

   #XMODEM: FAULT,<reason>,<program_count>

The ``<reason>`` value returns a hexadecimal integer indicating the reason of the modem fault.
The ``<program_count>`` value returns a hexadecimal integer indicating the address of the modem fault.

The application sends the following unsolicited notification when it shuts down libmodem:

::

   #XMODEM: SHUTDOWN,<result>

The ``<result>`` value returns an integer indicating the result of the shutdown of libmodem.

The application sends the following unsolicited notification when it re-initializes libmodem:

::

   #XMODEM: INIT,<result>

The ``<result>`` value returns an integer indicating the result of the re-initialization of libmodem.

.. note::
   After libmodem is re-initialized, the MCU side must restart the current active service as follows:

   1. Stopping the service.
      For example, disconnecting the TCP connection and closing the socket.
   #. Connecting again using LTE.
   #. Restarting the service.
      For example, opening the socket and re-establishing the TCP connection.
