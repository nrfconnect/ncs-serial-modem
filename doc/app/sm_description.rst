.. _sm_description:

Application description
#######################

.. contents::
   :local:
   :depth: 3

The |SM| (SM) application demonstrates how to use an nRF91 Series device as a stand-alone modem that can be controlled by AT commands.

Overview
********

The nRF91 Series SiP integrates both a full LTE modem and an application MCU, enabling you to run your application directly on the device.

However, if you want to run your application on a different chip and use the nRF91 Series device only as a modem, the |SM| application provides you with an interface for controlling the modem through AT commands.

The application accepts both the modem specific AT commands and proprietary AT commands.
The AT commands are documented in the following guides:

* Modem specific AT commands - `nRF91x1 AT Commands Reference Guide`_
* Proprietary AT commands - :ref:`SM_AT_commands`

Requirements
************

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

Configuration
*************

|config|

.. _sm_config_options:

Configuration options
=====================

.. _CONFIG_SM_CUSTOMER_VERSION:

CONFIG_SM_CUSTOMER_VERSION - Customer version string
   Version string defined by the customer after customizing the application.
   When defined, this version is reported with the baseline versions by the ``#XSMVER`` AT command.

.. _CONFIG_SM_AT_MAX_PARAM:

CONFIG_SM_AT_MAX_PARAM - AT command parameter count limit
   This defines the maximum number of parameters allowed in an AT command, including the command name.

.. _CONFIG_SM_AT_BUF_SIZE:

CONFIG_SM_AT_BUF_SIZE - AT command buffer size
   This option defines the size of the buffer for incoming AT commands and modem responses.

.. _CONFIG_SM_CMUX:

CONFIG_SM_CMUX - Enable CMUX functionality
   This option is enabled by the CMUX overlay.
   It adds support for CMUX.
   See :ref:`SM_AT_CMUX` for more information.

.. _CONFIG_SM_PPP:

CONFIG_SM_PPP - Enable PPP functionality
   This option is enabled by the PPP overlay.
   It adds support for PPP.
   PPP can be used in conjunction with :ref:`CMUX <CONFIG_SM_CMUX>` in order to use a single UART for both AT data and PPP.
   When CMUX is also enabled, PPP is usable only through a CMUX channel.
   See :ref:`SM_AT_PPP` for more information.

.. _CONFIG_SM_EXTERNAL_XTAL:

CONFIG_SM_EXTERNAL_XTAL - Use external XTAL for UARTE
   This option configures the application to use an external XTAL for UARTE.
   For more information, see the UARTE - Universal asynchronous receiver/transmitter with EasyDMA section of the `nRF9151 Product Specification`_ documentation.

.. _CONFIG_SM_AUTO_CONNECT:

CONFIG_SM_AUTO_CONNECT - Connect to the network at start-up or reset
   This option enables connecting to the network at start-up or reset using a defined PDN configuration.
   This option is enabled by the LwM2M Carrier overlay, but is otherwise disabled by default.

   .. note::
      This option requires network-specific configuration in the ``sm_auto_connect.h`` file.

   Here is a sample configuration for NIDD connection in the :file:`sm_auto_connect.h` file::

      /* Network-specific default system mode configured by %XSYSTEMMODE (refer to AT command manual) */
      0,        /* LTE support */
      1,        /* NB-IoT support */
      0,        /* GNSS support, also define CONFIG_MODEM_ANTENNA if not Nordic DK */
      0,        /* LTE preference */
      /* Network-specific default PDN configured by +CGDCONT and +CGAUTH (refer to AT command manual) */
      true,     /* PDP context definition required or not */
      "Non-IP", /* PDP type: "IP", "IPV6", "IPV4V6", "Non-IP" */
      "",       /* Access point name */
      0,        /* PDP authentication protocol: 0(None), 1(PAP), 2(CHAP) */
      "",       /* PDN connection authentication username */
      ""        /* PDN connection authentication password */

.. _CONFIG_SM_CR_TERMINATION:

CONFIG_SM_CR_TERMINATION - CR termination
   This option configures the application to accept AT commands ending with a carriage return.
   This is the default AT command terminator.

   Select this option if you want to connect to the development kit using PuTTY.
   See `Testing and optimization`_  for instructions.

