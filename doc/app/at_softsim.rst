.. _SM_AT_SOFTSIM:

SoftSIM AT commands
*******************

.. contents::
   :local:
   :depth: 1

This page describes the AT commands for the `Onomondo SoftSIM`_.
The SoftSIM replaces the physical SIM card with a SIM implementation that runs on the application core.
The SIM profile is stored in a dedicated flash partition, and the SIM keys are stored in the TF-M Internal Trusted Storage.

Building with SoftSIM
=====================

SoftSIM support is available for the nRF9151 DK.
To build the |SM| with SoftSIM, use the following files:

* The :file:`overlay-softsim.conf` Kconfig fragment from the SoftSIM module, with the SoftSIM settings.
* The :file:`softsim.conf` Kconfig fragment, with the |SM| settings for SoftSIM.
* The :file:`softsim-nrf9151.overlay` flash partition overlay.
  MCUboot also uses this overlay, because the image slots are smaller than in the default layout.
* The :file:`softsim-sram-nrf9151.overlay` SRAM overlay.

.. code-block:: console

   west build -p -b nrf9151dk/nrf9151/ns -- '-DEXTRA_CONF_FILE=${ZEPHYR_SOFTSIM_MODULE_DIR}/overlay-softsim.conf;softsim.conf' -DEXTRA_DTC_OVERLAY_FILE="softsim-nrf9151.overlay;softsim-sram-nrf9151.overlay" -Dmcuboot_EXTRA_DTC_OVERLAY_FILE=<path-to-app>/softsim-nrf9151.overlay -DSB_CONFIG_SOFTSIM_BUNDLE_TEMPLATE_HEX=y

A new or fully erased device needs the SoftSIM filesystem template in the ``nvs_storage`` partition.
Without the template, the SoftSIM filesystem fails to initialize and the ``#XSOFTSIMPROVISION`` set command returns ``ERROR``.
The build generates the template in the :file:`build/app/onomondo-softsim/template.hex` file.
The ``SB_CONFIG_SOFTSIM_BUNDLE_TEMPLATE_HEX`` option also merges the template with the firmware into the :file:`build/merged.hex` file.

The ``west flash`` command does not program the template.
To program the firmware and the template on a new device, use the :file:`build/merged.hex` file:

.. code-block:: console

   nrfutil device program --firmware build/merged.hex --options chip_erase_mode=ERASE_ALL

To add the template to a device that already runs the firmware, program only the template:

.. code-block:: console

   nrfutil device program --firmware build/app/onomondo-softsim/template.hex --options chip_erase_mode=ERASE_RANGES_TOUCHED_BY_FIRMWARE

.. note::
   Programming the template overwrites the SoftSIM filesystem, which erases a provisioned profile.
   To update the firmware on a provisioned device, use ``west flash`` or a firmware update.

Provision a SoftSIM profile #XSOFTSIMPROVISION
==============================================

The ``#XSOFTSIMPROVISION`` command provisions a SoftSIM profile and reports whether a profile is provisioned.

Set command
-----------

The set command provisions a SoftSIM profile.
The modem must be in the power-off functional mode (``AT+CFUN=0``).
The modem uses the new SIM identity when it is activated the next time.

Syntax
~~~~~~

::

   AT#XSOFTSIMPROVISION=<profile>

The ``<profile>`` parameter is a string.
It contains the SoftSIM profile as a hexadecimal string, as provided by Onomondo.
The profile contains the IMSI, ICCID, and SIM keys.
The maximum length is 512 characters.

The |SM| redacts the profile from the UART log.

Response syntax
~~~~~~~~~~~~~~~

::

   OK

The command returns ``ERROR`` if the modem is not in the power-off functional mode, or if the profile is not valid.

Example
~~~~~~~

::

   AT+CFUN=0
   OK

   AT#XSOFTSIMPROVISION="0112082943051220435931021498..."
   OK

   AT+CFUN=1
   OK

Read command
------------

The read command reports whether a SoftSIM profile is provisioned.

Syntax
~~~~~~

::

   AT#XSOFTSIMPROVISION?

Response syntax
~~~~~~~~~~~~~~~

::

   #XSOFTSIMPROVISION: <provisioned>

The ``<provisioned>`` parameter is an integer.
It can have one of the following values:

* ``0`` - No SoftSIM profile is provisioned.
* ``1`` - A SoftSIM profile is provisioned.

Example
~~~~~~~

::

   AT#XSOFTSIMPROVISION?

   #XSOFTSIMPROVISION: 1

   OK

Test command
------------

The test command returns the command syntax.

Syntax
~~~~~~

::

   AT#XSOFTSIMPROVISION=?

Response syntax
~~~~~~~~~~~~~~~

::

   #XSOFTSIMPROVISION: <profile>

Example
~~~~~~~

::

   AT#XSOFTSIMPROVISION=?

   #XSOFTSIMPROVISION: <profile>

   OK
