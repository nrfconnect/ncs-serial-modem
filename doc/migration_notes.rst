.. _migration_3.1.x_SM:

Migration notes
###############

.. contents::
   :local:
   :depth: 3

This document describes the changes required or recommended when migrating your application from the |NCS| v3.1.x Serial LTE modem (SLM) to the |addon|.

Upcoming breaking changes
*************************

Most of the |SM| application APIs will remain the same as in the |NCS| SLM, but there will be significant breaking changes in the host implementation in future versions of the |addon|.
The following are the most important upcoming breaking changes:

.. toggle::

   * Power and indicate pins modified into DTR and RI pins.
   * Default line termination changed from ``<CRLF>`` to ``<CR>``.
   * A ``<socket_id>`` field added to many socket operations as the first parameter.
   * PPP will not start automatically with ``AT+CFUN=1`` but you need to issue ``AT#XPPP=1`` before or after ``AT+CFUN=1``.
   * Rename the following AT commands:

     * ``AT#SLMVER``to ``AT#SMVER``
     * ``AT#GPS`` to ``AT#GNSS``

   * Removed the ``AT#XPOLL`` command.
     Use ``AT#XAPOLL`` instead.

.. _migration_3.1.x_SM_required:

Required changes
****************

The following changes are mandatory to make your application work in the same way as in previous releases.

.. toggle::

   This section gives instructions on how to migrate from the NCS v3.1.x Serial LTE Modem to the |SM| Add-On:

   * API and functionality:

     * There are no API or functional changes.
     * The ``AT#SLMVER`` command has not been renamed.
     * Same commands can be used but you need to do the following renaming if you are setting any |SM| related Kconfig options in overlays or command line:

       * Rename ``CONFIG_SLM_*`` to ``CONFIG_SM_*``
       * Rename ``CONFIG_MODEM_SLM_*`` to ``CONFIG_SM_HOST_*``

   * Code patches:

     * Rnamed the file name from ``slm_`` to ``sm_`` and ``modem_slm`` to ``sm_host``.
     * Functions and other symbols in the code have been renamed accordingly making automatic patching to likely fail.
