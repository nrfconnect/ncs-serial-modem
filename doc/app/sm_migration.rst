.. _sm_migration:

Migration
#########

.. contents::
   :local:
   :depth: 2

This is a migration guide to help move from NCS 3.1.x Serial LTE Modem (https://docs.nordicsemi.com/bundle/ncs-3.1.1/page/nrf/applications/serial_lte_modem/README.html) to Serial Modem add-on.

There are several breaking changes between Serial Modem add-on and NCS SLM, such as renaming, file movement, AT command changes, overlay file changes, etc.
Below you'll see all the changes that should be taken into account.

General
*******

The base of the Serial Modem add-on repository was done by taking a copy of NCS SLM and related components from NCS commit 437f372b37849fe215243f8de48847d578976c13.

.. toggle::

The following parts of NCS were copied into this repository:

   * applications/serial_lte_modem -> app
   * lib/modem_slm -> lib/sm_host
   * samples/cellular/slm_shell -> samples/sm_host_shell
   * include/modem/modm_slm.h -> include/sm_host.h
   * doc/nrf/libraries/modem/modem_slm.rst -> doc/lib/sm_host.rst

General renaming
****************

.. toggle::

   * Kconfig options:

     * Rename `CONFIG_SLM_*` to `CONFIG_SM_*`
     * Rename `CONFIG_MODEM_SLM_*` to `CONFIG_SM_HOST_*`

   * Names of files, functions, structures, enumerations etc.:

     * Renamed `slm_*` to `sm_*`
     * Renamed `modem_slm_*` to `sm_host_*`

`AT#XGPS` renamed to `AT#XGNSS`
************************************

.. toggle::

   * `AT#XGPS` command has been renamed to `AT#XGNSS`
   * `AT#XGPSDEL` command has been renamed to `AT#XGNSSDEL`
