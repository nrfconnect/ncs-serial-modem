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

.. _CONFIG_SM_CUSTOMER_VERSION:

CONFIG_SM_CUSTOMER_VERSION - Customer version string
   Version string defined by the customer after customizing the application.
   When defined, this version is reported with the baseline versions by the ``AT#XSMVER`` AT command.

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

   When enabled, the following sub-options are available for configuration:

   .. _CONFIG_SM_AUTO_CONNECT_SYSTEM_MODE:

   CONFIG_SM_AUTO_CONNECT_SYSTEM_MODE - System mode for automatic network attach
      This option defines the system mode to use for automatic network attach.
      The format is a comma-separated string of four single-digit integers: ``"X,Y,Z,W"``

      Where:

      * ``X`` = LTE-M support (0=disabled, 1=enabled)
      * ``Y`` = NB-IoT support (0=disabled, 1=enabled)
      * ``Z`` = GNSS support (0=disabled, 1=enabled)
      * ``W`` = LTE preference (0=no preference, 1=LTE-M, 2=NB-IoT, 3=network selection, 4=LTE-M then NB-IoT)

      See the `%XSYSTEMMODE`_ command in the AT command Reference Guide for more details.
      The default value is ``"1,1,1,0"`` (enable LTE-M, NB-IoT, and GNSS with no preference).

   .. _CONFIG_SM_AUTO_CONNECT_PDN_CONFIG:

   CONFIG_SM_AUTO_CONNECT_PDN_CONFIG - Enable PDN configuration
      This option enables sending of PDN configuration commands during automatic network attach.
      When enabled, the following sub-options become available:

      .. _CONFIG_SM_AUTO_CONNECT_PDN_APN:

      CONFIG_SM_AUTO_CONNECT_PDN_APN - Access Point Name (APN)
         This option specifies the APN to use for automatic network attach.
         If not set (default), the APN configured in the modem will be used.

      .. _CONFIG_SM_AUTO_CONNECT_PDN_FAMILY:

      CONFIG_SM_AUTO_CONNECT_PDN_FAMILY - PDN family (IP type)
         This choice option selects the PDN family (IP address type) to use for automatic network attach.
         See the `+CGDCONT`_ command in the AT command Reference Guide for more details.

         Available options:

         * ``CONFIG_SM_AUTO_CONNECT_PDN_FAMILY_IP`` - Use IPv4 only
         * ``CONFIG_SM_AUTO_CONNECT_PDN_FAMILY_IPV6`` - Use IPv6 only
         * ``CONFIG_SM_AUTO_CONNECT_PDN_FAMILY_IPV4V6`` - Use both IPv4 and IPv6 (dual stack, default)
         * ``CONFIG_SM_AUTO_CONNECT_PDN_FAMILY_NON_IP`` - Use Non-IP PDN type for data transmission without IP headers

      .. _CONFIG_SM_AUTO_CONNECT_PDN_AUTH:

      CONFIG_SM_AUTO_CONNECT_PDN_AUTH - PDN authentication type
         This option specifies the PDN authentication protocol to use for automatic network attach.
         See the `+CGAUTH`_ command in the AT command Reference Guide for more details.

         Valid values:

         * ``0`` - None (no authentication, default)
         * ``1`` - PAP (Password Authentication Protocol)
         * ``2`` - CHAP (Challenge Handshake Authentication Protocol)

      .. _CONFIG_SM_AUTO_CONNECT_PDN_USERNAME:

      CONFIG_SM_AUTO_CONNECT_PDN_USERNAME - PDN authentication username
         This option specifies the username to use for PDN authentication during automatic network attach.
         Do not set the Kconfig option (default) if no username is required.
         Use only when ``CONFIG_SM_AUTO_CONNECT_PDN_AUTH`` is set to 1 (PAP) or 2 (CHAP).

      .. _CONFIG_SM_AUTO_CONNECT_PDN_PASSWORD:

      CONFIG_SM_AUTO_CONNECT_PDN_PASSWORD - PDN authentication password
         This option specifies the password to use for PDN authentication during automatic network attach.
         Leave empty (default) if no password is required.
         Use only when ``CONFIG_SM_AUTO_CONNECT_PDN_AUTH`` is set to 1 (PAP) or 2 (CHAP).

   Example configuration overlay for NB-IoT with Non-IP PDN::

      CONFIG_SM_AUTO_CONNECT=y
      CONFIG_SM_AUTO_CONNECT_SYSTEM_MODE="0,1,0,0"
      CONFIG_SM_AUTO_CONNECT_PDN_CONFIG=y
      CONFIG_SM_AUTO_CONNECT_PDN_FAMILY_NON_IP=y

   Example configuration overlay for LTE-M with custom APN and PAP authentication::

      CONFIG_SM_AUTO_CONNECT=y
      CONFIG_SM_AUTO_CONNECT_SYSTEM_MODE="1,0,0,0"
      CONFIG_SM_AUTO_CONNECT_PDN_CONFIG=y
      CONFIG_SM_AUTO_CONNECT_PDN_APN="internet"
      CONFIG_SM_AUTO_CONNECT_PDN_FAMILY_IPV4V6=y
      CONFIG_SM_AUTO_CONNECT_PDN_AUTH=1
      CONFIG_SM_AUTO_CONNECT_PDN_USERNAME="myuser"
      CONFIG_SM_AUTO_CONNECT_PDN_PASSWORD="mypass"

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

.. _CONFIG_SM_URC_BUFFER_SIZE:

CONFIG_SM_URC_BUFFER_SIZE - URC buffer size.
   Buffer, in which unsolicited result codes (URC) are stored before being sent to the host.
   The buffer has to be large enough to hold the largest URC that can be sent by the modem.
   Result codes longer than this size will get dropped.
   The default value is 4096.

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

.. _CONFIG_SM_DFU_MODEM_FULL:

CONFIG_SM_DFU_MODEM_FULL - Enable full modem DFU
   This option enables support for full modem firmware updates using the ``AT#XDFUINIT``, ``AT#XDFUWRITE``, and ``AT#XDFUAPPLY`` commands.
   See the :ref:`DFU_AT_commands` for more information.

.. _sm_config_files:

Configuration files
*******************

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

* :file:`overlay-trace-backend-cmux.conf` - Configuration file that enables CMUX modem trace backend.
  When enabled, modem traces are transmitted on a dedicated CMUX channel.
  See the :ref:`sm_modem_trace_cmux` documentation for more information.

* :file:`overlay-memfault.conf` - Configuration file that enables `Memfault`_.
  For more information about Memfault features in |NCS|, see the `Memfault library`_ docs.

* :file:`overlay-disable-dtr.overlay` - Devicetree overlay that disables the DTR and RI pins and related functionality.
  This overlay can be used if your setup does not have the need or means for managing the power externally.
  Modify the overlay to fit your configuration.

The board-specific devicetree overlays (:file:`boards/*.overlay`) set up configurations that are specific to each supported development kit.
All of them configure the DTR to be deasserted from a button and RI to blink an LED.
