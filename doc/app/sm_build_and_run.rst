.. _sm_build_and_run:

Building and running
####################

.. contents::
   :local:
   :depth: 3

The |SM| application can be found under :file:`app` in the |SM| folder structure.

For more security, it is recommended to use the ``*/ns`` :ref:`variant <nrf:app_boards_names>` of the board target (see the Requirements section above.)
When built for this variant, the sample is configured to compile and run as a non-secure application using :ref:`security by separation <nrf:ug_tfm_security_by_separation>`.
Therefore, it automatically includes :ref:`Trusted Firmware-M <nrf:ug_tfm>` that prepares the required peripherals and secure services to be available for the application.

To build the sample, follow the instructions in :ref:`Building an application <nrf:building>` for your preferred building environment.
See also :ref:`Programming an application <nrf:programming>` for programming steps and :ref:`Testing and optimization <nrf:test_and_optimize>` for general information about testing and debugging in the |NCS|.

.. note::
   |sysbuild_autoenabled_ncs|

.. _sm_connecting_91dk:

Communicating with the modem on an nRF91 Series DK
**************************************************

In this scenario, an nRF91 Series DK running the |SM| application serves as the host.
You can use either a PC or an external MCU as a client.

.. _sm_connecting_91dk_pc:

Connecting with a PC
====================

To connect to an nRF91 Series DK with a PC:

.. sm_connecting_91dk_pc_instr_start

1. Verify that ``UART_0`` is selected in the application.
   It is defined in the default configuration.

2. Use the `Serial Terminal app`_ to connect to the development kit.
   See :ref:`Testing and optimization <nrf:test_and_optimize>` for instructions.
   You can also use the :guilabel:`Open Serial Terminal` option of the `Cellular Monitor app`_ to open the Serial Terminal app.
   Using the Cellular Monitor app in combination with the Serial Terminal app shows how the modem responds to the different modem commands.
   You can then use this connection to send or receive AT commands over UART, and to see the log output of the development kit.

   Instead of using the Serial Terminal app, you can use PuTTY to establish a terminal connection to the development kit, using the :ref:`default serial port connection settings <nrf:test_and_optimize>`.

   .. note::

      The default AT command terminator is a carriage return (``\r``).
      The Serial Terminal app, PuTTY and many terminal emulators support this format by default.
      However, make sure that the configured AT command terminator corresponds to the line terminator of your terminal.
      See :ref:`sm_config_options` for more details on AT command terminator choices.

.. sm_connecting_91dk_pc_instr_end

.. _sm_connecting_91dk_mcu:

Connecting with an external MCU
===============================

.. note::

   This section does not apply to Thingy:91 X.

If you run your user application on an external MCU (for example, an nRF54 Series development kit), you can control the |SM| application on an nRF91 Series device directly from the application.
See the :ref:`sm_at_client_shell_sample` for a sample implementation of such an application.

To connect with an external MCU using UART_2, include the :file:`external-mcu.overlay` devicetree overlay in your build.
This overlay configures the UART_2 pins, DTR pin, and RI pin for the nRF9151 DK.

If you use a different setup, you can customize the :file:`external-mcu.overlay` file to match your hardware configuration in (for example) the following ways:

* Change the highlighted UART baud rate or DTR and RI pins:

  .. parsed-literal::
     :class: highlight

     &uart2 {
        compatible = "nordic,nrf-uarte";
        current-speed = <**115200**>;
        hw-flow-control;
        status = "okay";

        dtr_uart2: dtr-uart {
           compatible = "nordic,dtr-uart";
           dtr-gpios = <**&gpio0 31** (GPIO_PULL_UP | GPIO_ACTIVE_LOW)>;
           ri-gpios = <**&gpio0 30** GPIO_ACTIVE_LOW>;
           status = "okay";
        };
        pinctrl-0 = <&uart2_default_alt>;
        pinctrl-1 = <&uart2_sleep_alt>;
        pinctrl-names = "default", "sleep";
     };

* Change the highlighted UART pins:

  .. parsed-literal::
     :class: highlight

     &pinctrl {
        uart2_default_alt: uart2_default_alt {
           group1 {
              psels = <NRF_PSEL(UART_RX, **0, 3**)>,
                      <NRF_PSEL(UART_CTS, **0, 7**)>;
              bias-pull-up;
           };
           group2 {
              psels = <NRF_PSEL(UART_TX, **0, 2**)>,
                      <NRF_PSEL(UART_RTS, **0, 6**)>;
           };
        };

        uart2_sleep_alt: uart2_sleep_alt {
           group1 {
              psels = <NRF_PSEL(UART_TX, **0, 2**)>,
                      <NRF_PSEL(UART_RX, **0, 3**)>,
                      <NRF_PSEL(UART_RTS, **0, 6**)>,
                      <NRF_PSEL(UART_CTS, **0, 7**)>;
              low-power-enable;
           };
        };
     };

The following table shows how to connect selected development kit to an nRF91 Series development kit to be able to communicate through UART:

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

Use the following UART devices:

* nRF54 Series DK - UART30
* nRF91 Series DK - UART2

