.. _sm_ppp_shell_sample:

PPP shell sample
################

.. contents::
   :local:
   :depth: 2

The |SM| PPP shell sample demonstrates how to use the nRF91 Series SiP as a Zephyr-compatible cellular modem through a Point-to-Point Protocol (PPP) interface.
This sample enables an external MCU or Linux host to use Zephyr's native networking stack and shell commands to control the cellular modem, test network connectivity, and measure network performance.

Requirements
************

The |SM| application must be built with the following overlays:

* :file:`app/overlay-cmux.conf` - To enable CMUX support
* :file:`app/overlay-ppp.conf` - To enable PPP support

If using an external MCU as the controlling device, also include:

* :file:`app/overlay-external-mcu.overlay` - To configure UART pins, DTR, and RI signals

The sample supports the following development kits:

.. list-table::
   :widths: auto
   :header-rows: 1

   * - Hardware platforms
     - PCA
     - Board name
     - Board target
   * - `nRF54L15 DK <nRF54L15 DK_>`_
     - PCA10156
     - `nrf54l15dk`_
     - | ``nrf54l15dk/nrf54l15/cpuapp``
       | ``nrf54l15dk/nrf54l15/cpuapp/ns`` (`TF-M`_)
   * - Native simulator
     - N/A
     - ``native_sim``
     - ``native_sim``

Hardware setup for nRF54L15 DK
==============================

When using the nRF54L15 DK as the controlling device, connect the DK pins to the corresponding pins on the nRF91 Series DK as follows:

.. list-table::
   :header-rows: 1

   * - nRF54L15 DK
     - nRF91 Series DK
   * - UART TX P0.00
     - UART RX P0.03
   * - UART RX P0.01
     - UART TX P0.02
   * - UART CTS P0.03
     - UART RTS P0.06
   * - UART RTS P0.02
     - UART CTS P0.07
   * - DTR OUT P1.11
     - DTR IN P0.31
   * - RI IN P1.12
     - RI OUT P0.30
   * - GPIO GND
     - GPIO GND

.. note::
   You must disable the VCOM0 on the nRF54L15 DK to release the UART GPIO pins for communication with the |SM|.

   * Use the `Board Configurator app`_ to disable the ``Connect port VCOM0`` setting.

.. note::
   The GPIO output levels on the nRF91 Series device and nRF54L15 DK must be the same.

   * Set the VDD voltages for both devices using the `Board Configurator app`_.

Native simulator setup
======================

When running on the native simulator, the sample expects the |SM| to be available at ``/dev/ttyACM0`` on the Linux host system.
Connect the nRF91 Series DK running |SM| to your Linux host through USB.

Overview
********

This sample showcases the integration between Zephyr's cellular modem driver and the |SM| application running on an nRF91 Series SiP.
Instead of using AT commands directly, the controlling device can use Zephyr's standard networking APIs and shell commands to interact with the modem.

The sample provides:

* Full Zephyr networking stack (IPv4 and IPv6 support).
* Network shell commands for interface management.
* Network performance testing using zperf.
* DNS resolution support.
* Automatic modem connection management.
* CMUX (GSM 07.10) multiplexing support.
* Power management with runtime power saving.

For more information about using the nRF91 Series SiP as a Zephyr-compatible modem, see :ref:`sm_as_zephyr_modem`.

Configuration
*************

The sample is configured with the following key features:

* PPP Layer 2 networking
* IPv4 and IPv6 support
* TCP and UDP protocols
* DNS resolver with automatic DNS server configuration from PPP
* Network connection manager for automatic reconnection
* Zephyr cellular modem driver with CMUX support
* Runtime power management with automatic power saving
* Network shell commands
* Zperf for network performance testing

The CMUX configuration enables:

* Power save mode that closes CMUX pipes when idle
* 5 second idle timeout before entering power save
* MTU size of 127 bytes for optimal performance

Building and running
********************

This sample can be found under :file:`samples/sm_ppp_shell`.

For more security, it is recommended to use the ``*/ns`` `variant <app_boards_names_>`_ of the board target (see the Requirements section above.)
When built for this variant, the sample is configured to compile and run as a non-secure application using `security by separation <ug_tfm_security_by_separation_>`_.
Therefore, it automatically includes `Trusted Firmware-M <ug_tfm_>`_ that prepares the required peripherals and secure services to be available for the application.

nRF54L15 DK
===========

To build the sample for the nRF54L15 DK:

.. code-block:: bash

   west build -b nrf54l15dk/nrf54l15/cpuapp

For the TrustZone-enabled variant:

.. code-block:: bash

   west build -b nrf54l15dk/nrf54l15/cpuapp/ns

Flash the sample to your board and open a serial terminal to view the shell interface.

For nRF54L15 DK, the shell is available on UART1 (VCOM1).

Native simulator
================

To build the sample for the native simulator:

.. code-block:: bash

   west build -b native_sim

For native simulator, run the executable with the ``-attach_uart`` option:

.. code-block:: bash

   build/zephyr/zephyr.exe -attach_uart

Testing
*******

After the sample starts, you will see the Zephyr shell prompt.
The PPP network interface is configured to start the modem automatically when requested.

Bringing up the network interface
=================================

To start the cellular modem and establish a network connection:

.. code-block:: console

   uart:~$ net iface up 1

The modem begins the network registration process.
You can monitor the connection status with:

.. code-block:: console

   uart:~$ net iface

The network management event monitor (enabled by default) will also display connection events automatically.

Once connected, the interface will show as "UP" and you will see an assigned IP address.

Testing network connectivity
============================

After the network interface is up, you can test basic connectivity.

To ping a (DNS) server:

.. code-block:: console

   uart:~$ net ping 8.8.8.8

To perform DNS resolution:

.. code-block:: console

   uart:~$ net dns google.com

Performance testing with zperf
==============================

The sample includes the zperf tool for network performance measurements:

To run a UDP upload test:

.. code-block:: console

   uart:~$ zperf udp upload <server-ip> <port> <duration> <packet-size> <rate>

See the `zperf: Network Traffic Generator`_ for more details on using zperf.

Shutting down the network interface
===================================

To shut down the cellular connection and power off the modem:

.. code-block:: console

   uart:~$ net iface down 1

Dependencies
************

The sample enables the following key features:

* Networking stack (``CONFIG_NETWORKING``)
* PPP L2 layer (``CONFIG_NET_L2_PPP``)
* Cellular modem driver (``CONFIG_MODEM_CELLULAR``)
* Network shell (``CONFIG_NET_SHELL``)
* Zperf (``CONFIG_NET_ZPERF``)
* Power management (``CONFIG_PM_DEVICE``)

References
**********

* :ref:`sm_as_zephyr_modem`
* :ref:`serial_modem_app`
* `Zephyr Networking`_
* `Zephyr Network APIs`_
* `Zephyr Modem modules`_
