.. raw:: latex

   \nordicStartAppendices

.. _nrf91m1_update_procedures:

Firmware update procedures
##########################

.. contents::
   :local:
   :depth: 2

This section describes how to update the firmware of the |SM| and how to verify that the update succeeded.
It covers the update of the application firmware, the modem firmware, and the MCUboot second-stage bootloader.

Update methods
**************

The following methods are available for delivering an update image to the |SM|:

.. list-table::
   :header-rows: 1
   :widths: 40 15 15 15 15

   * - Method
     - Application
     - MCUboot
     - Modem delta
     - Modem full
   * - DFU over UART (:ref:`DFU_AT_commands`)
     - Yes
     - Yes
     - Yes
     - Yes
   * - FOTA from an HTTP(S) server (:ref:`SM_AT_FOTA`)
     - Yes
     - Yes
     - Yes
     - No
   * - nRF Cloud FOTA (:ref:`SM_AT_NRFCLOUDFOTA`)
     - Yes
     - Not yet
     - Not yet
     - No

With DFU over UART, the host must store the update image and stream it to the |SM|.
In the field, make sure that the host has the image available in its own storage before it starts the update.
For example, the host can store the image in its flash memory or file system.
With the FOTA methods, the |SM| downloads the image itself from the server, so the host does not need to store it.

With DFU over UART and FOTA from an HTTP or HTTPS server, you must provide your own infrastructure to host the update images and deliver them to the device.
nRF Cloud FOTA does not require your own infrastructure because it uses the nRF Cloud infrastructure to host the update images.

In all methods, the |SM| first stores the update fully and then activates it with a reset.

.. _nrf91m1_update_images:

nRF91M1 images
**************

The update procedures use the application firmware images and the modem firmware images listed in this section.
Both are delivered in zip packages.
Unzip a package to get the images that the update procedures send to the |SM|.

Application firmware images
===========================

The application firmware package is available on the `nRF91M1 Downloads`_ page.
The package contains the images that the update procedures send to the |SM|, and the image for programming the firmware to the `nrf9151dk`_ board.

.. list-table::
   :header-rows: 1
   :widths: 25 35 40

   * - Package
     - Image
     - Description
   * - :file:`serial_modem_nrf91m1_2.0.0.zip`
     - :file:`serial_modem_nrf91m1_2.0.0_nrf9151dk.hex`
     - The firmware image that is pre-programmed to the nRF91M1 module.
       Use it to program the nRF91M1 firmware v2.0.0 to the `nrf9151dk`_ board (see :ref:`nrf91m1_update_dk`).
   * -
     - :file:`serial_modem_nrf91m1_2.0.0.bin`
     - Application update image v2.0.0.
       Use it to test the application update (see :ref:`nrf91m1_update_application`).
   * -
     - :file:`serial_modem_nrf91m1_2.0.0-FOTA-TEST.bin`
     - Test application update image, with the ``FOTA-TEST`` custom version text.
       Use it to test the application update (see :ref:`nrf91m1_update_application`).
   * -
     - :file:`mcuboot_nrf91m1_s0_v2-FOTA-TEST.bin`
     - MCUboot version 2 update image for slot ``s0``.
       Use it to test the MCUboot update (see :ref:`nrf91m1_update_mcuboot`).
   * -
     - :file:`mcuboot_nrf91m1_s1_v2-FOTA-TEST.bin`
     - MCUboot version 2 update image for slot ``s1``.
       Use it to test the MCUboot update (see :ref:`nrf91m1_update_mcuboot`).

Modem firmware images
=====================

The modem firmware packages are available on the `nRF9151 Downloads`_ page.
Each package contains the modem firmware and the images that the update procedures send to the |SM|.
You can also program a whole package to the `nrf9151dk`_ board (see :ref:`nrf91m1_update_dk`).

