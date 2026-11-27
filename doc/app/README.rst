.. _serial_modem_app:

|SM| application
################

The |SM| (SM) application can be used to emulate a stand-alone modem on an nRF91 Series device.
This allows you to run your application on a separate host MCU.
The application accepts both the modem specific AT commands and proprietary AT commands.
The AT commands are documented in the following guides:

* Modem specific AT commands

  * ``mfw_nrf91x1`` - `nRF91x1 AT Commands Reference Guide`_
  * ``mfw_nrf9151-ntn`` - `nRF91x1 NTN AT Commands Reference Guide`_

* Proprietary AT commands - :ref:`SM_AT_commands`

The application supports the following modem firmware:

* ``mfw_nrf91x1`` v2.0.4 or later
* ``mfw_nrf9151-ntn``

The application supports the following development kits:

.. list-table::
   :widths: auto
   :header-rows: 1

   * - Hardware platforms
     - PCA
     - Board name
     - Board target
     - SW maturity level
   * - :ref:`nRF9151 DK <nrf:ug_nrf91>`
     - PCA10171
     - :doc:`nrf9151dk <zephyr:boards/nordic/nrf9151dk/doc/index>`
     - ``nrf9151dk/nrf9151/ns``
     - :ref:`Supported <nrf:software_maturity>`
   * - :ref:`Thingy:91 X <nrf:ug_thingy91x>`
     - PCA20065
     - :ref:`thingy91x <nrf:ug_thingy91x>`
     - ``thingy91x/nrf9151/ns``
     - :ref:`Experimental <nrf:software_maturity>`

For more security, the application must use the ``*/ns`` :ref:`variant <nrf:app_boards_names>` of the board target, which is required when using the nRF91 Series :ref:`Modem library <nrfxlib:nrf_modem>`.
When built for this variant, the sample is configured to compile and run as a non-secure application using :ref:`security by separation <nrf:ug_tfm_security_by_separation>`.
Therefore, it automatically includes :ref:`Trusted Firmware-M <nrf:ug_tfm>` that prepares the required peripherals and secure services to be available for the application.

This application has the following dependencies:

* This application uses the following |NCS| libraries:

  * :ref:`AT parser <nrf:at_parser_readme>`
  * :ref:`AT monitor <nrf:at_monitor_readme>`
  * :ref:`Modem library integration layer <nrf:nrf_modem_lib_readme>`
  * :ref:`Modem JWT <nrf:lib_modem_jwt>`
  * :ref:`SMS <nrf:sms_readme>`
  * :ref:`FOTA download <nrf:lib_fota_download>`
  * :ref:`Downloader <nrf:lib_downloader>`
  * :ref:`nRF Cloud <nrf:lib_nrf_cloud>`
  * :ref:`nRF Cloud A-GNSS <nrf:lib_nrf_cloud_agnss>`
  * :ref:`nRF Cloud P-GPS <nrf:lib_nrf_cloud_pgps>`
  * :ref:`nRF Cloud location <nrf:lib_nrf_cloud_location>`
  * :ref:`nRF Cloud device provisioning <nrf:lib_nrf_provisioning>`

* It uses the following `sdk-nrfxlib`_ libraries:

  * :ref:`Modem library <nrfxlib:nrf_modem>`

* In addition, it uses the following secure firmware component:

  * :ref:`Trusted Firmware-M <nrf:ug_tfm>`

See the subpages for how to use the application, how to extend it, and information on the supported AT commands.

.. toctree::
   :maxdepth: 2
   :caption: Subpages:

   sm_build_and_run
   sm_logging
   sm_configuration
   sm_cellular_modem
   sm_releasing
   sm_extending
   sm_testing
