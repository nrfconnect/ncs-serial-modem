.. _SM_AT_PROVISIONING:

nRF Provisioning AT commands
*****************************

.. contents::
   :local:
   :depth: 2

.. note::

   These AT commands are `Experimental <Software maturity levels_>`_.

This page describes AT commands for the nRF Device Provisioning service.
The provisioning client connects to the `nRF Cloud Provisioning Service`_ over CoAP or DTLS and applies device configuration commands (credentials, settings, and firmware updates) issued from the cloud.

To use the provisioning service with the nRF9151 DK, you must first claim the device using its attestation token.
You can retrieve this token with the ``AT%ATTESTTOKEN`` command.
For more information, refer to the `nRF Cloud claiming Devices`_ documentation.

For other devices, follow the instructions in the `nRF Cloud Provisioning Service`_ documentation, including adding the necessary certificates.

Any steps that require nRF9151 application firmware support are handled by the |SM|.

These commands are only available when the application is built with the :file:`overlay-nrf-device-provisioning.conf` overlay, which enables the provisioning client and its AT command interface.

.. note::

   The provisioning client requires the host to manage the LTE connection.
   During a provisioning session, the modem must be taken offline to write credentials, then brought back online to reconnect to the provisioning server.
   The device signals these requirements through ``#XNRFPROV: 1`` and ``#XNRFPROV: 2`` notifications.
   See the :ref:`unsolicited notifications <xnrfprov_notifications>` section for details.

Trigger provisioning #XNRFPROV
===============================

The ``#XNRFPROV`` command triggers an immediate provisioning attempt.

Set command
-----------

The set command starts a provisioning session immediately, independently of the configured schedule interval.

Syntax
~~~~~~

::

   AT#XNRFPROV

Response syntax
~~~~~~~~~~~~~~~

::

   OK

.. _xnrfprov_notifications:

Unsolicited notifications
~~~~~~~~~~~~~~~~~~~~~~~~~

One of the following ``#XNRFPROV`` notifications is emitted when the provisioning session completes, an event occurs, or host action is required:

::

   #XNRFPROV: <status>

* The ``<status>`` parameter is an integer.
  It can have the following values:

  * ``0`` - Provisioning successful, or no pending commands on the server.
  * ``1`` - Host action required: deactivate LTE.
    The host must take the modem offline (for example ``AT+CFUN=4``) so that credentials can be written safely.
    The provisioning client waits until the modem reports offline functional mode before continuing or times out after ``CONFIG_NRF_PROVISIONING_MODEM_STATE_WAIT_TIMEOUT_SECONDS`` seconds.
  * ``2`` - Host action required: activate LTE.
    The host must bring the modem back online (for example ``AT+CFUN=1``) to reconnect to the provisioning server.
    The provisioning client waits until the modem reports LTE registration before continuing or times out after ``CONFIG_NRF_PROVISIONING_MODEM_STATE_WAIT_TIMEOUT_SECONDS`` seconds.
  * ``-1`` - Provisioning failed.
    The host can retry provisioning by triggering the ``#XNRFPROV`` command again.
  * ``-2`` - Device not claimed on nRF Cloud.
    The device must be claimed using its attestation token.
    Refer to the `nRF Cloud claiming Devices`_ documentation for more information.
  * ``-3`` - Wrong root CA certificate.
    Provision the correct nRF Cloud root CA certificate.
  * ``-4`` - No valid datetime reference.
    The modem must have a valid time before provisioning can proceed.
  * ``-5`` - Too many commands received from the server.
    Increase ``CONFIG_NRF_PROVISIONING_CBOR_RECORDS``.
  * ``-6`` - Fatal error; the provisioning client encountered an irrecoverable error.

Example
~~~~~~~

The provisioning client requests the host to manage the LTE connection during the session.
The host must respond to the ``#XNRFPROV: 1`` and ``#XNRFPROV: 2`` notifications accordingly.

::

   AT#XNRFPROV
   OK

   #XNRFPROV: 1

   AT+CFUN=4
   OK

   #XNRFPROV: 2

   AT+CFUN=1
   OK

   #XNRFPROV: 0

Test command
------------

The test command returns the command syntax.

Syntax
~~~~~~

::

   AT#XNRFPROV=?

Response syntax
~~~~~~~~~~~~~~~

::

   #XNRFPROV
