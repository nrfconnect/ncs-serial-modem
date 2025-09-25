# Serial Modem

The Serial Modem Add-On contains the Serial Modem application for nRF91-based devices, and is built on [nRF Connect SDK](https://www.nordicsemi.com/Products/Development-software/nRF-Connect-SDK) (NCS).
It includes host examples in the form of the Serial Modem Host library and the Serial Modem Host Shell sample.

## Background

The base of the Serial Modem Add-On has been copied from NCS commit `437f372b37849fe215243f8de48847d578976c13` on 17th Sep 2025.
The following files and folders were copied:

   * `applications/serial_lte_modem`
   * `lib/modem_slm`
   * `samples/cellular/slm_shell`
   * `include/modem/modm_slm.h`
   * `doc/nrf/libraries/modem/modem_slm.rst`

All of the above content will be removed from the NCS before its 3.2.0 release.

The following renaming has been done for filenames, Kconfig options, code symbols, etc.:

   * Serial LTE Modem -> Serial Modem
     * slm -> sm
     * `CONFIG_SLM_*` -> `CONFIG_SM_*`
   * Modem SLM library -> Serial Modem Host library
     * modem_slm -> sm_host
     * `CONFIG_MODEM_SLM_*` -> `CONFIG_SM_HOST_*`
   * SLM Shell sample -> Serial Modem Host Shell sample
     * slm_shell -> sm_host_shell

## Documentation

The documentation for the Serial Modem Add-On resides in the `/doc` folder.
At the moment, the content is a direct copy of the NCS versions. This means that, the RST files are not processed into a human friendly format. This will be improved in the future.
Documentation can be viewed in Github and it is rendered into some extent but the links are broken. [NCS v3.1.x Serial LTE Modem](https://docs.nordicsemi.com/bundle/ncs-3.1.1/page/nrf/applications/serial_lte_modem/README.html) documentation is still valid and can be used as a reference.

## Getting started

Fetching the repository:

```shell
# Initialize workspace
west init -m https://github.com/nrfconnect/ncs-serial-modem.git --mr main serial_modem

cd serial_modem

# Update nRF Connect SDK modules. This may take a while.
west update
```

Building and flashing nRF9151 DK:

```shell
# Assuming you are in the serial_modem folder
cd project/app

# Pristine build
west build -p -b nrf9151dk/nrf9151/ns

# Flashing
west flash
```

The application is now built and flashed to the device. You can open a serial terminal to use the application. The default baud rate is 115200.

## Upcoming breaking changes

While majority of the Serial Modem application API remain unchanged from the NCS SLM, there will also be breaking changes from the host implementations perspective in the future releases of the Serial Modem Add-On. Here are the most important changes that are planned currently:

   * Power and indicate pins modified into DTR and RI pins
   * Default line termination changed from `<CRLF>` to `<CR>`
   * A `<socket_id>` field added for many socket operations as the first parameter
   * PPP will not start automatically with `AT+CFUN=1` but you need to issue `AT#XPPP=1` before or after `AT+CFUN=1`
   * Renaming: `AT#SLMVER` -> `AT#SMVER`
   * Renaming: `AT#GPS` -> `AT#GNSS`
   * Removing: `AT#XPOLL` (use `AT#XAPOLL`)

## Migration Guide

This section gives instructions on how to migrate from the NCS v3.1.x Serial LTE Modem to the Serial Modem Add-On:

   * API and functionality
     * There are no API change or functional changes
     * Even `AT#SLMVER` command has not been renamed yet
   * Compilation
     * Same commands can be used but you need to do the following renaming if you are setting any Serial Modem related Kconfig options in overlays or command line
     * Rename `CONFIG_SLM_*` -> `CONFIG_SM_*`
     * Rename `CONFIG_MODEM_SLM_*` -> `CONFIG_SM_HOST_*`
   * Code patches
     * Unfortunately, adapting your own code patches will require some work
     * Filenames have been renamed: `slm_`-> `sm_` and `modem_slm`-> `sm_host`.
     * Functions and other symbols in the code have been renamed accordingly making automatic patching to likely fail

As the breaking changes to the Serial Modem Add-On are done, the migration guide will be updated.
