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

* Full FOTA - When compiling, rename ``overlay-full_fota.conf`` to ``overlay-full-fota.conf`` and add ``overlay-full-fota.overlay`` to the build configuration.
  See :ref:`SM_AT_FOTA` for more information.

Custom static partition layout migration
----------------------------------------

The |SM| no longer uses the |NCS| Partition Manager.
All flash and SRAM partitions are now defined in devicetree overlays instead of ``pm_static_*.yml`` files.

If you maintained a custom ``pm_static_*.yml`` file, recreate the partition layout as a devicetree overlay, using the files in :file:`app/boards/` and :file:`app/overlay-*.overlay` as a reference.
For a general guide on migrating from Partition Manager to DTS, see the |NCS| `PM to DTS migration <migration_partitions_>`_ page.

If you are planning to upgrade the applications in field from v1.x.x to v2.0.0 and were using the default partition layout, see :ref:`sm_build_disable_b0` for how to disable the B0 partition, which is used by default in v2.0.0.

Informational changes
*********************

The following changes are listed for informational purposes, and many hosts will work without any changes.

* Application rollback prevention - Application firmware DFU now uses MCUboot downgrade prevention (``CONFIG_MCUBOOT_DOWNGRADE_PREVENTION``).
  The device rejects signed application images older than the version currently running.
  Set :file:`app/VERSION` before each published build; see :ref:`sm_releasing`.

* ``AT#XSMVER`` - No change to the response layout for hosts.
  ``<sm_version>`` is still a quoted Git-describe-style string (for example ``"v2.0.0-18-g2c85d9224fca"``).
  Existing parsers can keep using it as in v1.x.x.
  The ``major.minor.patch`` part now follows :file:`app/VERSION` (same value used for rollback prevention), the ``-N-g<hash>`` suffix still comes from Git.

* Ring Indication (RI) - Change RI from pulse (100 ms) to level triggered, meaning RI stays asserted until the host asserts DTR.
  After the Serial Modem has enabled UART, RI will be deasserted.
* nRF Cloud transport has been changed from MQTT to CoAP.
* HTTP client has been added and it's enabled by default. Use CONFIG_SM_HTTPC=n if you do not need it and want to save flash.
