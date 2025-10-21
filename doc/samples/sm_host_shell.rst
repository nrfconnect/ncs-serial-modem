.. _sm_shell_sample:

|SM| Host Shell sample
######################

.. contents::
   :local:
   :depth: 2

The |SM| Host Shell sample demonstrates how to send AT commands to the modem through the :ref:`Serial Modem <sm_description>` application running on an nRF91 Series SiP.
This sample enables an external MCU to send modem and |SM| proprietary AT commands for LTE connection and IP services.
See more information on the functionality of this sample from the :ref:`lib_sm_host` library, which provides the core functionality for this sample.

Requirements
************

The |SM| application should be configured with :file:`app/overlay-external-mcu.overlay` on the nRF91 Series DK side.

The sample supports the following development kits:

.. list-table::
   :widths: auto
   :header-rows: 1

   * - Hardware platforms
     - PCA
     - Board name
     - Board target
   * - `nRF7002 DK <nRF7002dk_>`_
     - PCA10143
     - `nRF7002dk <nRF7002dk_>`_
     - ``nrf7002dk/nrf5340/cpuapp``
   * - `nRF5340 DK <nRF5340 DK_>`_
     - PCA10095
     - `nrf5340dk`_
     - ``nrf5340dk/nrf5340/cpuapp``
   * - `nRF52840 DK <nRF52840_DK_>`_
     - PCA10056
     - `nrf52840dk`_
     - ``nrf52840dk/nrf52840``

Connect the DK pins that are defined in the board-specific overlay files :file:`samples/sm_host_shell/boards/*.overlay` to the corresponding pins in the nRF91 Series DK, which are defined in :file:`app/overlay-external-mcu.overlay`.

The following table shows how to connect UART, DTR and RI pins of the DK to the corresponding pins in the nRF91 Series DKs:

.. tabs::

   .. group-tab:: nRF52840 DK

      .. list-table::
         :header-rows: 1

         * - nRF52840 DK
           - nRF91 Series DK
         * - UART TX P1.02
           - UART RX P0.11
         * - UART RX P1.01
           - UART TX P0.10
         * - UART CTS P1.07
           - UART RTS P0.12
         * - UART RTS P1.06
           - UART CTS P0.13
         * - DTR OUT P0.11
           - DTR IN P0.31
         * - RI IN P0.13
           - RI OUT P0.30
         * - GPIO GND
           - GPIO GND

      .. note::
         The GPIO output level on the nRF91 Series device side must be 3 V to work with the nRF52 Series DK.

         * For nRF9151 DK, you can set the VDD voltage with the `Board Configurator app`_.

   .. group-tab:: nRF5340 DK

      .. list-table::
         :header-rows: 1

         * - nRF5340 DK
           - nRF91 Series DK
         * - UART TX P1.04
           - UART RX P0.11
         * - UART RX P1.05
           - UART TX P0.10
         * - UART CTS P1.07
           - UART RTS P0.12
         * - UART RTS P1.06
           - UART CTS P0.13
         * - DTR OUT P0.23
           - DTR IN P0.31
         * - RI IN P0.28
           - RI OUT P0.30
         * - GPIO GND
           - GPIO GND

      .. note::
         The GPIO output level on the nRF91 Series device side must be 3 V to work with the nRF53 Series DK.

         * For nRF9151 DK, you can set the VDD voltage with the `Board Configurator app`_.

   .. group-tab:: nRF7002 DK

      .. list-table::
         :header-rows: 1

         * - nRF7002 DK
           - nRF91 Series DK
         * - UART TX P1.04
           - UART RX P0.11
         * - UART RX P1.05
           - UART TX P0.10
         * - UART CTS P1.07
           - UART RTS P0.12
         * - UART RTS P1.06
           - UART CTS P0.13
         * - DTR OUT P0.31
           - DTR IN P0.31
         * - RI IN P0.30
           - RI OUT P0.30
         * - GPIO GND
           - GPIO GND

      .. note::
         The GPIO output level on the nRF91 Series device side must be 1.8 V to work with the nRF7002 DK.

         * For nRF9151 DK, you can set the VDD voltage with the `Board Configurator app`_.

References
**********

* `nRF91x1 AT Commands Reference Guide`_
* :ref:`SM_AT_commands`

Dependencies
************

This sample uses the following |NCS| libraries:

* :ref:`lib_sm_host`
