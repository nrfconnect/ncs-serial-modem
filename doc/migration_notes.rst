.. _migration_3.1.x_SM:

Migration notes
###############

.. contents::
   :local:
   :depth: 3

This migration note helps you to move from |NCS| v3.1.x `Serial LTE modem (SLM) <Serial LTE modem_>`_ to the |addon|.

There are several breaking changes between |addon| and |NCS| SLM, such as renaming, file movement, AT command changes, overlay file changes, and so on.
The following sections cover all the changes that must be taken into account.

Background
**********

The base of the |addon| repository is a copy of |NCS| SLM and related components from the |NCS| commit ``437f372b37849fe215243f8de48847d578976c13``.
Most of the |SM| application APIs remain the same as in the |NCS| SLM, but with significant breaking changes from the host implementation's perspective in the future versions of the |addon|.

The following |NCS| files were copied into this repository:

* :file:`applications/serial_lte_modem` to :file:`app`
* :file:`lib/modem_slm` to :file:`lib/sm_host`
* :file:`samples/cellular/slm_shell` to :file:`samples/sm_host_shell`
* :file:`include/modem/modm_slm.h` to :file:`include/sm_host.h`
* :file:`doc/nrf/libraries/modem/modem_slm.rst` to :file:`doc/lib/sm_host.rst`

.. _migration_3.1.x_SM_required:

Required changes
****************

The following changes are mandatory to make your application work in the same way as in previous releases.

.. toggle::

   This section gives instructions on how to migrate from the NCS v3.1.x Serial LTE Modem to the |SM| Add-On:

   * Rename the use of the following Kconfig options:

     * Rename ``CONFIG_SLM_*`` to ``CONFIG_SM_*``
     * Rename ``CONFIG_MODEM_SLM_*`` to ``CONFIG_SM_HOST_*``

   * Code patches:

     * Renamed the file name from ``slm_`` to ``sm_`` and ``modem_slm`` to ``sm_host``.
     * Functions and other symbols in the code have been renamed accordingly making automatic patching to likely fail.

   * Rename the following AT commands:

     * ``AT#GPS`` to ``AT#GNSS``
     * ``AT#XGPSDEL`` command has been renamed to ``AT#XGNSSDEL``

   * Removed the ``AT#XPOLL`` command.
     Use ``AT#XAPOLL`` instead.
