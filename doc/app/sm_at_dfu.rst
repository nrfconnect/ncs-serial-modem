.. _SM_AT_DFU:

DFU AT commands
***************

.. contents::
   :local:
   :depth: 2

This page describes AT commands related to Device Firmware Update (DFU) operations.
These commands allow updating the application firmware, delta modem firmware, or full modem firmware via UART.

.. note::

   The DFU commands use data mode to receive firmware data.
   See the :ref:`sm_data_mode` section for more information about data mode.

DFU types
=========

The following DFU image types are supported:

* ``0`` - Application firmware (MCUboot)
* ``1`` - Delta modem firmware
* ``2`` - Full modem firmware (requires :ref:`CONFIG_SM_FULL_MODEM_DFU <CONFIG_SM_FULL_MODEM_DFU>`)

.. warning::

   Full modem firmware DFU is disabled by default. If the full modem firmware update fails, the modem will not operate until a successful update is completed.

DFU initialize #XDFUINIT
========================

The ``#XDFUINIT`` command initializes the DFU target for a specific image type.

Set command
-----------

The set command initializes the DFU target.

Syntax
~~~~~~

.. code-block:: none

   AT#XDFUINIT=<type>[,<size>]

* The ``<type>`` parameter is an integer indicating the DFU image type:

  * ``0`` - Application firmware
  * ``1`` - Delta modem firmware
  * ``2`` - Full modem firmware

* The ``<size>`` parameter is an integer indicating the total firmware image size in bytes.
  It is required for application and delta modem firmware updates.

For full modem firmware (type ``2``), the command triggers an immediate reboot into bootloader mode.

Example
~~~~~~~

.. code-block:: none

   AT#XDFUINIT=0,123456
   OK

   AT#XDFUINIT=2
   <device reboots into bootloader mode>

Read command
------------

The read command is not supported.

Test command
------------

The test command tests the existence of the command and provides information about the type of its subparameters.

Syntax
~~~~~~

.. code-block:: none

   AT#XDFUINIT=?

Response syntax
~~~~~~~~~~~~~~~

.. code-block:: none

   #XDFUINIT: (0,1),<size>
   #XDFUINIT: (2)

Example
~~~~~~~

.. code-block:: none

   AT#XDFUINIT=?

   #XDFUINIT: (0,1),<size>
   #XDFUINIT: (2)

   OK

DFU write #XDFUWRITE
====================

The ``#XDFUWRITE`` command writes firmware data to the DFU target.
The command enters data mode to receive the firmware chunk.

Set command
-----------

The set command initiates a firmware data write operation.

Syntax
~~~~~~

.. code-block:: none

   AT#XDFUWRITE=<type>,<addr>,<len>

* The ``<type>`` parameter is an integer indicating the DFU image type (``0``, ``1``, or ``2``).
* The ``<addr>`` parameter is an integer indicating the address offset for the data.
* The ``<len>`` parameter is an integer indicating the length of the data chunk to write.

After the command returns ``OK``, the |SM| application enters data mode to receive exactly ``<len>`` bytes of firmware data.
When the data has been received and written, the |SM| application sends a ``#XDFU`` unsolicited notification with the result.

Example
~~~~~~~

.. code-block:: none

   AT#XDFUWRITE=0,0,4096
   OK
   <4096 bytes of firmware data>
   #XDFU: 0,1,0

Read command
------------

The read command is not supported.

Test command
------------

The test command tests the existence of the command and provides information about the type of its subparameters.

Syntax
~~~~~~

.. code-block:: none

   AT#XDFUWRITE=?

Response syntax
~~~~~~~~~~~~~~~

.. code-block:: none

   #XDFUWRITE: (0,1,2),<addr>,<len>

Example
~~~~~~~

.. code-block:: none

   AT#XDFUWRITE=?

   #XDFUWRITE: (0,1,2),<addr>,<len>

   OK

DFU apply #XDFUAPPLY
====================

The ``#XDFUAPPLY`` command finalizes the firmware update and schedules it for activation.

Set command
-----------

The set command applies the firmware update.

Syntax
~~~~~~

.. code-block:: none

   AT#XDFUAPPLY=<type>

* The ``<type>`` parameter is an integer indicating the DFU image type (``0``, ``1``, or ``2``).

For application and delta modem firmware, the update is scheduled and will be activated on the next reset.
For full modem firmware, the command applies the current segment (bootloader or firmware) and may trigger a reboot.

Example
~~~~~~~

