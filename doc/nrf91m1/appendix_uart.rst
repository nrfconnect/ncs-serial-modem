.. raw:: latex

   \nordicStartAppendices

.. _nrf91m1_uart_configuration:

UART configuration
##################

.. contents::
   :local:
   :depth: 2

The |SM| is a pre-programmed nRF9151 SiP that runs the Serial Modem application out of the box.

UART interface
**************

The |SM| is controlled by a host processor through a UART interface.
The host sends AT commands and receives responses and notifications over this UART.
When CMUX is enabled, the same UART also carries PPP data frames for network communication.

.. figure:: /images/cellular-serial-setup-nrf91m1.svg
   :alt: UART interface setup

   UART interface setup

The following UART signals are used:

.. list-table::
   :header-rows: 1
   :widths: 15 20 65

   * - Signal
     - Direction
     - Purpose
   * - TX/RX
     - Bidirectional (crossed)
     - Data transmission.
   * - RTS/CTS
     - Bidirectional (crossed)
     - Hardware flow control.
   * - DTR
     - Host to nRF91M1
     - Data Terminal Ready.
       Host signals UART is powered on.
   * - RI
     - nRF91M1 to Host
     - Ring Indicator.
       The nRF91M1 wakes host for incoming data.

The Data Terminal Ready (DTR) and Ring Indicator (RI) signals are the control signals for UART power management, while TX/RX/RTS/CTS are the standard UART signals.

.. important::
   The |SM| has hardware flow control (RTS and CTS) permanently enabled.
   Using the |SM| without connecting RTS and CTS to the host is `experimental <Software maturity levels_>`_ and strongly not recommended.
   See :ref:`uart_without_flow_control` for more information.

.. _nrf91m1_uart_pin_assignment:

|SM| pin assignment
*******************

The following table shows how to connect the UART pins to the corresponding pins on the |SM| (`nRF9151 LGA pin assignments`_):

.. list-table::
   :header-rows: 1

   * - Signal
     - |SiP_pin|
   * - UART0 TX
     - 45
   * - UART0 RX
     - 44 (pull-up)
   * - UART0 RTS
     - 74
   * - UART0 CTS
     - 75 (pull-up)
   * - UART0 DTR
     - 50 (active low, pull-up)
   * - UART0 RI
     - 49 (active low)
   * - UART1 TX
     - 48
   * - UART1 RX
     - 47 (pull-up, not connected to the host)

**AT UART:**

* UART instance: UART0
* Baud rate: 115200
* Hardware flow control: Enabled

**Log / Trace UART:**

* UART instance: UART1
* Baud rate: 1000000
* Hardware flow control: Disabled

.. important::
   Operating the |SM| without hardware flow control is strongly not recommended.
   See :ref:`uart_without_flow_control` for more information.

.. include:: ../uart_configuration.rst
   :start-after: uart_power_pins_start
   :end-before: uart_power_pins_end

.. include:: ../uart_configuration.rst
   :start-after: uart_power_management_start
   :end-before: uart_power_management_end

Zephyr hosts
============

If the host runs Zephyr, you can automate the UART power management with Zephyr's cellular modem driver.

.. only:: html

   See :external+main:ref:`uart_zephyr_cellular_modem_driver` for more information.

.. only:: latex

   See `Serial Modem UART Zephyr cellular modem driver auto power`_ for more information.

.. _uart_without_flow_control:

Operating without hardware flow control
***************************************

.. important::
   The use of the |SM| without hardware flow control (RTS/CTS) is `experimental <Software maturity levels_>`_ and strongly not recommended.
   Hardware flow control ensures reliable communication and prevents timing-related issues.
   Without hardware flow control, there is a risk of data loss in UART communication, especially in high-throughput scenarios.

The |SM| has hardware flow control permanently enabled and it cannot be disabled.
To operate without hardware flow control, do the following:

* **Wire the CTS pin to GND** — Wiring CTS to GND holds the line low, allowing the |SM| to transmit.
  Without this, the |SM| sees CTS as high (not clear to send) and does not transmit.

  * **Increased power consumption** — The |SM| has an internal pull-up on the CTS pin that cannot be removed.
    With CTS wired to GND, the pull-up draws current and increases the power consumption while the UART is active.

* **Leave RTS unconnected** — The RTS pin is not used and can be left floating.

**Key requirements:**

* The host must enable its own UART before asserting DTR.
* To detect when the |SM| is ready to receive, monitor the RI signal (de-asserted when ready) or use a sufficient timeout after DTR assertion.
* See :ref:`Incoming data wake-up <uart_incoming_data_wake_up>` for detailed timing information.
* In boot-up scenarios, the host must wait for the ``"Ready"`` message from the |SM| before sending any data.

**Buffer size:**

The UART receive buffer of the |SM| is fixed to 532 bytes and cannot be configured.

.. _nrf91m1_uart_dk:

Operating with the nRF9151 DK
*****************************

You can flash the nRF91M1 firmware to the `nrf9151dk`_ board for development and testing purposes.
On the DK, the SiP is an nRF9151, and the UART pins follow the pin mapping of the nRF91M1 as listed in the following table:

.. list-table::
   :header-rows: 1

   * - Signal
     - nrf9151dk pin
   * - UART0 TX
     - P0.27
   * - UART0 RX
     - P0.26 (pull-up)
   * - UART0 RTS
     - P0.14
   * - UART0 CTS
     - P0.15 (pull-up)
   * - UART0 DTR
     - P0.31 (active low, pull-up)
   * - UART0 RI
     - P0.30 (active low)
   * - UART1 TX
     - P0.29

By default on the `nrf9151dk`_ board, UART0 is routed to VCOM0 on the interface chip, and UART1 is routed to VCOM1 on the interface chip.
This allows you to use the `nrf9151dk`_ board with a PC host for development and testing.

PC host
=======

.. important::
   When you use the `nrf9151dk`_ board with a PC host, you must wire the **DTR** pin to **GND** to power on UART0.
   Currently, you must use a physical jumper wire, but in the future, the `Board Configurator app`_ can be used.

When you use a PC serial terminal application (for example, PuTTY, Tera Term, minicom) to send AT commands, configure the terminal as follows:

* UART instance: VCOM0
* Baud rate: 115200
* Hardware flow control: RTS/CTS enabled
* Line terminator: CRLF
* AT command terminator: CR

Ensure that the return key sends the **CR** (Carriage Return) character as the line terminator.
Incoming serial output uses both the **CR** and the **LF** (Line Feed) characters.
These settings are the default configuration for the PuTTY, Tera Term, minicom, and picocom serial terminal applications.

UART1 (VCOM1) carries the log and trace output at 1000000 baud, without hardware flow control.

See :ref:`SM_AT_syntax` for the AT command syntax.

External MCU host
=================

When you use the `nrf9151dk`_ board with an external MCU host, you must disable VCOM0 in the `Board Configurator app`_ to release the UART pins for external use.

.. _nrf91m1_uart_cellular_ppp_modem:

Cellular PPP modem
==================

For information on how the |SM| works as a cellular PPP modem, see :ref:`nrf91m1_cellular_modem`.
For information on how to configure a Linux host with the `nrf9151dk`_ board running nRF91M1 firmware, see `Serial Modem PPP Linux host`_.
For a Zephyr host, see `Serial Modem PPP Zephyr host`_.
