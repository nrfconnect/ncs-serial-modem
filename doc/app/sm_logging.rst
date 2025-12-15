.. _sm_logging:

Logging
#######

.. contents::
   :local:
   :depth: 3

The application MCU and the modem produce separate logs with their own methods.
Logs often refer to the |SM| application logging while the modem logs are referred to as modem traces in many places.

Application logging
*******************

|SM| uses the SEGGER Real-Time Transfer (RTT) for application logging by default.
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

Modem traces
************

To send modem traces over UART on an nRF91 Series DK, configuration must be added for the UART device in the devicetree and Kconfig.
This is done by adding the `modem trace UART snippet`_ when building and programming.

It is also possible to send modem traces over CMUX channel.
See the :ref:`sm_modem_trace_cmux` documentation for more information.

Use the `Cellular Monitor app`_ for capturing and analyzing modem traces.

TF-M logging must use the same UART as the application. For more details, see `shared TF-M logging`_.