.. list-table::
   :header-rows: 1
   :widths: 25 35 40

   * - Package
     - Image
     - Description
   * - :file:`mfw_nrf91x1_2.0.4.zip`
     - The whole package
     - Modem firmware ``mfw_nrf91x1`` v2.0.4.
       Use it to program the modem firmware to the `nrf9151dk`_ board.
   * -
     - :file:`mfw_nrf91x1_update_from_2.0.4_to_2.0.4-FOTA-TEST.bin`
     - Delta update image from ``2.0.4`` to ``2.0.4-FOTA-TEST``.
       Use it to test the modem delta update (see :ref:`nrf91m1_update_modem_delta`).
   * -
     - :file:`mfw_nrf91x1_update_from_2.0.4-FOTA-TEST_to_2.0.4.bin`
     - Delta update image from ``2.0.4-FOTA-TEST`` to ``2.0.4``.
       Use it to test the modem delta update (see :ref:`nrf91m1_update_modem_delta`).
   * - :file:`mfw_nrf9151-ntn_1.0.1.zip`
     - The whole package
     - Modem firmware ``mfw_nrf9151-ntn`` v1.0.1.
       Use it to program the modem firmware to the `nrf9151dk`_ board.
   * -
     - :file:`mfw_nrf9151-ntn_1.0.1.cbor`
     - Full modem firmware image.
       Use it to update the modem firmware to ``mfw_nrf9151-ntn`` with the full modem update (see :ref:`nrf91m1_update_modem_full`).
   * -
     - :file:`mfw_nrf9151-ntn_update_from_1.0.1_to_1.0.1-FOTA-TEST.bin`
     - Delta update image from ``1.0.1`` to ``1.0.1-FOTA-TEST``.
       Use it to test the modem delta update after the full modem update to ``mfw_nrf9151-ntn`` (see :ref:`nrf91m1_update_modem_delta`).
   * -
     - :file:`mfw_nrf9151-ntn_update_from_1.0.1-FOTA-TEST_to_1.0.1.bin`
     - Delta update image from ``1.0.1-FOTA-TEST`` to ``1.0.1``.
       Use it to test the modem delta update after the full modem update to ``mfw_nrf9151-ntn`` (see :ref:`nrf91m1_update_modem_delta`).

.. _nrf91m1_update_rollback:

Rollback protection
*******************

The update images are protected against rollback.
The nRF91M1 firmware rejects an application image that has a lower version than the running application.
It also rejects an MCUboot image that has a lower version than the running MCUboot.

The modem has ``AT%XPRODDONE=1`` set, so the modem firmware can only be updated forward with a delta or a full modem update.
For this purpose, ``mfw_nrf9151-ntn`` is considered a newer version than ``mfw_nrf91x1`` v2.0.4 and any future ``mfw_nrf91x1`` v2.x.x releases.

In addition, the nRF91M1 SiP has the access port protection (APPROTECT) and the erase protection (ERASEPROTECT) enabled in the UICR.
This means that you cannot recover or erase the |SM|, and you cannot program any firmware to it through SWD.
The firmware on the |SM| can be changed only with the update methods described in this section.

.. important::
   Every update to higher version is permanent.
   You cannot return to an earlier application, MCUboot, or modem firmware version.

.. _nrf91m1_update_dk:

Testing updates with the nRF9151 DK
***********************************

You can use the `nrf9151dk`_ board to try the update procedures without the one-way limitations of the |SM| described in :ref:`nrf91m1_update_rollback`.
The nRF91M1 firmware does not set APPROTECT or ERASEPROTECT, and the modem does not have ``AT%XPRODDONE`` set.
This means that you can always program the DK again with any firmware, including any modem firmware, and return to an earlier version that way.
The rollback protection of the application and MCUboot in the nRF91M1 firmware still applies on the DK while the firmware is running, so the updates themselves cannot go backwards.

To set up the DK, follow the instructions in :ref:`nrf91m1_uart_nrf9151dk` with the following packages:

* Modem firmware: :file:`mfw_nrf91x1_2.0.4.zip`
* Application firmware: :file:`serial_modem_nrf91m1_2.0.0.zip`, which contains the :file:`serial_modem_nrf91m1_2.0.0_nrf9151dk.hex` image that you program to the DK

See :ref:`nrf91m1_update_images` for the contents of the packages.

The update procedures in this section work the same way on the DK as on the |SM|.
To repeat a procedure, or to start from an earlier firmware version, program the :file:`serial_modem_nrf91m1_2.0.0_nrf9151dk.hex` image to the DK again.
To return to an earlier modem firmware version, program the modem firmware package again.
If programming fails, erase the DK using the Programmer app and try again.

.. _nrf91m1_update_dfu_host:

Updating with the DFU host script
=================================

The `sm_dfu_host.py`_ script is a quick way to test the DFU over UART updates with the DK.
It sends the update image to the |SM| using the DFU AT commands.
The script does not require the |NCS|.
It is a Python script that needs only Python and the packages listed in the `sm_dfu_host.py requirements`_ file.
See :ref:`sm_dfu_host_example` for the full usage.

Connect the DK as described in :ref:`nrf91m1_uart_dk`, and use the serial port of VCOM0 and the baud rate ``115200``.
The following commands show the update of each image type:

.. code-block:: none

   python sm_dfu_host.py --port <serial_port> --baudrate 115200 --type application --file <image.bin>
   python sm_dfu_host.py --port <serial_port> --baudrate 115200 --type mcuboot-bootloader --file <image.bin>
   python sm_dfu_host.py --port <serial_port> --baudrate 115200 --type modem-delta --file <image.bin>
   python sm_dfu_host.py --port <serial_port> --baudrate 115200 --type modem-full --file <image.cbor>

After the transfer, activate the update with a reset.

.. _nrf91m1_update_application:

Application update
******************

The following procedure updates the application with the :file:`serial_modem_nrf91m1_2.0.0-FOTA-TEST.bin` image and then back with the :file:`serial_modem_nrf91m1_2.0.0.bin` image.
Both images have the application version ``2.0.0``, but the :file:`serial_modem_nrf91m1_2.0.0-FOTA-TEST.bin` image has the custom version text ``FOTA-TEST``.
The ``AT#XSMVER`` command returns the application version and the custom version text if there is one (see :ref:`SM_AT_XSMVER`).
This makes the update visible in the ``AT#XSMVER`` response.

Use this procedure to test your update path before you deploy your own application updates.

Application update with DFU over UART
=====================================

#. Verify the current application version:

   .. code-block:: none

      AT#XSMVER
      #XSMVER: "v2.0.0","3.4.1"
      OK

#. Send the :file:`serial_modem_nrf91m1_2.0.0-FOTA-TEST.bin` image to the |SM| using the DFU AT commands (``AT#XDFUINIT=0``, ``AT#XDFUWRITE=0``, and ``AT#XDFUAPPLY=0``).
   See :ref:`DFU_AT_commands` for the full command sequence.
   When you test with the DK, you can use the DFU host script instead (see :ref:`nrf91m1_update_dfu_host`).

#. Reset the |SM| to activate the new application.
   For example, use the :ref:`SM_AT_XRESET` AT command or a pin reset:

   .. code-block:: none

      AT#XRESET
      OK
      Ready

#. Verify that the update succeeded.
   The custom version text ``FOTA-TEST`` is visible in the response.

   .. code-block:: none

      AT#XSMVER
      #XSMVER: "v2.0.0","3.4.1","FOTA-TEST"
      OK

#. Repeat steps 2 and 3 with the :file:`serial_modem_nrf91m1_2.0.0.bin` image to update back.

#. Verify that the update succeeded.
   The custom version text is no longer present in the response.

   .. code-block:: none

      AT#XSMVER
      #XSMVER: "v2.0.0","3.4.1"
      OK

Application update with FOTA from an HTTP server
================================================

#. Verify the current application version:

   .. code-block:: none

      AT#XSMVER
      #XSMVER: "v2.0.0","3.4.1"
      OK

#. Upload the :file:`serial_modem_nrf91m1_2.0.0-FOTA-TEST.bin` image to your HTTP(S) server.
   For HTTPS, provision the server certificate to the |SM| and note its security tag.