.. _CONFIG_SM_LF_TERMINATION:

CONFIG_SM_LF_TERMINATION - LF termination
   This option configures the application to accept AT commands ending with a line feed.

.. _CONFIG_SM_CR_LF_TERMINATION:

CONFIG_SM_CR_LF_TERMINATION - CR+LF termination
   This option configures the application to accept AT commands ending with a carriage return followed by a line feed.

.. _CONFIG_SM_TCP_POLL_TIME:

CONFIG_SM_TCP_POLL_TIME - Poll timeout in seconds for TCP connection
   This option specifies the poll timeout for the TCP connection, in seconds.

.. _CONFIG_SM_SMS:

CONFIG_SM_SMS - SMS support in |SM|
   This option enables additional AT commands for using the SMS service.

.. _CONFIG_SM_GNSS:

CONFIG_SM_GNSS - GNSS support in |SM|
   This option enables additional AT commands for using the GNSS service.

.. _CONFIG_SM_NRF_CLOUD:

CONFIG_SM_NRF_CLOUD - nRF Cloud support in |SM|
   This option enables additional AT commands for using the nRF Cloud service.

.. _CONFIG_SM_MQTTC:

CONFIG_SM_MQTTC - MQTT client support in |SM|
   This option enables additional AT commands for using the MQTT client service.

.. _CONFIG_SM_MQTTC_MESSAGE_BUFFER_LEN:

CONFIG_SM_MQTTC_MESSAGE_BUFFER_LEN - Size of the buffer for the MQTT library
   This option specifies the maximum message size which can be transmitted or received through MQTT (excluding PUBLISH payload).
   The default value is 512, meaning 512 bytes for TX and RX, respectively.

.. _CONFIG_SM_UART_RX_BUF_COUNT:

CONFIG_SM_UART_RX_BUF_COUNT - Receive buffers for UART.
   This option defines the number of buffers for receiving (RX) UART traffic.
   The default value is 3.

.. _CONFIG_SM_UART_RX_BUF_SIZE:

CONFIG_SM_UART_RX_BUF_SIZE - Receive buffer size for UART.
   This option defines the size of a single buffer for receiving (RX) UART traffic.
   The default value is 256.

.. _CONFIG_SM_UART_TX_BUF_SIZE:

CONFIG_SM_UART_TX_BUF_SIZE - Send buffer size for UART.
   This option defines the size of the buffer for sending (TX) UART traffic.
   The default value is 256.

.. _CONFIG_SM_PPP_FALLBACK_MTU:

CONFIG_SM_PPP_FALLBACK_MTU - Control the MTU used by PPP.
   This option controls the MTU used by PPP.
   PPP tries to retrieve the cellular link MTU from the modem (using ``AT+CGCONTRDP``).
   If MTU is not returned by the modem, this value will be used as a fallback.
   The MTU will be used for sending and receiving data on both the PPP and cellular links.
   The default value is 1280.

.. _CONFIG_SM_CARRIER_AUTO_STARTUP:

CONFIG_SM_CARRIER_AUTO_STARTUP - Enable automatic startup on boot.
   This option enables automatic startup of the library on device boot.
   If this configuration is disabled, automatic startup is controlled through a dedicated AT command.

.. _CONFIG_SM_PGPS_INJECT_FIX_DATA:

CONFIG_SM_PGPS_INJECT_FIX_DATA - Injects the data obtained when acquiring a fix.
   This option, if enabled, when a fix is acquired the current location and time will be passed to the P-GPS subsystem.
   It allows you to speed up the time it takes to acquire the next fix when A-GNSS is disabled or unavailable.
   This can be detrimental to short TTFF if the device is expected to move distances longer than a few dozen kilometers between fix attempts.
   In that case, this option should be disabled.
   The default value is ``y``.


.. _sm_logging:

Logging
=======

|SM| uses the SEGGER Real-Time Transfer (RTT) for application logging.
You can view the RTT logs with an RTT client such as ``J-Link RTT Viewer``.
See `Testing and optimization`_ for instructions on how to view the logs.

