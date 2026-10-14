.. raw:: latex

   \nordicStartAppendices

.. _nrf91m1_uart_configuration:

UART configuration
##################

The nRF91M1 is a pre-programmed nRF9151 SiP that runs the |SM| application out of the box.

UART interface
**************

The nRF91M1 is controlled by a host processor through a UART interface.
The host sends AT commands and receives responses and notifications over this UART.
When CMUX is enabled, the same UART also carries PPP data frames for network communication.

.. figure:: /images/cellular-serial-setup-nrf91m1.svg
   :alt: UART interface setup

   UART interface setup

The following UART signals are used:

.. list-table:: UART Signals
   :header-rows: 1
   :widths: 15 20 65

   * - Signal
     - Direction
     - Purpose
   * - TX/RX
     - Bidirectional (crossed)
     - Data transmission
   * - RTS/CTS
     - Bidirectional (crossed)
     - Hardware flow control
   * - DTR
     - Host → Modem
     - *Data Terminal Ready* - Host signals UART is powered on
   * - RI
     - Modem → Host
     - *Ring Indicator* - Modem wakes host for incoming data

The Data Terminal Ready (DTR) and Ring Indicator (RI) signals are the **control signals** used for UART power management, while TX/RX/RTS/CTS are the standard UART signals.

.. important::
   The nRF91M1 has hardware flow control (RTS/CTS) permanently enabled.
   Using the nRF91M1 without connecting RTS/CTS to the host is `experimental <Software maturity levels_>`_ and strongly not recommended.
   See :ref:`uart_without_flow_control` for more information.

nRF91M1 pin assignment
**********************

The following table shows how to connect the UART pins to the corresponding pins on the nRF91M1 SiP (`nRF9151 LGA pin assignments`_):

.. list-table::
   :header-rows: 1

   * - Signal
     - nRF91M1 pin
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

**AT UART:**

* UART instance: UART0
* Baud rate: 115200
* Hardware flow control: Enabled

**Log / Trace UART:**

* UART instance: UART1
* Baud rate: 1000000
* Hardware flow control: Disabled

.. important::
   Operating the nRF91M1 without hardware flow control is strongly not recommended.
   See :ref:`uart_without_flow_control` for more information.

Controlling the UART power state
********************************

The UART power state of the nRF91M1 is controlled by the **DTR** signal.
When DTR is asserted by the host, the UART is enabled and ready for communication.
When DTR is deasserted, the UART is powered down to save power.

When the UART is powered down, it releases its high-speed clocks, which are then automatically stopped by the system's power management.
This achieves the lowest possible power consumption, as the UART is one of the primary power consumers in the nRF91M1.
Power consumption can be reduced from hundreds of microamps to just a few microamps when the UART is powered down.

UART power management is completely independent of the modem power management (CFUN, PSM, eDRX, and so on).
By actively managing the UART power state through DTR control, the nRF91M1 achieves ultra-low power consumption of approximately 2 µA when the cellular modem enters Power Saving Mode (PSM) and the UART is powered down.
This is a reduction of over 99% compared to the 400-450 µA consumed when the UART remains active during idle periods.
The nRF91M1 retains all the RAM content while in any of the power saving modes, so the application can resume from where it left off when the UART is powered up.
Full network context is maintained, including socket connections, TLS/DTLS sessions, and so on.
LTE connection is automatically resumed when there is outgoing data to be sent.
No other GPIO control is required to utilize the PSM or eDRX modes.

.. _uart_incoming_data_wake_up:

Incoming data wake-up
=====================

When the UART is powered down and there is incoming data, the nRF91M1 asserts the **RI** pin to notify the host.
The host must handle the RI signal by powering up its UART device and asserting the DTR line, signaling to the nRF91M1 that the UART is powered on and ready to receive data.
The RI signal remains asserted until the nRF91M1 UART is activated and ready to transmit data.

.. figure:: /images/cmux-wakeup-timing.svg
   :alt: Modem waking up the host timing diagram

   Sequence diagram showing the wake-up sequence when the UART is powered down

This sequence diagram illustrates the wake-up sequence when the UART is powered down.
The RI signal triggers the host to power up its UART and assert DTR.
Once the host UART is ready and DTR is asserted, the nRF91M1 enables its UART interface.
The RI signal remains active throughout this process and only returns to inactive once the nRF91M1 UART is active.
This coordinated wake-up ensures both ends of the communication link are ready before data transmission begins.

**With hardware flow control -**  The host can observe its CTS pin (connected to the RTS pin of the nRF91M1) to know when the nRF91M1 is ready to receive data.
When the nRF91M1 starts driving its RTS pin, the host knows that the nRF91M1 is ready to receive.
The host must also have a pull-up on its CTS pin.

**Without hardware flow control-** The host must monitor the RI signal, which is de-asserted when the nRF91M1 UART becomes ready to receive data.
Alternatively, the host can implement a sufficient timeout between asserting DTR and initiating its first TX operation.
See :ref:`uart_without_flow_control` for how to operate the nRF91M1 without hardware flow control.

Host-initiated wake-up
======================

When the host processor needs to send data, it first powers up the UART device, then asserts the DTR signal to indicate that the UART is active.
The modem detects the DTR assertion and powers up its UART interface, allowing communication to resume.

.. figure:: /images/cmux-host-wakeup-modem.svg
   :alt: Host waking up the modem

   Sequence diagram showing host-initiated wake-up of the modem

Boot-up scenario
================

To boot up the nRF91M1, the host asserts **ENABLE**, **DTR**, and enables its UART (driving the **CTS** pin of the nRF91M1 low).
The nRF91M1 initializes within approximately 2000 ms, during which it activates its UART and asserts **RTS** to signal readiness.
The nRF91M1 then transmits a ``"Ready"`` message on **TX**, confirming that the link is operational.

.. figure:: /images/boot-up-sequence.svg
   :alt: Boot-up sequence diagram

   Signal sequence during boot-up

The **ENABLE** pin (pin **10** on the nRF91M1) is a high-impedance control input for the internal power management unit (PMU) of the nRF91M1.
Driving it to a logic high powers on the nRF91M1.
Driving it to a logic low brings the device into an extremely low-current state.
Any internal state not retained in non‑volatile memory is lost, and a full boot-up sequence is required when **ENABLE** is reasserted.
See `nRF9151 Hardware Design Guidelines — ENABLE`_ for more information about the ENABLE pin.

Zephyr hosts
============

If the host runs Zephyr, the UART power management can be automated with Zephyr's cellular modem driver.
See :external+main:ref:`uart_zephyr_cellular_modem_driver` for more information.

.. _uart_without_flow_control:

Operating without hardware flow control
***************************************

.. important::
   The use of the nRF91M1 without hardware flow control (RTS/CTS) is `experimental <Software maturity levels_>`_ and strongly not recommended.
   Hardware flow control ensures reliable communication and prevents timing-related issues.
   Without hardware flow control, there is a risk of data loss in UART communication, especially in high-throughput scenarios.

The nRF91M1 has hardware flow control permanently enabled and it cannot be disabled.
To operate without hardware flow control, do the following:

* **Wire the CTS pin to GND** — Wiring CTS to GND holds the line low, allowing the nRF91M1 to transmit.
  Without this, the nRF91M1 sees CTS as high (not clear to send) and does not transmit.

  * **Increased power consumption** — The nRF91M1 has an internal pull-up on the CTS pin that cannot be removed.
    With CTS wired to GND, the pull-up draws current and increases the power consumption while the UART is active.

* **Leave RTS unconnected** — The RTS pin is not used and can be left floating.

**Key requirements:**

* The host must enable its own UART before asserting DTR.
* To detect when the nRF91M1 is ready to receive, monitor the RI signal (de-asserted when ready) or use a sufficient timeout after DTR assertion.
* See :ref:`Incoming data wake-up <uart_incoming_data_wake_up>` for detailed timing information.
* In boot-up scenarios, the host must wait for the ``"Ready"`` message from the nRF91M1 before sending any data.

**Buffer size:**

The UART receive buffer of the nRF91M1 is fixed to 532 bytes and cannot be configured.

.. _nrf91m1_uart_dk:

Operating with the nRF9151 DK
*****************************

You can flash the nRF91M1 software to the `nrf9151dk`_ board for development and testing purposes.
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
This allows the `nrf9151dk`_ board to be used with a PC host for development and testing.

PC host
=======

.. important::
   When working with the `nrf9151dk`_ board with a PC host, the **DTR** pin must be wired to **GND** to power on UART0.
   Currently, you must use a physical jumper wire, but in the future, the `Board Configurator app`_ can be used.

When you use a PC serial terminal application (for example, PuTTY, Tera Term, minicom) to send AT commands, configure the terminal as follows:

* UART instance: VCOM0
* Baud rate: 115200
* Hardware flow control: CTS/RTS enabled
* Line terminator: CRLF
* AT command terminator: CR

Ensure that the return key sends the **CR** (Carriage Return) character as the line terminator.
For incoming serial output, both **CR** and **LF** (Line Feed) characters are used.
This is the default configuration for the PuTTY, Tera Term, minicom, and picocom serial terminal applications.

UART1 (VCOM1) carries the log and trace output at 1000000 baud without hardware flow control.

See :ref:`SM_AT_syntax` for the AT command syntax.

External MCU host
=================

When working with the `nrf9151dk`_ board with an external MCU host, you must disable VCOM0 in the `Board Configurator app`_ to release the UART pins for external use.