#. Download the image using ``AT#XFOTA=1``.
   Wait for the ``#XFOTA: 4,0`` notification, which indicates that the image is downloaded and waiting for activation.

   .. code-block:: none

      AT+CFUN=1
      OK

      AT#XFOTA=1,"https://remote.host/fota/serial_modem_nrf91m1_2.0.0-FOTA-TEST.bin",42
      OK
      #XFOTA: 1,0,0
      ...
      #XFOTA: 4,0

#. Set the modem to minimal function mode before the reset:

   .. code-block:: none

      AT+CFUN=0
      OK

#. Reset the |SM| and verify that the custom version text ``FOTA-TEST`` is visible in the response:

   .. code-block:: none

      AT#XSMVER
      #XSMVER: "v2.0.0","3.4.1","FOTA-TEST"
      OK

#. Repeat steps 2 to 5 with the :file:`serial_modem_nrf91m1_2.0.0.bin` image to update back.

#. Verify that the custom version text is no longer present in the response:

   .. code-block:: none

      AT#XSMVER
      #XSMVER: "v2.0.0","3.4.1"
      OK

See :ref:`SM_AT_FOTA` for the full description of the command and its notifications.

Application update with nRF Cloud FOTA
======================================

Before you use nRF Cloud FOTA, you must provision and claim the device in nRF Cloud.
See :ref:`SM_AT_PROVISIONING` for the provisioning AT commands and the claiming procedure.

.. note::
   nRF Cloud shows the application version of the device as ``2.0.0+0``, and you must name the release with this version.
   The ``+0`` part is not used in the version check when updating the application, and ``AT#XSMVER`` does not show it.
   In this example procedure, both application images have the version ``2.0.0+0``, so nRF Cloud does not offer the release by default.
   The **Repeatedly Return Latest Release** setting of the cohort makes nRF Cloud offer it anyway.
   This setting should only be used for development and testing purposes. On a real firmware update the version number is increased, for example ``2.0.1+0``.

#. In the application firmware project, make sure that the hardware version ``nrf9151dk`` exists in the project settings (**Project Settings** > **Hardware**).
   If it does not exist, create it.

#. Create a cohort for the device and assign the device to it.
   Do not use the default cohort, because the next step changes the behavior of the whole cohort.

#. In the settings of the cohort, turn on **Repeatedly Return Latest Release**.
   The setting makes nRF Cloud offer the latest release even if the device already runs it.
   Use it only for cohorts that are used for testing.

#. Create a full release in the application firmware project with the version ``2.0.0+0``, and add an OTA payload to it with the following values:

   * Hardware version: ``nrf9151dk``
   * Software type: ``app``
   * File: :file:`serial_modem_nrf91m1_2.0.0-FOTA-TEST.bin`

#. Create an activation for the release.
   Select the cohort that you created.
   See `nRF Cloud over-the-air updates`_ for more information.

#. Verify the current application version:

   .. code-block:: none

      AT#XSMVER
      #XSMVER: "v2.0.0","3.4.1"
      OK

#. Check that the update is available, then download it:

   .. code-block:: none

      AT+CFUN=1
      OK

      AT#XNRFCLOUDFOTA=4
      OK
      #XNRFCLOUDFOTA: 0,0,1

      AT#XNRFCLOUDFOTA=1
      OK
      ...
      #XNRFCLOUDFOTA: 4,0

#. Set the modem to minimal function mode before the reset:

   .. code-block:: none

      AT+CFUN=0
      OK

#. Reset the |SM| and verify that the custom version text ``FOTA-TEST`` is visible in the response:

   .. code-block:: none

      AT#XSMVER
      #XSMVER: "v2.0.0","3.4.1","FOTA-TEST"
      OK

#. Delete the OTA payload from the release, and add an OTA payload with the hardware version ``nrf9151dk``, the software type ``app``, and the :file:`serial_modem_nrf91m1_2.0.0.bin` file to the same release.
   The release and its activation stay the same.
   Then repeat steps 7 and 8, and reset the |SM|.

#. Verify that the custom version text is no longer present in the response:

   .. code-block:: none

      AT#XSMVER
      #XSMVER: "v2.0.0","3.4.1"
      OK

#. Abort the activation of the release for the cohort and delete the OTA payload.
   Turn off **Repeatedly Return Latest Release** for the cohort.

See :ref:`SM_AT_NRFCLOUDFOTA` for the full description of the command and its notifications.

.. _nrf91m1_update_modem_delta:

Modem delta update
******************

A delta update patches the modem firmware that runs on the |SM| to a newer version.
The delta image is specific to the version it updates from.
The delta images are included in the modem firmware packages (see :ref:`nrf91m1_update_images`).
In this procedure, use the images from :file:`mfw_nrf91x1_2.0.4.zip`.
If you have updated the modem to ``mfw_nrf9151-ntn`` with the full modem update, use the delta images from :file:`mfw_nrf9151-ntn_1.0.1.zip` instead.

The following procedures update the modem firmware from ``nrf91x1_2.0.4`` to ``nrf91x1_2.0.4-FOTA-TEST`` and back.
The ``AT%SHORTSWVER`` command returns the modem firmware version.

Modem delta update with DFU over UART
=====================================

#. Verify that the modem runs the v2.0.4 firmware:

   .. code-block:: none

      AT%SHORTSWVER
      %SHORTSWVER: nrf91x1_2.0.4
      OK

#. Send the :file:`mfw_nrf91x1_update_from_2.0.4_to_2.0.4-FOTA-TEST.bin` delta image to the |SM| using the DFU AT commands (``AT#XDFUINIT=1``, ``AT#XDFUWRITE=1``, and ``AT#XDFUAPPLY=1``).
   See :ref:`DFU_AT_commands` for the full command sequence.
   When you test with the DK, you can use the DFU host script instead (see :ref:`nrf91m1_update_dfu_host`).

#. Reset the |SM| or only the modem to activate the new modem firmware.
   To reset only the modem, use the :ref:`SM_AT_XMODEMRESET` AT command:

   .. code-block:: none

      AT#XMODEMRESET
      #XMODEMRESET: 0
      OK

#. Verify that the modem runs the ``2.0.4-FOTA-TEST`` firmware:

   .. code-block:: none

      AT%SHORTSWVER
      %SHORTSWVER: nrf91x1_2.0.4-FOTA-TEST
      OK

#. Repeat steps 2 and 3 with the :file:`mfw_nrf91x1_update_from_2.0.4-FOTA-TEST_to_2.0.4.bin` delta image to update back to v2.0.4.

#. Verify that the modem runs the v2.0.4 firmware again:

   .. code-block:: none

      AT%SHORTSWVER
      %SHORTSWVER: nrf91x1_2.0.4
      OK

Modem delta update with FOTA from an HTTP server
================================================

#. Verify that the modem runs the v2.0.4 firmware:

   .. code-block:: none

      AT%SHORTSWVER
      %SHORTSWVER: nrf91x1_2.0.4
      OK

#. Upload the :file:`mfw_nrf91x1_update_from_2.0.4_to_2.0.4-FOTA-TEST.bin` delta image to your HTTP(S) server.
   For HTTPS, provision the server certificate to the |SM| and note its security tag.

#. Download the image using ``AT#XFOTA=2``.
   Wait for the ``#XFOTA: 4,0`` notification, which indicates that the image is downloaded and waiting for activation.

   .. code-block:: none

      AT+CFUN=1
      OK

      AT#XFOTA=2,"https://remote.host/fota/mfw_nrf91x1_update_from_2.0.4_to_2.0.4-FOTA-TEST.bin",42
      OK
      #XFOTA: 1,0,0
      ...
      #XFOTA: 4,0

#. Set the modem to minimal function mode before the reset:

   .. code-block:: none

      AT+CFUN=0
      OK

#. Reset the |SM| or only the modem with the :ref:`SM_AT_XMODEMRESET` AT command to activate the new modem firmware.

#. Verify that the modem runs the ``2.0.4-FOTA-TEST`` firmware:

   .. code-block:: none

      AT%SHORTSWVER
      %SHORTSWVER: nrf91x1_2.0.4-FOTA-TEST
      OK

#. Repeat steps 2 to 6 with the :file:`mfw_nrf91x1_update_from_2.0.4-FOTA-TEST_to_2.0.4.bin` delta image to update back to v2.0.4.

