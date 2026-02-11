.. _sm_at_client_shell_sample:

AT Client Shell sample
######################

.. contents::
   :local:
   :depth: 2

The |SM| AT Client Shell sample demonstrates how to send AT commands to the modem through the :ref:`serial_modem_app` running on an nRF91 Series SiP.
This sample enables an external MCU to send modem and |SM| proprietary AT commands for LTE connection and IP services.
See more information on the functionality of this sample from the :ref:`lib_sm_at_client`, which provides the core functionality for this sample.

Requirements
************

The |SM| application should be configured with :file:`app/overlay-external-mcu.overlay` on the nRF91 Series DK side.

The sample supports the following development kit:

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
     - ``nrf54l15dk/nrf54l15/cpuapp``

Connect the DK pins that are defined in the board-specific overlay files :file:`samples/sm_at_client_shell/boards/*.overlay` to the corresponding pins in the nRF91 Series DK, which are defined in :file:`app/overlay-external-mcu.overlay`.

The following table shows how to connect UART, DTR and RI pins of the DK to the corresponding pins in the nRF91 Series DKs:

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
    You must disable the VCOM0 on the nRF54L15 DK to release the UART GPIO pins to use it with the :ref:`sm_at_client_shell_sample`.

    * For nRF54L15 DK, you can use the `Board Configurator app`_ to disable the ``Connect port VCOM0`` setting.

.. note::
    The GPIO output levels on the nRF91 Series device and nRF54L15 DK must be the same.

    * You can set the VDD voltages for both devices with the `Board Configurator app`_.

References
**********

* `nRF91x1 AT Commands Reference Guide`_
* :ref:`SM_AT_commands`

Dependencies
************

This sample uses the following |NCS| libraries:

* :ref:`lib_sm_at_client`
