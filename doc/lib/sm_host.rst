.. _lib_sm_host:

Serial Modem Host
#################

.. contents::
   :local:
   :depth: 2

The Serial Modem Host library exposes the AT command interface of the :ref:`Serial Modem <sm_description>` application for external devices over a serial interface.
This library is for applications running on external MCU that is built with |NCS| and is connected to an nRF91 Series SiP through UART.

Overview
********

The Serial Modem Host library allows you to perform the following functions:

* Manage the serial interface so that the application only decides which UART device to use and configures its DTS.
* Manage the GPIO pins, with support for bidirectional indication and power pin.
* Send modem or Serial Modem proprietary AT commands, receive responses and notifications, similar to the :ref:`lib_at_host` library.
  Received AT responses or notifications can be parsed by the :ref:`at_parser_readme` library.
* Send raw data in Serial Modem data mode.
  Refer to :ref:`sm_data_mode`.
* Monitor AT notifications with registered callbacks, similar to the :ref:`at_monitor_readme` library.
* Send AT commands through UART or RTT shell, similar to the :ref:`lib_at_shell` library.

Configuration
*************

The library is enabled and configured entirely using the Kconfig system.

Configure the following Kconfig options to enable this library:

* :kconfig:option:`CONFIG_SM_HOST` - Enables the Modem Serial Modem library.
* :kconfig:option:`CONFIG_SM_HOST_AT_CMD_RESP_MAX_SIZE` - Configures the size of the AT command response buffer.
  The default size is 2100 bytes, which is aligned with Serial Modem.
* :kconfig:option:`CONFIG_SM_HOST_POWER_PIN` - Configures the mandatory power pin GPIO, which is not configured by default.
* :kconfig:option:`CONFIG_SM_HOST_POWER_PIN_TIME` - Sets the toggle time value in milliseconds for power pin GPIO, by default 100 ms.

Optionally configure the following Kconfig options based on need:

* :kconfig:option:`CONFIG_SM_HOST_SHELL` - Enables the shell function in the Modem Serial Modem library, which is not enabled by default.
* :kconfig:option:`CONFIG_SM_HOST_INDICATE_PIN` - Configures the optional indicator GPIO, which is not configured by default.
* :kconfig:option:`CONFIG_SM_HOST_UART_RX_BUF_COUNT` - Configures the number of RX buffers for the UART device.
  The default value is 3.
* :kconfig:option:`CONFIG_SM_HOST_UART_RX_BUF_SIZE` - Configures the size of the RX buffer for the UART device.
  The default value is 256 bytes.
* :kconfig:option:`CONFIG_SM_HOST_UART_TX_BUF_SIZE` - Configures the size of the TX buffer for the UART device.
  The default value is 256 bytes.

The application must use Zephyr ``chosen`` nodes in devicetree to select UART device.
Additionally, GPIO can also be selected.
For example:

.. code-block:: devicetree

   / {
      chosen {
         ncs,sm-uart = &uart1;
         ncs,sm-gpio = &gpio0;
      };
   };

Use one of the following options to select the termination character:

* :kconfig:option:`CONFIG_SM_HOST_CR_TERMINATION` - Enables ``<CR>`` as the termination character.
* :kconfig:option:`CONFIG_SM_HOST_LF_TERMINATION` - Enables ``<LF>`` as the termination character.
* :kconfig:option:`CONFIG_SM_HOST_CR_LF_TERMINATION` - Enables ``<CR+LF>`` as the termination character, which is selected by default.

You must configure the same termination character as that configured in Serial Modem on the nRF91 Series SiP.
The library sends the termination character automatically after an AT command.

Shell usage
***********

Serial Modem
---

Send AT commands for Serial Modem in shell:

  .. code-block:: console

     uart:~$ sm AT%XPTW=4,\"0001\"

     OK

     uart:~$ sm at%ptw?

     %XPTW: 4,"0001"
     %XPTW: 5,"0011"

     OK

Serial Modem accepts AT command characters in upper, lower, or mixed case.

Host
----

Use ``smsh`` command to see commands for the Modem Serial Modem library functions.

Request toggling of the power pin from the Modem Serial Modem library to put the Serial Modem device to sleep and then wake it up:

  .. code-block:: console

     uart:~$ smsh powerpin
     [00:00:17.973,510] <inf> mdm_sm: Enable power pin
     [00:00:18.078,887] <inf> mdm_sm: Disable power pin

     uart:~$ smsh powerpin
     [00:00:33.038,604] <inf> mdm_sm: Enable power pin
     [00:00:33.143,951] <inf> mdm_sm: Disable power pin
     Ready

     [00:00:34.538,513] <inf> app: Data received (len=7): Ready
     uart:~$

Serial Modem Host Monitor usage
*******************************

The Serial Modem Host Monitor has similar functions to the :ref:`at_monitor_readme` library, except "Direct dispatching".

  .. code-block:: console

     SM_MONITOR(network, "\r\n+CEREG:", cereg_mon);

API documentation
*****************

| Header file: :file:`include/sm_host.h`
| Source file: :file:`lib/sm_host/sm_host.c`
| Source file: :file:`lib/sm_host/sm_host_monitor.c`

.. doxygengroup:: sm_host