.. note::
   The negative error codes that are visible in logs are *errno* codes defined in `nrf_errno.h`_.

By default, the |SM| uses the ``UART0`` for sending and receiving AT commands.
If a different UART is used, the application log can be output through ``UART0`` while AT commands are sent and received through the other UART.
To switch to ``UART0`` output for application logs, change the following options in the :file:`prj.conf` file::

   # Segger RTT
   CONFIG_USE_SEGGER_RTT=n
   CONFIG_RTT_CONSOLE=n
   CONFIG_UART_CONSOLE=y
   CONFIG_LOG_BACKEND_RTT=n
   CONFIG_LOG_BACKEND_UART=y

.. _sm_config_files:

Configuration files
===================

You can find the configuration files in the :file:`app` directory.

In general, they have an ``overlay-`` prefix, and a :file:`.conf` or :file:`.overlay` extension for Kconfig or devicetree overlays, respectively.
Board-specific configuration files are named :file:`<BOARD>` with a :file:`.conf` or :file:`.overlay` extension and are located in the :file:`boards` directory.
When the name of the board-specific configuration file matches the board target, the overlay is automatically included by the build system.

See `Build and configuration system`_ for more information on the |NCS| configuration system.

.. important::

  When adding Kconfig fragments and devicetree overlays, make sure to use the :makevar:`EXTRA_CONF_FILE` and :makevar:`EXTRA_DTC_OVERLAY_FILE` `CMake options <cmake_options_>`_, respectively.
  Otherwise, if :makevar:`CONF_FILE` or :makevar:`DTC_OVERLAY_FILE` is used, all the configuration files that normally get picked up automatically will have to be included explicitly.

The following configuration files are provided:

* :file:`prj.conf` - This configuration file contains the standard configuration for the |SM| application and is included by default by the build system.

* :file:`overlay-external-mcu.overlay` - This configures the |SM| application to communicate with external MCU over ``uart2``, using specific pins for UART, DTR, and RI.
  The overlay is pin compatible with nRF9151DK.
  For other setups, you can customize the overlay to fit your configuration.

* :file:`overlay-carrier.conf` - Configuration file that adds |NCS| `LwM2M carrier`_ support.
  See :ref:`sm_carrier_library_support` for more information on how to connect to an operator's device management platform.

* :file:`overlay-carrier-softbank.conf` and :file:`sysbuild-softbank.conf` - Configuration files that add SoftBank configurations needed by the carrier library.
  Used in conjunction with :file:`overlay-carrier.conf`.
  For more information, see the `Carrier-specific dependencies`_ section of the `LwM2M carrier`_ documentation.
* :file:`overlay-carrier-lgu.conf` - This configuration file adds LG U+ configurations needed by the carrier library.
  Used in conjunction with :file:`overlay-carrier.conf`.
  For more information, see the `Carrier-specific dependencies`_ section of the `LwM2M carrier`_ documentation.

* :file:`overlay-full_fota.conf` - Configuration file that adds full modem FOTA support.
  See :ref:`SM_AT_FOTA` for more information on how to use full modem FOTA functionality.

* :file:`overlay-cmux.conf` - Configuration file that adds support for the CMUX protocol.
  See :ref:`SM_AT_CMUX` for more information.

* :file:`overlay-ppp.conf` - Configuration file that adds support for the Point-to-Point Protocol (PPP).
  This disables most of the IP-based protocols available through AT commands (such as MQTT) as it is expected that the controlling chip's own IP stack is used instead.
  See :ref:`CONFIG_SM_PPP <CONFIG_SM_PPP>` and :ref:`SM_AT_PPP` for more information.

* :file:`overlay-ppp-without-cmux.conf` - Configuration file that enables support for the second UART to be used by PPP.
  This configuration file should be included when building |SM| with PPP and without CMUX, in addition to :file:`overlay-ppp.conf` and :file:`overlay-ppp-without-cmux.overlay`.

