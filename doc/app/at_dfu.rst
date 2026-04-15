.. _DFU_AT_commands:

DFU AT commands
***************

.. contents::
   :local:
   :depth: 2

This page describes AT commands related to Device Firmware Update (DFU) operations.
These commands allow you to update the application firmware, delta modem firmware, full modem firmware, or the MCUboot second-stage bootloader through UART.

.. note::

   The DFU commands use data mode to receive firmware data.
   See the :ref:`sm_data_mode` section for more information about data mode.

.. _dfu_types:

DFU types
=========

The following DFU image types are supported:

* ``0`` - Application firmware (MCUboot)
* ``1`` - Delta modem firmware
* ``2`` - Full modem firmware (requires :ref:`CONFIG_SM_DFU_MODEM_FULL <CONFIG_SM_DFU_MODEM_FULL>`)
* ``3`` - MCUboot second-stage bootloader self-update.
  Only available when the device uses the NSIB (B0) and MCUboot as a second-stage bootloader.
  The host must stream the signed image for the **inactive** ``s0``/``s1`` slot.

.. caution::

   Full modem firmware DFU is disabled by default.
   If the full modem firmware update fails, the modem will not operate until a successful update is completed.

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

* The ``<type>`` parameter is an integer indicating the DFU image type as specified in the :ref:`dfu_types` section.
* The ``<size>`` parameter is an integer indicating the total firmware image size in bytes.
  It is required for application (type ``0``), delta modem firmware (type ``1``), and MCUboot bootloader (type ``3``) updates.

For full modem firmware (type ``2``), the command triggers an immediate reboot into bootloader mode.

Example
~~~~~~~

.. code-block:: none

   AT#XDFUINIT=0,123456
   OK

   AT#XDFUINIT=2
   // device reboots into bootloader mode
   Bootloader mode ready

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

   #XDFUINIT: <list of supported types>,<size>

Example
~~~~~~~

.. code-block:: none

   AT#XDFUINIT=?

   #XDFUINIT: (0,1,2,3),<size>

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

* The ``<type>`` parameter is an integer indicating the DFU image type as specified in the :ref:`dfu_types` section.
* The ``<addr>`` parameter is an integer indicating the address offset for the data.
* The ``<len>`` parameter is an integer indicating the length of the data chunk to write.

After the command returns ``OK``, the |SM| application enters data mode to receive exactly ``<len>`` bytes of firmware data.
When the data has been received and written, the |SM| application sends a ``#XDFU`` unsolicited notification with the status.

Example
~~~~~~~

.. code-block:: none

   AT#XDFUWRITE=0,0,4096
   OK
   // 4096 bytes of firmware data
   #XDATAMODE: 0
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

   #XDFUWRITE: <list of supported types>,<addr>,<len>

Example
~~~~~~~

.. code-block:: none

   AT#XDFUWRITE=?

   #XDFUWRITE: (0,1,2,3),<addr>,<len>

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

* The ``<type>`` parameter is an integer indicating the DFU image type as specified in the :ref:`dfu_types` section.

For application (type ``0``), delta modem firmware (type ``1``), and MCUboot bootloader (type ``3``), the update is scheduled and will be activated on the next reset, which can be done with the ``AT#XRESET`` command.
For full modem firmware (type ``2``), the command applies the current segment (bootloader or firmware) and triggers a reboot if needed.

Example
~~~~~~~

.. code-block:: none

   AT#XDFUAPPLY=0
   OK
   #XDFU: 0,2,0

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

   #XDFUAPPLY: <list of supported types>

Example
~~~~~~~

.. code-block:: none

   AT#XDFUAPPLY=?

   #XDFUAPPLY: (0,1,2,3)

   OK

DFU unsolicited notification #XDFU
==================================

The |SM| application sends the ``#XDFU`` unsolicited notification to report the status of DFU write and apply operations.

Unsolicited notification
------------------------

.. code-block:: none

   #XDFU: <type>,<operation>,<status>

* The ``<type>`` parameter is an integer indicating the DFU image type as specified in the :ref:`dfu_types` section.
* The ``<operation>`` parameter is an integer indicating the operation:

  * ``1`` - Data write completed
  * ``2`` - Apply update completed