The UART configuration must match on both sides.
By default the |SM| application and :ref:`sm_at_client_shell_sample` use the following settings:

* Hardware flow control: enabled
* Baud rate: 115200
* Parity bit: no

Communicating with the modem on Thingy:91 X
*******************************************

In this scenario, Thingy:91 X running the |SM| application serves as the host.
You can use a PC as a client.

Connecting with a PC
====================

The nRF5340 SoC of Thingy:91 X is pre-programmed with the :ref:`Connectivity bridge <nrf:connectivity_bridge>` application.
To update the Connectivity bridge application, see the :ref:`Updating the Thingy:91 X firmware using nRF Util <nrf:updating_firmware_nrf5340>`  documentation.
The Connectivity bridge application routes ``UART_0`` to ``USB_CDC0`` on Thingy:91 X.
By enabling the ``CONFIG_BRIDGE_BLE_ENABLE`` Kconfig option in the Connectivity bridge application, you can also use |SM| over :ref:`Nordic UART Service (NUS) <nrf:nus_service_readme>`.

To connect to a Thingy:91 X with a PC:

.. include:: sm_build_and_run.rst
   :start-after: .. sm_connecting_91dk_pc_instr_start
   :end-before: .. sm_connecting_91dk_pc_instr_end

.. _sm_testing_section:

Testing
*******

The following testing instructions focus on testing the application with a PC client.

|test_sample|

1. |connect_kit|
#. :ref:`Connect to the kit with the Serial Terminal app <nrf:test_and_optimize>`.
   You can also use the :guilabel:`Open Serial Terminal` option of the `Cellular Monitor app`_ to open the Serial Terminal app.
   If you want to use a different terminal emulator, see :ref:`sm_connecting_91dk_pc`.
#. Reset the kit.
#. Observe that the development kit sends a ``Ready\r\n`` message on UART.
#. Send AT commands and observe the responses from the development kit.

See :ref:`sm_testing` for typical test cases.

.. note::

   If the initialization of |SM| fails, the application sends an ``INIT ERROR\r\n`` message on UART if it has managed to enable UART.
   See the logs for more information about the error.
   The logs are written to RTT by default.

.. _sm_carrier_library_support:

Using the LwM2M carrier library
*******************************

The application supports the |NCS| :ref:`LwM2M carrier <nrf:liblwm2m_carrier_readme>` library that you can use to connect to the operator's device management platform.
See the library's documentation for more information and configuration options.

To enable the LwM2M carrier library, add the following parameters to your build command:

.. code-block:: none

   -DEXTRA_CONF_FILE=carrier.conf
   -DEXTRA_DTC_OVERLAY_FILE=carrier-nrf9151.overlay

The CA root certificates that are needed for modem FOTA are not provisioned in the |SM| application.
You can flash the :ref:`Cellular: LwM2M carrier <nrf:lwm2m_carrier>` sample to write the certificates to modem before flashing the |SM| application, or use the :ref:`Cellular: AT Client <nrf:at_client_sample>` sample as explained in :ref:`preparing the Cellular: LwM2M Client sample for production <nrf:lwm2m_client_provisioning>`.
It is also possible to modify the |SM| application project itself to include the certificate provisioning, as demonstrated in the :ref:`Cellular: LwM2M carrier <nrf:lwm2m_carrier>` sample.

.. code-block:: c

   int lwm2m_carrier_event_handler(const lwm2m_carrier_event_t *event)
   {
           switch (event->type) {
           case LWM2M_CARRIER_EVENT_INIT:
                   carrier_cert_provision();
           ...


The certificate provisioning can also be done directly in the |SM| application by using the same AT commands as described for the :ref:`Cellular: AT Client <nrf:at_client_sample>` sample.

When the :ref:`LwM2M carrier <nrf:liblwm2m_carrier_readme>` library is in use, by default the application will auto-connect to the network on startup.
This behavior can be changed by disabling the :option:`CONFIG_SM_AUTO_CONNECT` option.

.. _sm_build_disable_b0:

Building without the immutable B0 bootloader
********************************************

By default, the |SM| application uses an immutable bootloader B0 (NSIB) and an upgradeable MCUboot bootloader.

Version 1.x.x of the |SM| application uses only an upgradeable MCUboot bootloader.
Building without the B0 bootloader is offered as backwards compatibility option if you want to maintain the same partition layout as in |SM| version 1.x.x.

To build without B0 and with a single, non-updatable MCUboot slot, include :file:`disable-b0-nrf9151.overlay` in both the application and MCUboot images and disable the secure boot appcore in the sysbuild configuration:

.. code-block:: console

   west build -p -b nrf9151dk/nrf9151/ns -- -DEXTRA_DTC_OVERLAY_FILE=disable-b0-nrf9151.overlay -Dmcuboot_EXTRA_DTC_OVERLAY_FILE=<path-to-app>/disable-b0-nrf9151.overlay -DSB_CONFIG_SECURE_BOOT_APPCORE=n

Where ``<path-to-app>`` is the absolute path to the :file:`app` directory.

This configuration removes the ``b0`` and ``s1`` partitions, giving MCUboot a single 64 kB slot.