* :file:`overlay-ppp-without-cmux.overlay` - Devicetree overlay that configures the second UART to be used by PPP.
  This configuration file should be included when building |SM| with PPP and without CMUX, in addition to :file:`overlay-ppp.conf` and :file:`overlay-ppp-without-cmux.conf`.
  It can be customized to fit your configuration, such as UART settings, baud rate, and flow control.
  By default, it sets the baud rate of the PPP UART to 1 000 000.

* :file:`overlay-memfault.conf` - Configuration file that enables `Memfault`_.
  For more information about Memfault features in |NCS|, see the `Memfault library`_ docs.

* :file:`overlay-disable-dtr.overlay` - Devicetree overlay that disables the DTR and RI pins and related functionality.
  This overlay can be used if your setup does not have the need or means for managing the power externally.
  Modify the overlay to fit your configuration.

The board-specific devicetree overlays (:file:`boards/*.overlay`) set up configurations that are specific to each supported development kit.
All of them configure the DTR to be deasserted from a button and RI to blink an LED.

Sending traces over UART on an nRF91 Series DK
==============================================

To send modem traces over UART on an nRF91 Series DK, configuration must be added for the UART device in the devicetree and Kconfig.
This is done by adding the `modem trace UART snippet`_ when building and programming.

Use the `Cellular Monitor app`_ for capturing and analyzing modem traces.

TF-M logging must use the same UART as the application. For more details, see `shared TF-M logging`_.

.. _sm_building:

Building and running
********************

The |SM| application can be found under :file:`app` in the |SM| folder structure.

For more security, it is recommended to use the ``*/ns`` `variant <app_boards_names_>`_ of the board target (see the Requirements section above.)
When built for this variant, the sample is configured to compile and run as a non-secure application using `security by separation <ug_tfm_security_by_separation_>`_.
Therefore, it automatically includes `Trusted Firmware-M <ug_tfm_>`_ that prepares the required peripherals and secure services to be available for the application.

To build the sample, follow the instructions in `Building an application`_ for your preferred building environment.
See also `Programming an application`_ for programming steps and `Testing and optimization`_ for general information about testing and debugging in the |NCS|.

.. note::
   |sysbuild_autoenabled_ncs|

.. _sm_connecting_91dk:

Communicating with the modem on an nRF91 Series DK
==================================================

In this scenario, an nRF91 Series DK running the |SM| application serves as the host.
You can use either a PC or an external MCU as a client.

.. _sm_connecting_91dk_pc:

Connecting with a PC
--------------------

To connect to an nRF91 Series DK with a PC:

.. sm_connecting_91dk_pc_instr_start

1. Verify that ``UART_0`` is selected in the application.
   It is defined in the default configuration.

2. Use the `Serial Terminal app`_ to connect to the development kit.
   See `Testing and optimization`_ for instructions.
   You can also use the :guilabel:`Open Serial Terminal` option of the `Cellular Monitor app`_ to open the Serial Terminal app.
   Using the Cellular Monitor app in combination with the Serial Terminal app shows how the modem responds to the different modem commands.
   You can then use this connection to send or receive AT commands over UART, and to see the log output of the development kit.

   Instead of using the Serial Terminal app, you can use PuTTY to establish a terminal connection to the development kit, using the `default serial port connection settings <Testing and optimization_>`_.

   .. note::

      The default AT command terminator is a carriage return (``\r``).
      The Serial Terminal app, PuTTY and many terminal emulators support this format by default.
      However, make sure that the configured AT command terminator corresponds to the line terminator of your terminal.
      See :ref:`sm_config_options` for more details on AT command terminator choices.

.. sm_connecting_91dk_pc_instr_end

.. _sm_connecting_91dk_mcu:

Connecting with an external MCU
-------------------------------

.. note::

   This section does not apply to Thingy:91 X.

If you run your user application on an external MCU (for example, an nRF52 Series development kit), you can control the |SM| application on an nRF91 Series device directly from the application.
See the :ref:`sm_shell_sample` for a sample implementation of such an application.

To connect with an external MCU using UART_2, include the :file:`overlay-external-mcu.overlay` devicetree overlay in your build.
This overlay configures the UART_2 pins, DTR pin, and RI pin for the nRF9151 DK.

If you use a different setup, you can customize the :file:`overlay-external-mcu.overlay` file to match your hardware configuration in (for example) the following ways:

