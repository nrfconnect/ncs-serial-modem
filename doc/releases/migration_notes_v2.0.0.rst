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

* Full FOTA - When compiling, rename ``overlay-full_fota.conf`` to ``overlay-full-fota.conf`` and add ``PM_STATIC_YML_FILE=pm_static_nrf9151dk_nrf9151_ns_full_fota.yml`` to the build configuration.
  See :ref:`SM_AT_FOTA` for more information.

* ``AT#XNRFCLOUDPOS``:

  * Changed ``<cell_pos>`` parameter to ``<cell_count>``. The meaning changes from no cell positioning, single-cell or multi-cell to the number of cells to beincluded in the location request.
    ``0`` means that cellular positioning is not requested at all.
  * The ``AT#XNRFCLOUDPOS`` command has been updated to use the ``AT%NCELLMEAS`` command internally so the host must not use it anymore.

Informational changes
*********************

The following changes are listed for informational purposes, and many hosts will work without any changes.

* Ring Indication (RI) - Change RI from pulse (100 ms) to level triggered, meaning RI stays asserted until the host asserts DTR.
  After the Serial Modem has enabled UART, RI will be deasserted.
* nRF Cloud transport has been changed from MQTT to CoAP.
* HTTP client has been added and it's enabled by default. Use CONFIG_SM_HTTPC=n if you do not need it and want to save flash.