.. code-block:: none

   AT#XDFUAPPLY=0
   #XDFU: 0,2,0
   OK

   AT#XRESET
   OK
   Ready

Read command
------------

The read command is not supported.

Test command
------------

The test command tests the existence of the command and provides information about the type of its subparameters.

Syntax
~~~~~~

.. code-block:: none

   AT#XDFUAPPLY=?

Response syntax
~~~~~~~~~~~~~~~

.. code-block:: none

   #XDFUAPPLY: (0,1,2)

Example
~~~~~~~

.. code-block:: none

   AT#XDFUAPPLY=?

   #XDFUAPPLY: (0,1,2)

   OK

DFU unsolicited notification #XDFU
==================================

The |SM| application sends the ``#XDFU`` unsolicited notification to report the result of DFU write and apply operations.

Unsolicited notification
------------------------

.. code-block:: none

   #XDFU: <type>,<operation>,<result>

* The ``<type>`` value is an integer indicating the DFU image type (``0``, ``1``, or ``2``).
* The ``<operation>`` value is an integer indicating the operation:

  * ``1`` - Data write completed
  * ``2`` - Apply update completed

* The ``<result>`` value is an integer indicating the result.
  ``0`` indicates success, negative values indicate errors.

Complete DFU examples
=====================

Application firmware update
---------------------------

The following example shows a complete application firmware update:

.. code-block:: none

   Initialize DFU for application firmware (total size 123456 bytes)
   AT#XDFUINIT=0,123456
   OK

   Write first chunk (4096 bytes at offset 0)
   AT#XDFUWRITE=0,0,4096
   OK
   <4096 bytes of firmware data>
   #XDFU: 0,1,0

   Write second chunk (4096 bytes at offset 4096)
   AT#XDFUWRITE=0,4096,4096
   OK
   <4096 bytes of firmware data>
   #XDFU: 0,1,0

   ... continue writing chunks ...

   Apply the update
   AT#XDFUAPPLY=0
   #XDFU: 0,2,0
   OK

   Reset to activate the new firmware
   AT#XRESET
   OK
   Ready

Delta modem firmware update
---------------------------

The following example shows a complete delta modem firmware update:

.. code-block:: none

   Initialize DFU for delta modem firmware (total size 65536 bytes)
   AT#XDFUINIT=1,65536
   OK

   Write first chunk (4096 bytes at offset 0)
   AT#XDFUWRITE=1,0,4096
   OK
   <4096 bytes of firmware data>
   #XDFU: 1,1,0

   Write second chunk (4096 bytes at offset 4096)
   AT#XDFUWRITE=1,4096,4096
   OK
   <4096 bytes of firmware data>
   #XDFU: 1,1,0

   ... continue writing chunks ...

   Apply the update
   AT#XDFUAPPLY=1
   #XDFU: 1,2,0
   OK

   Reset the modem to activate the new firmware
   AT#XMODEMRESET
   #XMODEMRESET: 0
   OK

Full modem firmware update
--------------------------

The following example shows a complete full modem firmware update.

.. important::

   If the update fails, the modem will not function until a successful update is performed.
   The bootloader remains intact for retries. Ensure a reliable connection and power
   supply before starting.

The full modem update consists of two phases:

1. **Bootloader segment**: Write the bootloader data
2. **Firmware segment**: Write the firmware data

.. code-block:: none

   Initialize DFU for full modem firmware
   AT#XDFUINIT=2
   <device reboots>

   After reboot, device is in bootloader mode
   Phase 1: Write bootloader segment
   AT#XDFUWRITE=2,0,4096
   OK
   <4096 bytes of bootloader data>
   #XDFU: 2,1,0

   ... continue writing bootloader chunks ...

   Apply bootloader segment (switches to firmware segment mode)
   AT#XDFUAPPLY=2
   #XDFU: 2,2,0
   OK

   Phase 2: Write firmware segments
   AT#XDFUWRITE=2,0,4096
   OK
   <4096 bytes of firmware data>
   #XDFU: 2,1,0

   ... continue writing firmware chunks ...

   Apply firmware segment (triggers reboot)
   AT#XDFUAPPLY=2
   #XDFU: 2,2,0
   OK
   <device reboots>

Configuration options
*********************

.. _CONFIG_SM_FULL_MODEM_DFU:

CONFIG_SM_FULL_MODEM_DFU - Enable full modem DFU
   This option enables support for full modem firmware updates.
   When enabled, the ``#XDFUINIT``, ``#XDFUWRITE``, and ``#XDFUAPPLY`` commands support type ``2``.
