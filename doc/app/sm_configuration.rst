.. _sm_configuration:

Configuration
#############

.. contents::
   :local:
   :depth: 3

|config|

.. _sm_config_options:

Configuration options
*********************

The following configuration options are available in the |SM| application.

.. options-from-kconfig::
   :show-type:

.. _sm_config_files:

Configuration files
*******************

You can find the configuration files in the :file:`app` directory.

Configuration files have a :file:`.conf` or :file:`.overlay` extension for Kconfig or devicetree overlays, respectively.
Board-specific configuration files are named :file:`<BOARD>` with a :file:`.conf` or :file:`.overlay` extension and are located in the :file:`boards` directory.
When the name of the board-specific configuration file matches the board target, the overlay is automatically included by the build system.

See :ref:`Build and configuration system <nrf:app_build_system>` for more information on the |NCS| configuration system.

.. important::

  When adding Kconfig fragments and devicetree overlays, make sure to use the :makevar:`EXTRA_CONF_FILE` and :makevar:`EXTRA_DTC_OVERLAY_FILE` :ref:`CMake options <nrf:cmake_options>`, respectively.
  Otherwise, if :makevar:`CONF_FILE` or :makevar:`DTC_OVERLAY_FILE` is used, all the configuration files that normally get picked up automatically will have to be included explicitly.

The following configuration files are provided:

* :file:`prj.conf` - This configuration file contains the standard configuration for the |SM| application and is included by default by the build system.

* :file:`external-mcu.overlay` - This configures the |SM| application to communicate with external MCU over ``uart2``, using specific pins for UART, DTR, and RI.
  The overlay is pin compatible with nRF9151DK.
  For other setups, you can customize the overlay to fit your configuration.

* :file:`nrf91m1.overlay` - Devicetree overlay for use with the nRF91M1 SiP.
  Configures ``uart0`` and ``uart1`` with pins suitable for nRF9151 DK usage and for connecting to an external MCU.
  See :ref:`uart_configuration_nrf91m1` for pin details.

* :file:`interposer-nrf9151-mk-bg95.overlay` - Devicetree overlay for an nRF9151 SiP on an nRF9151-MK-BG95 interposer board.
  Configures the ``uart0`` pins to match the interposer footprint and removes ``hw-flow-control``, as the interposer does not use the RTS and CTS signals for flow control.
  See :ref:`uart_configuration_interposer` for pin details.

* :file:`interposer-nrf9151-mk-gm02s.overlay` - Devicetree overlay for an nRF9151 SiP on an nRF9151-MK-GM02S interposer board.
  Configures the ``uart0`` pins to match the interposer footprint and moves the DTR pin off the UART0 TX pin.
  See :ref:`uart_configuration_interposer` for pin details.

* :file:`carrier.conf` - Configuration file that adds |NCS| :ref:`LwM2M carrier <nrf:liblwm2m_carrier_readme>` support.
  Used in conjunction with :file:`carrier-nrf9151.overlay`.
  See :ref:`sm_carrier_library_support` for more information on how to connect to an operator's device management platform.

* :file:`carrier-nrf9151.overlay` - Devicetree overlay that adds a dedicated flash partition for the LwM2M carrier library NVS storage.
  Used in conjunction with :file:`carrier.conf`.
  It does not reserve a ``memfault_coredump_partition``, so the Memfault coredump falls back to RAM in this configuration.

* :file:`carrier-softbank.conf` and :file:`sysbuild-softbank.conf` - Configuration files that add SoftBank configurations needed by the carrier library.
  Used in conjunction with :file:`carrier.conf` and :file:`carrier-nrf9151.overlay`.
  For more information, see the :ref:`Carrier-specific dependencies <nrf:lwm2m_carrier_dependent>` section of the :ref:`LwM2M carrier <nrf:liblwm2m_carrier_readme>` documentation.

* :file:`carrier-lgu.conf` - This configuration file adds LG U+ configurations needed by the carrier library.
  Used in conjunction with :file:`carrier.conf` and :file:`carrier-nrf9151.overlay`.
  For more information, see the :ref:`Carrier-specific dependencies <nrf:lwm2m_carrier_dependent>` section of the :ref:`LwM2M carrier <nrf:liblwm2m_carrier_readme>` documentation.

