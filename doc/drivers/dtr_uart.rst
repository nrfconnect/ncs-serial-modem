.. _drivers_dtr_uart:

DTR UART driver
###############

.. contents::
   :local:
   :depth: 2

The DTR UART driver implements the standard *asynchronous UART API* for the Data Communication Equipment (DCE) (also known as modem) with Data Terminal Ready (DTR) and Ring Indicator (RI) signals.

Another device, Data Terminal Equipment (DTE) (also known as host), controls the power state of the DTR UART by asserting and deasserting the DTR signal.
The :ref:`lib_sm_host` gives an example of DTE implementation.

When the DTE, is ready to communicate with DCE, it asserts the DTR signal.
This causes the DTR UART driver to resume the UART peripheral, RX, and TX operations.

When the DTE wants to save power, it deasserts the DTR signal.
This causes the DTR UART driver to stop the RX and TX operations and disable the UART peripheral.

When the DCE has data to be read by the DTE, it asserts the RI signal for a short period of time.
The DTE can then assert the DTR signal to resume the UART peripheral and read the data.

Application integration
***********************

When the DTR signal is deasserted:

* The DTR UART driver closes the UART internally.
  The application will not receive UART data, or the ``UART_RX_DISABLED`` or the ``UART_RX_STOPPED`` event.
* :c:func:`uart_rx_enable` will indicate that the application is ready to receive data, but the buffer is immediately released with the ``UART_RX_BUF_RELEASED`` event.
* :c:func:`uart_tx` will cause the RI signal to be asserted, indicating to the DTE that there is data to be read.
  Data will be queued and transmitted when the DTR signal is asserted again.

When the DTR signal is asserted:

* The DTR UART driver will send any pending TX data.
  The application will receive the ``UART_TX_DONE`` notification when the transmission is completed.
* The DTR UART driver queries a new RX buffer from the application with the ``UART_RX_BUF_REQUEST`` event.
  The application must provide a new RX buffer by calling the :c:func:`uart_rx_buf_rsp` function.
* The application will be able to send and receive UART data as normal.

Power management
----------------

With Device Power Management enabled with the ``CONFIG_PM_DEVICE`` Kconfig option, the DTR UART driver can be suspended and resumed from the application using the :c:func:`pm_device_action_run` function.

When the DTR UART driver is suspended, it disables the UART peripheral, stops the RX and TX operations, and stops monitoring the DTR signal.
When resumed, it starts monitoring the DTR signal again and resumes the UART peripheral, RX, and TX operations if the DTR signal is asserted.

Configuration
*************

The DTR UART driver is built on top of the standard UART driver extended with DTR and RI signals.
It is configured in the devicetree as a child node of the UART instance that extends standard UART configuration with DTR and RI pins.

See the configuration example from :file:`app/overlay-external-mcu.overlay`::

   &uart2 {
      compatible = "nordic,nrf-uarte";
      current-speed = <115200>;
      hw-flow-control;
      status = "okay";

      dtr_uart2: dtr-uart {
         compatible = "nordic,dtr-uart";
         dtr-gpios = <&gpio0 31 (GPIO_PULL_UP | GPIO_ACTIVE_LOW)>;
         ri-gpios = <&gpio0 30 GPIO_ACTIVE_HIGH>;
         status = "okay";
      };
      pinctrl-0 = <&uart2_default_alt>;
      pinctrl-1 = <&uart2_sleep_alt>;
      pinctrl-names = "default", "sleep";
   };

When the DTR UART driver is included in the devicetree, the Kconfig option ``CONFIG_DTR_UART`` is automatically enabled.