* The ``<status>`` parameter is an integer indicating the status of the DFU operation.
  It can have one of the following values:

  * ``0`` - Success
  * ``-1`` - Failure

Complete DFU examples
=====================

The following sections showcase a complete example of application, delta modem, and full modem firmware update.

Application firmware update
---------------------------

The following example shows a complete application firmware update:

.. code-block:: none

   // Note the current firmware version of the application
   AT#XSMVER
   #XSMVER: "v2.0.0-preview1-7-gdf00f46-dirty","3.3.0-rc1"
   OK

   // Initialize DFU for application firmware (total size 123456 bytes)
   AT#XDFUINIT=0,123456
   OK

   // Write first chunk (4096 bytes at offset 0)
   AT#XDFUWRITE=0,0,4096
   OK
   // 4096 bytes of firmware data
   #XDATAMODE: 0
   #XDFU: 0,1,0

   // Write second chunk (4096 bytes at offset 4096)
   AT#XDFUWRITE=0,4096,4096
   OK
   // 4096 bytes of firmware data
   #XDATAMODE: 0
   #XDFU: 0,1,0

   // ... continue writing chunks ...

   // Apply the update
   AT#XDFUAPPLY=0
   OK
   #XDFU: 0,2,0

   // Reset to activate the new firmware
   AT#XRESET
   OK
   Ready

   // Verify that the application is running the new firmware
   AT#XSMVER
   #XSMVER: "v2.0.0-preview2-8-f0gd0f46-dirty","3.3.0-rc1"
   OK

Delta modem firmware update
---------------------------

The following example shows a complete delta modem firmware update:

.. code-block:: none

   // Note the current modem firmware
   AT%SHORTSWVER
   %SHORTSWVER: nrf91x1_2.0.0
   OK

   // Initialize DFU for delta modem firmware (total size 65536 bytes)
   AT#XDFUINIT=1,65536
   OK

   // Write first chunk (4096 bytes at offset 0)
   AT#XDFUWRITE=1,0,4096
   OK
   // 4096 bytes of firmware data
   #XDATAMODE: 0
   #XDFU: 1,1,0

   // Write second chunk (4096 bytes at offset 4096)
   AT#XDFUWRITE=1,4096,4096
   OK
   // 4096 bytes of firmware data
   #XDATAMODE: 0
   #XDFU: 1,1,0

   // ... continue writing chunks ...

   // Apply the update
   AT#XDFUAPPLY=1
   OK
   #XDFU: 1,2,0

   // Reset the modem to activate the new firmware
   AT#XMODEMRESET
   #XMODEMRESET: 0
   OK

   // Verify that the modem is running the new firmware
   AT%SHORTSWVER
   %SHORTSWVER: nrf91x1_2.0.4
   OK

Full modem firmware update
--------------------------

The following example shows a complete full modem firmware update.
For full modem firmware update, enable the :ref:`CONFIG_SM_DFU_MODEM_FULL <CONFIG_SM_DFU_MODEM_FULL>` Kconfig option.

.. important::

   If the update fails, the modem will not function until a successful update is performed.
   The bootloader remains intact for retries.
   Ensure a reliable connection between the host and serial modem, and stable power supply before starting the update.

The full modem update consists of two phases:

#. Bootloader segment - Writes the bootloader data.
#. Firmware segment - Writes the firmware data.

.. code-block:: none

   // Note the current firmware version of the modem
   AT%SHORTSWVER
   %SHORTSWVER: nrf91x1_2.0.0
   OK

   // Initialize DFU for full modem firmware
   AT#XDFUINIT=2
   // device reboots
   Bootloader mode ready

   // Phase 1: Write bootloader segment
   AT#XDFUWRITE=2,0,4096
   OK
   // 4096 bytes of bootloader data
   #XDATAMODE: 0
   #XDFU: 2,1,0

   // ... continue writing bootloader chunks ...

   // Apply bootloader segment (switches to firmware segment mode)
   AT#XDFUAPPLY=2
   OK
   #XDFU: 2,2,0

   // Phase 2: Write firmware segments
   // Warning: After the first firmware segment write, the modem will be corrupted if the update is not completed successfully.
   AT#XDFUWRITE=2,0,4096
   OK
   // 4096 bytes of firmware data
   #XDATAMODE: 0
   #XDFU: 2,1,0

   // ... continue writing firmware chunks ...

   // Apply firmware segment (triggers reboot)
   AT#XDFUAPPLY=2
   OK
   #XDFU: 2,2,0
   // device reboots
   Ready

   // Verify that the modem is running the new firmware
   AT%SHORTSWVER
   %SHORTSWVER: nrf91x1_2.0.4
   OK

