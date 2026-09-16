.. _lib_sm_xdfu:

|SM| XDFU library
#################

.. contents::
   :local:
   :depth: 2

.. note::

   This library is `Experimental <Software maturity levels_>`_.

The |SM| XDFU library implements the host side of the ``AT#XDFU`` update sequence, so that a host MCU can update the nRF91 Series SiP that runs the :ref:`serial_modem_app`.
This library is intended for applications running on an external MCU that use Zephyr's ``modem_cellular`` driver to control the |SM| device over UART, for example, the :ref:`sm_ppp_shell_sample`.

Overview
********

The |SM| XDFU library drives the full DFU exchange described in :ref:`DFU_AT_commands` on behalf of the application:

* Sends ``AT#XDFUINIT``, ``AT#XDFUWRITE``, and ``AT#XDFUAPPLY`` for one of the following image types: application firmware, delta modem firmware, full modem firmware, or the MCUboot bootloader.
* Streams the update file from a mounted file system to the |SM| device in chunks, using the ``modem_cellular`` driver's UART pipe directly.
  This means the pipe must not be otherwise in use by the driver's own chat scripts while the update runs.
* For full modem firmware updates, decodes the CBOR-encoded update package (as produced for |NCS| full modem firmware updates) and writes each firmware segment to the address indicated in the package.
* Resets the |SM| device with ``AT#XRESET`` after the update (except after a successful full modem firmware update, where the final ``AT#XDFUAPPLY`` already reboots the modem into the new firmware) and waits for it to become ready again.

The library exposes a single blocking call, :c:func:`sm_xdfu_run`, that runs the whole sequence for one file and returns only once the update has completed, failed, or timed out.

Configuration
*************

Configure the following Kconfig option to enable the library:

* ``CONFIG_SM_XDFU_LIB`` - Enables the |SM| XDFU library.

The library also depends on the following features, which must be enabled by the application:

* ``CONFIG_MODEM_CELLULAR`` - Provides the modem device and UART pipe that the library attaches to.
* ``CONFIG_FILE_SYSTEM`` - Provides access to the update file.
* ``CONFIG_ZCBOR`` - Decodes the CBOR-encoded full modem firmware update package.
* ``CONFIG_COMMON_LIBC_MALLOC`` - Provides dynamic memory for the library's internal chat instance and CBOR metadata buffer.

Usage
*****

Call :c:func:`sm_xdfu_run` with the ``modem_cellular``-compatible device for the |SM| device, the image type to update, and the path to the update file on a mounted file system:

.. code-block:: c

   #include <sm_xdfu.h>

   const struct device *modem = DEVICE_DT_GET_ONE(nordic_nrf91_sm_v2);

   int ret = sm_xdfu_run(modem, SM_XDFU_TYPE_APP, "/lfs1/app_update.bin");

   if (ret < 0) {
       /* Handle error, see sm_xdfu_run() return values */
   }

Before calling :c:func:`sm_xdfu_run`, ensure that the modem device and its underlying UART are not in use, for example, by suspending networking on the ``modem_cellular`` device.
The function returns ``-EBUSY`` if either is in use.

The :ref:`sm_ppp_shell_sample` sample wraps this call in an ``xdfu`` shell command, see its source for a complete example, including how to report the different error codes in your application.

API documentation
******************

| Header file: :file:`include/sm_xdfu.h`
| Source file: :file:`lib/sm_xdfu/sm_xdfu.c`

.. doxygengroup:: sm_xdfu
   :members:
