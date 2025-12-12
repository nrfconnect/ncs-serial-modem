.. _lib_sm_at_client:

|SM| AT Client library
######################

.. contents::
   :local:
   :depth: 2

The |SM| AT Client library exposes the AT command interface of the :ref:`serial_modem_app` for external devices over a serial interface.
This library is intended for applications running on an external MCU that are connected to an nRF91 Series SiP through UART.

Overview
********

The |SM| AT Client library allows you to perform the following functions:

* Send modem or |SM| proprietary AT commands, receive responses and notifications, similar to the `AT Host`_ library.
  Received AT responses or notifications can be parsed by the `AT parser`_ library.
* Send raw data in |SM| data mode.
  Refer to :ref:`sm_data_mode`.
* Manage the UART power state of the host.
* Manage the UART power state of the |SM| with DTR (Data Terminal Ready) and RI (Ring Indicator) pins.
  See :ref:`uart_configuration` for more information.
* Monitor AT notifications with registered callbacks, similar to the `AT monitor`_ library.
* Send AT commands through UART or RTT shell, similar to the `AT shell`_ library.

Configuration
*************

Configure the following Kconfig option to enable the library:

* ``CONFIG_SM_AT_CLIENT`` - Enables the |SM| AT Client library.

Optionally configure the following Kconfig options based on need:

* ``CONFIG_SM_AT_CLIENT_SHELL`` - Enables the shell function in the |SM| AT Client library, which is disabled by default.
* ``CONFIG_SM_AT_CLIENT_AT_CMD_RESP_MAX_SIZE`` - Configures the size of the AT command response buffer.
  The default size is 2100 bytes, which is aligned with |SM|.
* ``CONFIG_SM_AT_CLIENT_UART_RX_BUF_COUNT`` - Configures the number of RX buffers for the UART device.
  The default value is ``3``.
* ``CONFIG_SM_AT_CLIENT_UART_RX_BUF_SIZE`` - Configures the size of the RX buffer for the UART device.
  The default value is 256 bytes.
* ``CONFIG_SM_AT_CLIENT_UART_TX_BUF_SIZE`` - Configures the size of the TX buffer for the UART device.
  The default value is 256 bytes.

Use one of the following options to select the termination character:

* ``CONFIG_SM_AT_CLIENT_CR_TERMINATION`` - Enables ``<CR>`` as the termination character, which is selected by default.
* ``CONFIG_SM_AT_CLIENT_LF_TERMINATION`` - Enables ``<LF>`` as the termination character.
* ``CONFIG_SM_AT_CLIENT_CR_LF_TERMINATION`` - Enables ``<CR+LF>`` as the termination character.

You must configure the same termination character as that configured in |SM| on the nRF91 Series SiP.
The library sends the termination character automatically after an AT command.

The application must configure the devicetree nodes for the UART, UART pins, and DTR GPIOs.

Below is an example overlay for configuring UART and DTR/RI GPIOs:

.. code-block:: devicetree

  / {
    chosen {
      ncs,sm-uart = &uart2;
    };
  };

  /* Serial Modem AT Client <-> Serial Modem UART */
  &uart2 {
    compatible = "nordic,nrf-uarte";
    current-speed = <115200>;
    status = "okay";
    hw-flow-control;
    pinctrl-0 = <&uart2_default>;
    pinctrl-1 = <&uart2_sleep>;
    pinctrl-names = "default", "sleep";
  };

  /* UART pin configuration */
  &pinctrl {
    uart2_default: uart2_default {
      group1 {
        psels = <NRF_PSEL(UART_TX, 1, 4)>,
          <NRF_PSEL(UART_RTS, 1, 6)>;
      };
      group2 {
        psels = <NRF_PSEL(UART_RX, 1, 5)>,
          <NRF_PSEL(UART_CTS, 1, 7)>;
        bias-pull-up;
      };
    };

    uart2_sleep: uart2_sleep {
      group1 {
        psels = <NRF_PSEL(UART_TX, 1, 4)>,
          <NRF_PSEL(UART_RX, 1, 5)>,
          <NRF_PSEL(UART_RTS, 1, 6)>,
          <NRF_PSEL(UART_CTS, 1, 7)>;
        low-power-enable;
      };
    };
  };

  /* DTR gpios for uart2 */
  / {
    dte_dtr: dte_dtr {
      compatible = "nordic,dte-dtr";
      dtr-gpios = <&gpio0 26 GPIO_ACTIVE_LOW>;
      ri-gpios = <&gpio0 25 (GPIO_ACTIVE_LOW | GPIO_PULL_UP)>;
    };
  };

UART baud rate, hardware flow control, and parity must match the |SM| application configuration.
UART pins must be wired correctly (TX to RX, RTS to CTS) to the |SM| application UART pins.
DTR and RI GPIO must be wired to the corresponding DTR and RI pins of the |SM| application.

Shell usage
***********

|SM| AT commands
----------------

Send AT commands for |SM| in shell:

  .. code-block:: console

     uart:~$ sm AT%XPTW=4,\"0001\"

     OK

     uart:~$ sm at%ptw?

     %XPTW: 4,"0001"
     %XPTW: 5,"0011"

     OK

|SM| accepts AT command characters in upper, lower, or mixed case.

Host commands
-------------

Use ``smsh`` command to see commands for the |SM| AT Client library functions.

  .. code-block:: console

    uart:~$ smsh
    smsh - Commands handled in Serial Modem AT Client shell device
    Subcommands:
      uart  : Enable/Disable DTR UART.

    uart:~$ smsh uart
    uart - Enable/Disable DTR UART.
    Subcommands:
      auto     : [<inactivity_period>]
                (Default) Automatically enable DTR UART from RI. Disable DTR UART
                after inactivity period (default value is 100ms).
      enable   : Enable DTR UART. Disable automatic handling.
      disable  : Disable DTR UART. Disable automatic handling.

Enable or disable host UART and command |SM| to do the same with DTR:

  .. code-block:: console

    uart:~$ smsh uart enable
    Enable DTR UART.

    uart:~$ smsh uart disable
    Disable DTR UART.

Set the automatic UART handling to 1000 ms inactivity period:

  .. code-block:: console

    uart:~$ smsh uart auto 1000
    Automatic DTR UART. Inactivity timeout 1000 ms

When automatic UART and DTR handling is enabled, the UART's will be suspended after the inactivity period.
UARTs are resumed when there is an RI signal from the |SM| or when the host sends data.

|SM| AT Client Monitor usage
****************************

The |SM| AT Client Monitor has similar functions to the `AT monitor`_ library, except "Direct dispatching".

  .. code-block:: console

     SM_MONITOR(network, "\r\n+CEREG:", cereg_mon);

API documentation
*****************

| Header file: :file:`include/sm_at_client.h`
| Source file: :file:`lib/sm_at_client/sm_at_client.c`
| Source file: :file:`lib/sm_at_client/sm_at_client_monitor.c`

.. doxygengroup:: sm_at_client
   :members:
