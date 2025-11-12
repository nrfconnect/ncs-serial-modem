.. _sm_dtr_ri:

DTR and RI signals
##################

.. contents::
   :local:
   :depth: 2

When the modem is in PSM or eDRX mode, the UART between the |SM| and the host is the most power-consuming part.
|SM| application supports the DTR (Data Terminal Ready) and RI (Ring Indicator) signals, which allow the host to control the power state of the UART.

DTR is an output signal from the host to the modem, which indicates that the host is ready to communicate.
When the host asserts DTR, the |SM| application enables UART communication.
When the host deasserts DTR, the |SM| application disables UART communication to save power.

RI is an output signal from the |SM| to the host, which can be used to indicate incoming data.
When there is incoming data to be read from the modem, the |SM| application asserts RI for a short period of time.
The host can then assert DTR to enable UART communication and read the data.

See the :ref:`drivers_dtr_uart` DTR UART driver for more details on how the UART, DTR and RI signals are managed in the |SM| application.

DTR and RI with PC
******************

When you are using a PC as a host, the board-specific overlay files in the :file:`app/boards/` folder configure the DTR signal to a button and the RI signal to an LED.
The DTR signal is asserted when the button is not pressed, and deasserted when the button is pressed.

Given that the DTR signal is level sensitive, pushing the button for an extended period is impractical.
Therefore, you can use the ``AT#XSLEEP=2`` command to disable the UART and override the current asserted state of the DTR signal.
When the DTR signal is deasserted and asserted (the button is pushed and released), the UART is enabled, and the |SM| application starts responding to the DTR signal again.

.. note::

   The ``AT#XSLEEP`` command is intended for experimentation and power consumption measurements and must not be used in production.

DTR and RI with External MCU
****************************

When you are using an external MCU as a host, you need to include the :file:`app/overlay-external-mcu.overlay` file in your build.
This overlay file configures the UART, DTR, and RI pins for the nRF9151 DK.
Depending on your board, you might need to modify the overlay file to match your specific hardware configuration.
See :ref:`sm_connecting_91dk_mcu` for more information.

The host MCU must configure the DTR pin as an output and assert it when it is ready to communicate with the modem.
The active level must be configured to be the same in both the host and the modem (default: low).

When the host wants to save power, it pulls DTR to the inactive level, the |SM| application then stops the UART TX and RX operations and deactivates the UART peripheral.
It pulls the input RX and CTS pins to an inactive level and lets the TX and RTS pins float.
To save power, the host MCU should also deactivate the UART peripheral, pull the input RX and CTS pins to an inactive level and let TX and RTS pins float.

When there is incoming data to be read from the modem, the |SM| application asserts RI for 100 ms.
The host can then resume its UART peripheral, assert DTR to resume the |SM| UART peripheral, and read the data.

The following image illustrates the DTR and RI signal behavior in relation to the UART activity:

.. figure:: /images/dtr_ri.svg
   :align: center
   :alt: DTR/RI signal diagram
   :width: 600px

   DTR/RI signal diagram

The :ref:`sm_at_client_shell_sample` sample and :ref:`lib_sm_at_client` library show an example of how to use DTR and RI signals in an external MCU application.