* :file:`cmux.conf` - Configuration file that adds support for the CMUX protocol.
  See :ref:`SM_AT_CMUX` for more information.

* :file:`ppp.conf` - Configuration file that adds support for the Point-to-Point Protocol (PPP).
  This disables most of the IP-based protocols available through AT commands (such as MQTT) as it is expected that the controlling chip's own IP stack is used instead.
  See :option:`CONFIG_SM_PPP` and :ref:`SM_AT_PPP` for more information.

* :file:`trace-backend-cmux.conf` - Configuration file that enables CMUX modem trace backend.
  When enabled, modem traces are transmitted on a dedicated CMUX channel.
  Must be combined with :file:`trace-backend-nrf9151.overlay`.
  See the :ref:`sm_modem_trace_cmux` documentation for more information.

* :file:`trace-backend-uart.conf` - Configuration file that enables the UART modem trace backend.
  Backend starts disabled and is activated at runtime using ``AT#XTRACE``.
  Must be combined with :file:`trace-backend-nrf9151.overlay`.
  See the :ref:`sm_logging_uart_backend` documentation for more information.

* :file:`trace-backend-nrf9151.overlay` - Devicetree overlay that defines the SRAM partition used by the modem trace backend.
  Required when using :file:`trace-backend-uart.conf` or :file:`trace-backend-cmux.conf`.

* :file:`full-fota.conf` - Configuration file that adds full modem FOTA support.
  Must be combined with :file:`full-fota-nrf9151.overlay`.
  See :ref:`SM_AT_FOTA` for more information on how to use full modem FOTA functionality.

* :file:`full-fota-nrf9151.overlay` - Devicetree overlay that adds a dedicated flash partition for modem firmware storage in external flash.
  Must be combined with :file:`full-fota.conf`.

* :file:`pgps-nrf9151.overlay` - Devicetree overlay that adds a dedicated flash partition for P-GPS prediction data storage.
  The overlay resizes the MCUboot primary and secondary slots to free up space for the ``pgps_partition``.
  Required when :kconfig:option:`CONFIG_NRF_CLOUD_PGPS` is enabled.
  Must also be passed to the ``mcuboot`` image using :makevar:`mcuboot_EXTRA_DTC_OVERLAY_FILE` so that MCUboot operates with the same partition layout.
  The absolute path to the overlay file must be provided.
  It does not reserve a ``memfault_coredump_partition``, so the Memfault coredump falls back to RAM in this configuration.
  See :ref:`SM_AT_GNSS` for more information.

* :file:`disable-b0-nrf9151.overlay` - Devicetree overlay for a build configuration without the NSIB (B0) immutable bootloader.
  This is intended only for upgrading v1.x.x devices to v2.x.x firmware.
  Must be combined with ``SB_CONFIG_SECURE_BOOT_APPCORE=n`` in the sysbuild configuration and passed to the ``mcuboot`` image using :makevar:`mcuboot_EXTRA_DTC_OVERLAY_FILE` (using the absolute path).
  It does not reserve a ``memfault_coredump_partition``, so the Memfault coredump falls back to RAM in this configuration.

* :file:`disable-dtr.overlay` - Devicetree overlay that disables the DTR and RI pins and related functionality.
  This overlay can be used if your setup does not have the need or means for managing the power externally.
  Modify the overlay to fit your configuration.

* :file:`nrf-device-provisioning.conf` - Configuration file that enables the nRF Cloud device provisioning client and its AT command interface.
  See :ref:`SM_AT_PROVISIONING` for more information.

The board-specific devicetree overlays (:file:`boards/*.overlay`) set up configurations that are specific to each supported development kit.
All of them configure the DTR to be deasserted from a button and RI to drive an LED.

.. _sm_partition_layouts:

Flash and SRAM partition layouts
********************************

The |SM| application uses DTS-defined partitions instead of the |NCS| Partition Manager.
The base partition layout for the nRF9151 DK is defined in :file:`boards/nrf9151dk_nrf9151_ns.overlay`.
Several DTS overlay files extend or replace parts of the layout for specific build configurations.
