.. _sm_as_zephyr_modem:

nRF91 Series as a Zephyr-compatible modem
#########################################

.. contents::
   :local:
   :depth: 2

Overview
********

Starting with |NCS| v2.6.0, the |SM| (SM) application can be used to turn an nRF91 Series SiP into a standalone modem that can be used through Zephyr's cellular modem driver.
This means that the controlling chip can run a Zephyr application that seamlessly uses Zephyr's IP stack instead of offloaded sockets through AT commands.

This is made possible by |SM|'s support of CMUX and PPP and Zephyr's cellular modem driver.

The `nRF52840 SoC present in the nRF9160 DK <nRF9160dk_nRF52840_>`_ can even be made to be the controlling chip with the appropriate configuration (described below).
This is only possible on the nRF9160 DK, not on the other nRF91 Series DKs.

.. note::

   It is not yet possible to make use of the nRF91 Series' GNSS functionality when Zephyr's cellular modem driver controls the SiP.
   Also, the driver's support of power saving features is quite limited, only allowing complete power off of the SiP.

Configuration
*************

Both |SM| and the Zephyr application running on the controlling chip must be compiled with the appropriate configuration to make them work together.

For that, Kconfig fragments and devicetree overlays must be added to the compilation command.

See the :ref:`sm_config_files` section for information on how to compile with additional configuration files and a description of some of the mentioned Kconfig fragments.

.. include:: CMUX_AT_commands.rst
   :start-after: sm_cmux_baud_rate_note_start
   :end-before: sm_cmux_baud_rate_note_end

nRF91 Series SiP running |SM|
=============================

The following configuration files must be included:

* :file:`overlay-cmux.conf` - To enable CMUX.
* :file:`overlay-ppp.conf` - To enable PPP.
* :file:`overlay-zephyr-modem.conf` - To tailor |SM| to how Zephyr's cellular modem driver works.
  This enables the :ref:`CONFIG_SM_START_SLEEP <CONFIG_SM_START_SLEEP>` Kconfig option, which makes the nRF91 Series SiP start only when the :ref:`power pin <CONFIG_SM_POWER_PIN>` is toggled.

In addition, if the controlling chip is an external MCU, the following configurations must also be included:

* :ref:`CONFIG_SM_POWER_PIN <CONFIG_SM_POWER_PIN>` Kconfig option - To define the power pin so that it matches your setup.
* :file:`overlay-external-mcu.overlay` - To configure which UART |SM| will use.
  The actual configuration of the UART is defined in the :file:`*_ns.overlay` overlay file matching your board in the :file:`boards` directory.
  Make sure to update the UART configuration (pins, baud rate) so that it matches your setup.
  You can do this by modifying the properties of the node of the UART to be used, by editing either :file:`overlay-external-mcu.overlay` or the overlay file matching your board in the :file:`boards` directory.

Or, if the controlling chip is the nRF52840 of the nRF9160 DK, the following files must also be included:

* :file:`overlay-zephyr-modem-nrf9160dk-nrf52840.conf` - To define the power pin.
* :file:`overlay-zephyr-modem-nrf9160dk-nrf52840.overlay` - To configure the UART to be routed between the nRF9160 and the nRF52840 of the DK.

Finally, if you want more verbose logging that includes the AT commands and responses, you can enable debug logging by uncommenting ``CONFIG_SM_LOG_LEVEL_DBG=y`` in the :file:`prj.conf` configuration file.

Controlling chip running Zephyr
===============================

Configuration files found in `Zephyr’s Cellular modem`_ sample are a good starting point.

Specifically, regardless of what the controlling chip is, the Kconfig options found in the following files are needed:

* :file:`prj.conf` - This file enables various Zephyr APIs, most of which are needed for proper functioning of the application.
* :file:`boards/nrf9160dk_nrf52840.conf` - This file tailors the configuration of the modem subsystem and driver to the |SM|.
  It makes the application's logs be output on UART 0 and also enables the debug logs of the cellular modem driver.
  If you do not want the debug logs output by the driver, you may turn them off by removing ``CONFIG_MODEM_LOG_LEVEL_DBG=y``.

In addition, depending on what the controlling chip is, the following devicetree overlay files are also needed.
They define the modem along with the UART it is connected to and its power pin.

If the controlling chip is an external MCU:

* :file:`boards/nrf9160dk_nrf9160_ns.overlay` - The UART configuration and power pin can be customized according to your setup.

If the controlling chip is the nRF52840 of the nRF9160 DK:

* :file:`boards/nrf9160dk_nrf52840.overlay` - The UART and power pin are configured to be routed to the nRF9160.

Developing the Zephyr application
*********************************

To get started developing the Zephyr application running on the controlling chip, look at the code of `Zephyr’s Cellular modem`_ sample to see how the modem is managed and used.
You can even compile, flash and run the sample to verify proper operation of the modem.

Flashing and running
********************

When built with the Zephyr-compatible modem configuration, |SM| will put the nRF91 Series SiP to deep sleep when powered on.
Zephyr's cellular modem driver running on the controlling chip will take care of waking up the nRF91 Series SiP, so it is advised to first flash |SM| to the nRF91 Series SiP.

However, before flashing the |SM| built with the Zephyr-compatible modem configuration, make sure that the nRF91 Series modem has been set to the desired system mode.
For this, you will need a regular |SM| running in the nRF91 Series SiP to be able to run AT commands manually.
To set the modem to the desired system mode, issue an ``AT%XSYSTEMMODE`` command followed by an ``AT+CFUN=0`` command so that the modem saves the system mode to NVM.
For example, to enable only LTE-M, issue the following command: ``AT%XSYSTEMMODE=1,0,0,0``
You need to do this because the modem's system mode is not automatically set at any point, so the one already configured will be used.

Additionally, if the controlling chip is an external MCU:

* Make sure that the UART and the power pin are wired according to how they are configured in both the external MCU and the nRF91 Series SiP.

Or if the controlling chip is the nRF52840 of the nRF9160 DK:

* Make sure that the **PROG/DEBUG SW10** switch on the DK is set to **NRF91** when flashing |SM|, and to **NRF52** when flashing the Zephyr application.
  The switch also affects which chip is reset when the Reset button is pressed.
* No wiring is needed as the routing between the pins happens internally.

To observe the operation sequence, you can view the application logs coming from both chips.

By default, |SM| will output its logs through RTT, and the Zephyr application running on the controlling chip through its UART 0.
The RTT logs can be seen with an RTT client such as ``JLinkRTTViewer``.
If |SM| is running on the nRF9160 DK, the **PROG/DEBUG SW10** switch needs to be set to **NRF91** to be able to receive the RTT logs.
However, for convenience you may want to redirect |SM|'s logs to the SiP's UART 0 so that you do not need to reconnect the RTT client every time the board is reset.
See the :ref:`sm_additional_config` section for information on how to do this.

The logs output through UART can be seen by connecting to the appropriate UART with a serial communication program.
Under Linux, if the controlling chip is the nRF52840 of the nRF9160 DK, the device file of its UART 0 will typically be :file:`/dev/ttyACM1`.

After both applications have been flashed to their respective chips and you are connected to receive logs, you can reset the controlling chip.
When the Zephyr application starts up, the following happens:

* If power management is enabled (the ``CONFIG_PM_DEVICE`` Kconfig option is set to ``y``): when the application powers on the modem (by calling ``pm_device_action_run(<dev>, PM_DEVICE_ACTION_RESUME)`` as the sample does), the cellular modem driver will toggle the modem's power pin to wake it up.

  If power management is not enabled, the cellular modem driver will automatically proceed and expect |SM| to already be started and in a pristine state.
  In this case, |SM| should be compiled with the :ref:`CONFIG_SM_START_SLEEP <CONFIG_SM_START_SLEEP>` Kconfig option set to ``n``, and :ref:`CONFIG_SM_POWER_PIN <CONFIG_SM_POWER_PIN>` can be left undefined.

* The cellular modem driver will start sending AT commands to |SM|.
  It will enable the network status notifications, gather some information from the modem, enable CMUX, and set the modem to normal mode (with an ``AT+CFUN=1`` command).
  This will result in |SM|'s PPP starting automatically when the network registration is complete.

* From this point onwards, once the Zephyr application has brought up the driver's network interface, it will be able to send and receive IP traffic through it.
  The `Zephyr’s Cellular modem`_ sample does this.
