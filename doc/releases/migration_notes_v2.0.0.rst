Migration notes for |SM| v2.0.0 (working draft)
###############################################

.. contents::
   :local:
   :depth: 3

This document provides guidance for migrating from |SM| v1.x.x to v2.0.0.
The general principle has been to keep v2.0.0 backwards compatible.
However, a small number of changes have been introduced to reduce both maintenance and flash usage.
Some of these modifications might not require any updates on the host side, even if corresponding features are in use.

Required changes
****************

The following changes are mandatory to make your application work in the same way as in previous releases.

* Application logging backend changed from RTT to UART — The default application log backend has changed from SEGGER RTT to UART1 (VCOM1 on the nRF9151 DK).
  The UART is suspended at startup and activated at runtime with ``AT#XLOG=1``.
  See :ref:`SM_AT_trace` for the full command reference.
  If you cannot move to UART logs, see :ref:`sm_logging_rtt` for how to re-enable RTT logs.

* Full FOTA - When compiling, rename ``overlay-full_fota.conf`` to ``overlay-full-fota.conf`` and add ``PM_STATIC_YML_FILE=pm_static_nrf9151dk_nrf9151_ns_full_fota.yml`` to the build configuration.
  See :ref:`SM_AT_FOTA` for more information.

Informational changes
*********************

The following changes are listed for informational purposes, and many hosts will work without any changes.

* Ring Indication (RI) - Change RI from pulse (100 ms) to level triggered, meaning RI stays asserted until the host asserts DTR.
  After the Serial Modem has enabled UART, RI will be deasserted.
* nRF Cloud transport has been changed from MQTT to CoAP.
* HTTP client has been added and it's enabled by default. Use CONFIG_SM_HTTPC=n if you do not need it and want to save flash.