* Change the highlighted UART baud rate or DTR and RI pins::

   &uart2 {
      compatible = "nordic,nrf-uarte";
      current-speed = <**115200**>;
      hw-flow-control;
      status = "okay";

      dtr_uart2: dtr-uart {
         compatible = "nordic,dtr-uart";
         dtr-gpios = <**&gpio0 31** (GPIO_PULL_UP | GPIO_ACTIVE_LOW)>;
         ri-gpios = <**&gpio0 30** GPIO_ACTIVE_LOW>;
         status = "okay";
      };
      pinctrl-0 = <&uart2_default_alt>;
      pinctrl-1 = <&uart2_sleep_alt>;
      pinctrl-names = "default", "sleep";
   };

* Change the highlighted UART pins::

   &pinctrl {
      uart2_default_alt: uart2_default_alt {
         group1 {
            psels = <NRF_PSEL(UART_RX, **0, 11**)>,
                    <NRF_PSEL(UART_CTS, **0, 13**)>;
            bias-pull-up;
         };
         group2 {
            psels = <NRF_PSEL(UART_TX, **0, 10**)>,
                    <NRF_PSEL(UART_RTS, **0, 12**)>;
         };
      };

      uart2_sleep_alt: uart2_sleep_alt {
         group1 {
            psels = <NRF_PSEL(UART_TX, **0, 10**)>,
                    <NRF_PSEL(UART_RX, **0, 11**)>,
                    <NRF_PSEL(UART_RTS, **0, 12**)>,
                    <NRF_PSEL(UART_CTS, **0, 13**)>;
            low-power-enable;
         };
      };
   };

The following table shows how to connect selected development kit to an nRF91 Series development kit to be able to communicate through UART:

.. tabs::

   .. group-tab:: nRF54L15 DK

      .. list-table::
         :header-rows: 1

         * - nRF54L15 DK
           - nRF91 Series DK
         * - UART TX P0.00
           - UART RX P0.11
         * - UART RX P0.01
           - UART TX P0.10
         * - UART CTS P0.03
           - UART RTS P0.12
         * - UART RTS P0.02
           - UART CTS P0.13
         * - DTR OUT P1.11
           - DTR IN P0.31
         * - RI IN P1.12
           - RI OUT P0.30
         * - GPIO GND
           - GPIO GND

      .. note::
         You must disable the VCOM0 on the nRF54L15 DK to release the UART GPIO pins to use it with the :ref:`sm_shell_sample`.

         * For nRF54L15 DK, you can use the `Board Configurator app`_ to disable the `Connect port VCOM0` setting.

      .. note::
         The GPIO output levels on the nRF91 Series device and nRF54L15 DK must be the same.

         * You can set the VDD voltages for both devices with the `Board Configurator app`_.

   .. group-tab:: nRF52 DK

      .. list-table::
         :header-rows: 1

         * - nRF52 Series DK
           - nRF91 Series DK
         * - UART TX P1.02
           - UART RX P0.11
         * - UART RX P1.01
           - UART TX P0.10
         * - UART CTS P1.07
           - UART RTS P0.12
         * - UART RTS P1.06
           - UART CTS P0.13
         * - DTR OUT P0.11
           - DTR IN P0.31
         * - RI IN P0.13
           - RI OUT P0.30
         * - GPIO GND
           - GPIO GND

      .. note::
         The GPIO output level on the nRF91 Series device side must be 3 V.

         * For nRF9151 DK, you can set the VDD voltage with the `Board Configurator app`_.

   .. group-tab:: nRF53 DK

      .. list-table::
         :header-rows: 1

         * - nRF53 Series DK
           - nRF91 Series DK
         * - UART TX P1.04
           - UART RX P0.11
         * - UART RX P1.05
           - UART TX P0.10
         * - UART CTS P1.07
           - UART RTS P0.12
         * - UART RTS P1.06
           - UART CTS P0.13
         * - DTR OUT P0.23
           - DTR IN P0.31
         * - RI IN P0.28
           - RI OUT P0.30
         * - GPIO GND
           - GPIO GND

      .. note::
         The GPIO output level on the nRF91 Series device side must be 3 V.

         * For nRF9151 DK, you can set the VDD voltage with the `Board Configurator app`_.

