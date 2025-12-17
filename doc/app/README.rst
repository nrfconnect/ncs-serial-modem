.. _serial_modem_app:

|SM| application
################

The |SM| (SM) application can be used to emulate a stand-alone modem on an nRF91 Series device.
This allows you to run your application on a separate host MCU.
The application accepts both the modem specific AT commands and proprietary AT commands.
The AT commands are documented in the following guides:

* Modem specific AT commands - `nRF91x1 AT Commands Reference Guide`_
* Proprietary AT commands - :ref:`SM_AT_commands`

The application supports the following development kits:

.. list-table::
   :widths: auto
   :header-rows: 1

   * - Hardware platforms
     - PCA
     - Board name
     - Board target
   * - `nRF9151 DK <nRF91 DK_>`_
     - PCA10171
     - `nrf9151dk`_
     - ``nrf9151dk/nrf9151/ns``
   * - `Thingy:91 X <Thingy91X_>`_
     - PCA20065
     - `thingy91x <Thingy91X_>`_
     - ``thingy91x/nrf9151/ns``

For more security, the application must use the ``*/ns`` `variant <app_boards_names_>`_ of the board target, which is required when using the nRF91 Series `Modem library`_.
When built for this variant, the sample is configured to compile and run as a non-secure application using `security by separation <ug_tfm_security_by_separation_>`_.
Therefore, it automatically includes `Trusted Firmware-M <ug_tfm_>`_ that prepares the required peripherals and secure services to be available for the application.

This application has the following dependencies:

* This application uses the following |NCS| libraries:

  * `AT parser`_
  * `AT monitor`_
  * `Modem library integration layer`_
  * `Modem JWT`_
  * `SMS`_
  * `FOTA download`_
  * `Downloader`_
  * `nRF Cloud`_
  * `nRF Cloud A-GNSS`_
  * `nRF Cloud P-GPS`_
  * `nRF Cloud location`_

* It uses the following `sdk-nrfxlib`_ libraries:

  * `Modem library`_

* In addition, it uses the following secure firmware component:

  * `Trusted Firmware-M`_

See the subpages for how to use the application, how to extend it, and information on the supported AT commands.

.. toctree::
   :maxdepth: 2
   :caption: Subpages:

   sm_build_and_run
   sm_logging
   sm_configuration
   nRF91_as_Zephyr_modem
   PPP_linux
   sm_data_mode
   sm_extending
   sm_testing