#. Verify that the modem runs the v2.0.4 firmware again:

   .. code-block:: none

      AT%SHORTSWVER
      %SHORTSWVER: nrf91x1_2.0.4
      OK

See :ref:`SM_AT_FOTA` for the full description of the command and its notifications.

.. _nrf91m1_update_modem_full:

Modem full update
*****************

A full update replaces the whole modem firmware.
The full modem update is available only with DFU over UART.
The |SM| can only be updated forward to a newer modem firmware version (see :ref:`nrf91m1_update_rollback`).
The full modem firmware image is :file:`mfw_nrf9151-ntn_1.0.1.cbor` in the :file:`mfw_nrf9151-ntn_1.0.1.zip` package (see :ref:`nrf91m1_update_images`).

.. caution::
   The full modem update can make the modem unusable.
   If the update fails after the first 8 kB block of firmware data has been programmed, the modem remains unusable until a successful update is performed.
   Use the full modem update only to update from ``mfw_nrf91x1`` to ``mfw_nrf9151-ntn``.
   Nordic Semiconductor highly recommends performing this update in your production line, where you can control the connection between the host and the |SM|, and the power supply.

Modem full update with DFU over UART
====================================

#. Verify that the modem runs the v2.0.4 firmware:

   .. code-block:: none

      AT%SHORTSWVER
      %SHORTSWVER: nrf91x1_2.0.4
      OK

#. Send the :file:`mfw_nrf9151-ntn_1.0.1.cbor` image to the |SM| using the DFU AT commands (``AT#XDFUINIT=2``, ``AT#XDFUWRITE=2``, and ``AT#XDFUAPPLY=2``).
   The update has a bootloader segment and a firmware segment, and the |SM| reboots into bootloader mode after ``AT#XDFUINIT=2``.
   See :ref:`DFU_AT_commands` for the full command sequence.
   When you test with the DK, you can use the DFU host script instead (see :ref:`nrf91m1_update_dfu_host`).

#. Wait for the |SM| to reboot after the firmware segment is applied:

   .. code-block:: none

      Ready

#. Verify that the modem runs the ``mfw_nrf9151-ntn`` firmware:

   .. code-block:: none

      AT%SHORTSWVER
      %SHORTSWVER: nrf9151-ntn_1.0.1
      OK

.. _nrf91m1_update_mcuboot:

MCUboot update
**************

The |SM| has two copies of the MCUboot second-stage bootloader, in slots ``s0`` and ``s1``.
An update always writes the new MCUboot to the inactive slot, and the |SM| switches to it after a reset.
For this reason, the |SM| needs an MCUboot image built for the inactive slot, and the image names end with ``_s1`` for the ``s1`` slot.

.. note::
   The MCUboot update is available with DFU over UART and FOTA from an HTTP server.
   nRF Cloud FOTA does not support the MCUboot update yet.

The number of MCUboot updates is limited by the number of available monotonic counter slots, which is 40.

The following procedures update MCUboot from version 1 to version 2.
The MCUboot images in this document are provided for testing the MCUboot update.
MCUboot version 2 has the same functionality as version 1.
The |SM| always runs MCUboot version 1 from slot ``s0``, so the first update goes to slot ``s1``.
Always verify the active slot before you send an image, because the image must be built for the inactive slot.
The image for slot ``s0`` is :file:`mcuboot_nrf91m1_s0_v2-FOTA-TEST.bin`, and the image for slot ``s1`` is :file:`mcuboot_nrf91m1_s1_v2-FOTA-TEST.bin`.

The ``AT#XBOOTINFO`` command (see :ref:`SM_AT_XBOOTINFO`) returns the following information:

* ``AT#XBOOTINFO=0`` returns the MCUboot version, which is also the monotonic counter value that prevents rollback.
* ``AT#XBOOTINFO=1`` returns the active MCUboot slot: ``0`` for ``s0`` and ``1`` for ``s1``.

MCUboot update with DFU over UART
=================================

#. Verify the current MCUboot version and the active slot:

   .. code-block:: none

      AT#XBOOTINFO=0
      #XBOOTINFO: 1
      OK

      AT#XBOOTINFO=1
      #XBOOTINFO: 0
      OK

   MCUboot version 1 runs from slot ``s0``, so the inactive slot is ``s1``.

#. Send the :file:`mcuboot_nrf91m1_s1_v2-FOTA-TEST.bin` image to the |SM| using the DFU AT commands (``AT#XDFUINIT=3``, ``AT#XDFUWRITE=3``, and ``AT#XDFUAPPLY=3``).
   See :ref:`DFU_AT_commands` for the full command sequence.
   When you test with the DK, you can use the DFU host script instead (see :ref:`nrf91m1_update_dfu_host`).

#. Reset the |SM|.

#. Verify that MCUboot version 2 runs from slot ``s1``:

   .. code-block:: none

      AT#XBOOTINFO=0
      #XBOOTINFO: 2
      OK

      AT#XBOOTINFO=1
      #XBOOTINFO: 1
      OK

MCUboot update with FOTA from an HTTP server
============================================

#. Verify the current MCUboot version and the active slot:

   .. code-block:: none

      AT#XBOOTINFO=0
      #XBOOTINFO: 1
      OK

      AT#XBOOTINFO=1
      #XBOOTINFO: 0
      OK

   MCUboot version 1 runs from slot ``s0``, so the inactive slot is ``s1``.

#. Upload the :file:`mcuboot_nrf91m1_s1_v2-FOTA-TEST.bin` image to your HTTP(S) server.
   For HTTPS, provision the server certificate to the |SM| and note its security tag.

#. Download the image using ``AT#XFOTA=5``.
   Wait for the ``#XFOTA: 4,0`` notification, which indicates that the image is downloaded and waiting for activation.

   .. code-block:: none

      AT+CFUN=1
      OK

      AT#XFOTA=5,"https://remote.host/fota/mcuboot_nrf91m1_s1_v2-FOTA-TEST.bin",42
      OK
      #XFOTA: 1,0,0
      ...
      #XFOTA: 4,0

#. Set the modem to minimal function mode before the reset:

   .. code-block:: none

      AT+CFUN=0
      OK

#. Reset the |SM|.

#. Verify that MCUboot version 2 runs from slot ``s1``:

   .. code-block:: none

      AT#XBOOTINFO=0
      #XBOOTINFO: 2
      OK

      AT#XBOOTINFO=1
      #XBOOTINFO: 1
      OK

See :ref:`SM_AT_FOTA` for the full description of the command and its notifications.

Updating the same version to slot s0 (optional)
===============================================

This procedure is optional and mainly provided for testing.
When the |SM| runs MCUboot version 2 from slot ``s1``, you can update the same version to slot ``s0``.
You can use this to test the update to slot ``s0`` with the images in this document.

#. Verify that MCUboot version 2 runs from slot ``s1``:

   .. code-block:: none

      AT#XBOOTINFO=0
      #XBOOTINFO: 2
      OK

      AT#XBOOTINFO=1
      #XBOOTINFO: 1
      OK

#. Send ``mcuboot_nrf91m1_s0_v2-FOTA-TEST.bin``, the MCUboot version 2 image for slot ``s0``, to the |SM| in the same way as in the previous procedures.

#. Reset the |SM|.

   .. note::
      If you sent the image with ``AT#XFOTA=5``, the |SM| reports the error ``1`` in the ``#XFOTA: 5,1,1`` notification after the reset, because the MCUboot version did not increase.
      In this procedure, the update has still succeeded.
      Verify the result with the ``AT#XBOOTINFO`` command, as in the next step.

#. Verify that MCUboot version 2 runs from slot ``s0``:

   .. code-block:: none

      AT#XBOOTINFO=0
      #XBOOTINFO: 2
      OK

      AT#XBOOTINFO=1
      #XBOOTINFO: 0
      OK

The |SM| does not apply an MCUboot image that is built for the active slot, or whose version is lower than the running MCUboot version.
The |SM| accepts an image with the same version as the running MCUboot only when you send it to slot ``s0`` while slot ``s1`` is active.
When slot ``s0`` is active, you cannot update the same version.