MCUboot firmware update
-----------------------

.. important::
   NSIB (B0) uses monotonic counter values to prevent rollback of the MCUboot bootloader.
   When performing an MCUboot DFU, the host must ensure that the new MCUboot image has a strictly higher monotonic counter value than the currently running MCUboot to avoid update failure.
   This also means that updates for MCUboot images are limited to the number of slots defined in the ``SB_CONFIG_SECURE_BOOT_NUM_VER_COUNTER_SLOTS`` Kconfig option.
   In |SM|, it is set to ``40`` by default.

The following example shows a complete MCUboot firmware update:

.. code-block:: none

   // Query the current slot for MCUboot image
   AT#XBOOTINFO=1
   #XBOOTINFO: 0
   OK

   // Download the correct MCUboot image for the inactive slot.
   // The default NCS image names for MCUboot are:
   // - Slot 0: signed_by_mcuboot_and_b0_mcuboot.bin
   // - Slot 1: signed_by_mcuboot_and_b0_s1_image.bin

   // In this example, the device is currently running slot 0, so the host should download the signed image for slot 1, which is signed_by_mcuboot_and_b0_s1_image.bin.

   // Note the current monotonic counter value for MCUboot
   AT#XBOOTINFO=0
   #XBOOTINFO: 1
   OK

   // Initialize DFU for MCUboot firmware (total size 23456 bytes)
   AT#XDFUINIT=3,23456
   OK

   // Write first chunk (4096 bytes at offset 0)
   AT#XDFUWRITE=3,0,4096
   OK
   // 4096 bytes of firmware data
   #XDATAMODE: 0
   #XDFU: 3,1,0

   // Write second chunk (4096 bytes at offset 4096)
   AT#XDFUWRITE=3,4096,4096
   OK
   // 4096 bytes of firmware data
   #XDATAMODE: 0
   #XDFU: 3,1,0

   // ... continue writing chunks ...

   // Apply the update
   AT#XDFUAPPLY=3
   OK
   #XDFU: 3,2,0

   // Reset to activate the new firmware
   AT#XRESET
   OK
   Ready

   // Verify that the monotonic counter value for MCUboot was updated
   AT#XBOOTINFO=0
   #XBOOTINFO: 2
   OK

   // (Optional) Verify that the MCUboot slot was switched
   AT#XBOOTINFO=1
   #XBOOTINFO: 1
   OK


DFU host example
================

See ``app/scripts/sm_dfu_host.py`` for an example of how to implement a DFU host. The script demonstrates the DFU AT command flow and can be used as a starting point for custom implementations.

Install dependencies:

.. code-block:: bash

   pip install -r app/scripts/requirements.txt

.. note::

   cbor2 5.8.0 does not work with the ``sm_dfu_host.py`` script due to a known bug introduced in that version.
   Versions older and newer than that work.
   See `cbor2 bug`_ for more information.

Usage
-----

.. code-block:: bash

   python sm_dfu_host.py --port <serial_port> --baudrate <baud> --file <path-to-firmware> --type <dfu_type>

Where ``<dfu_type>`` is one of: ``application``, ``modem-delta``, or ``modem-full``.

After the DFU transfer completes:

.. note::

   Applying application and delta modem firmware can take some time.

* For application updates, reset the device to activate the new firmware:

  .. code-block:: bash

     python sm_dfu_host.py --port <serial_port> --baudrate <baud> --reset

* For delta modem updates, reset the modem to apply the update:

  .. code-block:: bash

     python sm_dfu_host.py --port <serial_port> --baudrate <baud> --modem-reset

* Full modem updates handle the reset automatically.

To check if the device is operating in normal mode:

.. code-block:: bash

   python sm_dfu_host.py --port <serial_port> --baudrate <baud> --ping

.. note::

   Ping will return an error if the device is in bootloader mode.
