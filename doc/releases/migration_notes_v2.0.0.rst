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

* ``AT#XNRFCLOUDPOS``:

  * Changed ``<cell_pos>`` parameter to ``<cell_count>``. The meaning changes from no cell positioning, single-cell or multi-cell to the number of cells to be included in the location request.
    ``0`` means that cellular positioning is not requested at all.
  * The ``AT#XNRFCLOUDPOS`` command has been updated to use the ``AT%NCELLMEAS`` command internally so the host must not use it anymore.
  * ``#XNRFCLOUDPOS`` notification now includes the status of the location request. The syntax has changed from:
    ::
      #XNRFCLOUDPOS: <error>
      #XNRFCLOUDPOS: <type>,<latitude>,<longitude>,<uncertainty>
    to:
    ::
      #XNRFCLOUDPOS: <status>[,<type>,<latitude>,<longitude>,<uncertainty>]

Informational changes
*********************

The following changes are listed for informational purposes, and many hosts will work without any changes.

* Ring Indication (RI) - Change RI from pulse (100 ms) to level triggered, meaning RI stays asserted until the host asserts DTR.
  After the Serial Modem has enabled UART, RI will be deasserted.
* HTTP client has been added and it's enabled by default. Use CONFIG_SM_HTTPC=n if you do not need it and want to save flash.
* nRF Cloud transport has been changed from MQTT to CoAP.
* ``CONFIG_SM_NRF_CLOUD_LOCATION`` is enabled by default whenever ``CONFIG_SM_NRF_CLOUD`` is enabled. Use ``CONFIG_SM_NRF_CLOUD_LOCATION=n`` if you do not need it and want to save flash.