Use the following UART devices:

* nRF54, nRF52 or nRF53 Series DK - UART0
* nRF91 Series DK - UART2

The UART configuration must match on both sides.
By default the |SM| application and :ref:`sm_shell_sample` use the following settings:

* Hardware flow control: enabled
* Baud rate: 115200
* Parity bit: no

Communicating with the modem on Thingy:91 X
===========================================

In this scenario, Thingy:91 X running the |SM| application serves as the host.
You can use a PC as a client.

Connecting with a PC
--------------------

The nRF5340 SoC of Thingy:91 X is pre-programmed with the `Connectivity bridge`_ application.
To update the Connectivity bridge application, see the `Updating the Thingy:91 X firmware using nRF Util <Thingy91x_firmware_update_>`_  documentation.
The Connectivity bridge application routes ``UART_0`` to ``USB_CDC0`` on Thingy:91 X.
By enabling the ``CONFIG_BRIDGE_BLE_ENABLE`` Kconfig option in the Connectivity bridge application, you can also use |SM| over `Nordic UART Service (NUS) <Nordic UART Service_>`_.

To connect to a Thingy:91 X with a PC:

.. include:: sm_description.rst
   :start-after: .. sm_connecting_91dk_pc_instr_start
   :end-before: .. sm_connecting_91dk_pc_instr_end

.. _sm_testing_section:

Testing
=======

The following testing instructions focus on testing the application with a PC client.
If you have an nRF52 Series DK running a client application, you can also use this DK for testing the different scenarios.

|test_sample|

1. |connect_kit|
#. `Connect to the kit with the Serial Terminal app <Testing and optimization_>`_.
   You can also use the :guilabel:`Open Serial Terminal` option of the `Cellular Monitor app`_ to open the Serial Terminal app.
   If you want to use a different terminal emulator, see :ref:`sm_connecting_91dk_pc`.
#. Reset the kit.
#. Observe that the development kit sends a ``Ready\r\n`` message on UART.
#. Send AT commands and observe the responses from the development kit.

See :ref:`sm_testing` for typical test cases.

.. note::

   If the initialization of |SM| fails, the application sends an ``INIT ERROR\r\n`` message on UART if it has managed to enable UART.
   See the logs for more information about the error.
   The logs are written to RTT by default.

.. _sm_carrier_library_support:

Using the LwM2M carrier library
===============================

The application supports the |NCS| `LwM2M carrier`_ library that you can use to connect to the operator's device management platform.
See the library's documentation for more information and configuration options.

To enable the LwM2M carrier library, add the parameter ``-DOVERLAY_CONFIG=overlay-carrier.conf`` to your build command.

The CA root certificates that are needed for modem FOTA are not provisioned in the |SM| application.
You can flash the `Cellular: LwM2M carrier`_ sample to write the certificates to modem before flashing the |SM| application, or use the `Cellular: AT Client`_ sample as explained in `preparing the Cellular: LwM2M Client sample for production <lwm2m_client_provisioning_>`_.
It is also possible to modify the |SM| application project itself to include the certificate provisioning, as demonstrated in the `Cellular: LwM2M carrier`_ sample.

.. code-block:: c

   int lwm2m_carrier_event_handler(const lwm2m_carrier_event_t *event)
   {
           switch (event->type) {
           case LWM2M_CARRIER_EVENT_INIT:
                   carrier_cert_provision();
           ...


The certificate provisioning can also be done directly in the |SM| application by using the same AT commands as described for the `Cellular: AT Client`_ sample.

When the `LwM2M carrier`_ library is in use, by default the application will auto-connect to the network on startup.
This behavior can be changed by disabling the :ref:`CONFIG_SM_AUTO_CONNECT <CONFIG_SM_AUTO_CONNECT>` option.

Dependencies
************

This application uses the following |NCS| libraries:

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

It uses the following `sdk-nrfxlib`_ libraries:

* `Modem library`_

In addition, it uses the following secure firmware component:

* `Trusted Firmware-M`_
