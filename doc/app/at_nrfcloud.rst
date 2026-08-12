nRF Cloud AT commands
*********************

.. contents::
   :local:
   :depth: 2

.. note::

   These AT commands are `Experimental <Software maturity levels_>`_.

The page describes nRF Cloud-related AT commands.

.. _SM_AT_NRFCLOUD:

nRF Cloud access
================

The ``#XNRFCLOUD`` command controls the access to the nRF Cloud service.

.. note::
   To use ``#XNRFCLOUD``, the following preconditions apply:

   * You must first onboard the device to nRF Cloud, using the device-specific UUID as the device ID.
     See `nRF Cloud Preconnect onboarding`_ for more information.
   * The :ref:`CONFIG_SM_NRF_CLOUD <CONFIG_SM_NRF_CLOUD>` Kconfig option must be enabled.
   * The device must have access to nRF Cloud through the LTE network.

Set command
-----------

The set command allows you to access the nRF Cloud service.

.. note::

   The ``#XNRFCLOUD`` command uses default PDN connection with ID ``0``.
   Raw sockets must not use the PDN connection at the same time.
   See :ref:`SM_AT_SOCKET_RAW_SOCKET_LIMITATION` for more information.

Syntax
~~~~~~

::

   AT#XNRFCLOUD=<op>[,<send_location>]

* The ``<op>`` parameter can have the following integer values:

  * ``0`` - Disconnect from the nRF Cloud service.
  * ``1`` - Connect to the nRF Cloud service.
  * ``2`` - Send a message in the JSON format to the nRF Cloud service.

  When ``<op>`` is ``2``, |SM| enters :ref:`sm_data_mode`.

* The ``<send_location>`` parameter is used only when the value of ``<op>`` is ``1``.
  It can have the following integer values:

  * ``0`` - The device location is not sent to nRF Cloud.
    This is the default behavior if the parameter is omitted.
  * ``1`` - The device location is sent to nRF Cloud.

  .. note::
     The location is sent to the nRF Cloud whenever a fix is produced by the GNSS module.
     You must use the :ref:`#XGNSS <SM_AT_GNSS>` AT command to start GNSS either in single-fix or periodic navigation mode.
     The interval between fixes must be at least 5 seconds.

Unsolicited notification
~~~~~~~~~~~~~~~~~~~~~~~~

::

   #XNRFCLOUD: <ready>,<send_location>

* The ``<ready>`` parameter indicates whether the connection to nRF Cloud is established or not.
* The ``<send_location>`` parameter indicates whether the device location will be sent to nRF Cloud or not.

Example
~~~~~~~

::

  // Connect to nRF Cloud without sending location.
  AT#XNRFCLOUD=1

  OK

  #XNRFCLOUD: 1,0
  // Send a message to nRF Cloud.
  AT#XNRFCLOUD=2

  OK
  {"msg":"Hello, nRF Cloud"}+++

  #XDATAMODE: 0
  // Disconnect from nRF Cloud.
  AT#XNRFCLOUD=0

  OK

  #XNRFCLOUD: 0,0
  // Connect to nRF Cloud and send location.
  AT#XNRFCLOUD=1,1

  OK

  #XNRFCLOUD: 1,1
  AT#XNRFCLOUD=0

  #XNRFCLOUD: 0,1

  OK

Read command
------------

The read command checks whether the connection to nRF Cloud is established or not.

Syntax
~~~~~~

::

   AT#XNRFCLOUD?

Response syntax
~~~~~~~~~~~~~~~

::

   #XNRFCLOUD: <ready>,<send_location>,<sec_tag>,<device_id>

* The ``<ready>`` parameter indicates whether the connection to nRF Cloud is established or not.
* The ``<send_location>`` parameter indicates whether the device location will be sent to nRF Cloud or not.
* The ``<sec_tag>`` parameter indicates the ``sec_tag`` used for accessing nRF Cloud.
* The ``<device_id>`` parameter indicates the device ID used for accessing nRF Cloud.

Example
~~~~~~~

::

  AT#XNRFCLOUD?

  #XNRFCLOUD: 1,0,16842753,"50503041-3633-4261-803d-1e2b8f70111a"

  OK

Test command
------------

The test command tests the existence of the command and provides information about the type of its subparameters.

Syntax
~~~~~~

::

   AT#XNRFCLOUD=?

Example
~~~~~~~

::

  AT#XNRFCLOUD=?

  #XNRFCLOUD: (0,1,2),<send_location>

  OK

.. _SM_AT_NRFCLOUDPOS:

nRF Cloud location
==================

The ``#XNRFCLOUDPOS`` command sends a request to nRF Cloud to determine the device's location.
The request uses information from the cellular network, Wi-Fi® access points, or both.

.. note::
   To use ``#XNRFCLOUDPOS``, the following preconditions apply:

   * The device must be connected to nRF Cloud using :ref:`#XNRFCLOUD <SM_AT_NRFCLOUD>`.
   * The :ref:`CONFIG_SM_NRF_CLOUD_LOCATION <CONFIG_SM_NRF_CLOUD_LOCATION>` Kconfig option must be enabled.

Set command
-----------

The set command allows sending a location request to nRF Cloud.

Syntax
~~~~~~

::

   AT#XNRFCLOUDPOS=<cell_count>,<wifi_pos>[,<MAC 1>[,<RSSI 1>],<MAC 2>[,<RSSI 2>][,<MAC 3>[...]]]

* The ``<cell_count>`` parameter indicates the number of cells to include in the location request.
  The value range is ``0`` to ``15``.
  For cellular positioning, a recommended value is ``4``.
  ``0`` means that no cellular network information will be included in the location request.
  The |SM| uses the ``AT%NCELLMEAS`` command to retrieve the cellular network information, and depending on the value of ``<cell_count>``, the command might be executed multiple times.

  .. note::

     Since the |SM| uses the ``AT%NCELLMEAS`` command internally, the host must not use the ``AT%NCELLMEAS`` command during ``#XNRFCLOUDPOS`` command execution.
     You may still use ``AT%NCELLMEAS`` command before or after ``#XNRFCLOUDPOS`` command execution for your own purposes.
     You will also see ``%NCELLMEAS`` notifications, which you can ignore, during the ``#XNRFCLOUDPOS`` command execution.

* The ``<wifi_pos>`` parameter can have the following integer values:

  * ``0`` - Do not include Wi-Fi access point information in the location request.
  * ``1`` - Use Wi-Fi access point information.
    The access points must be given as additional parameters to the command.
    The minimum number of access points to provide is two (``NRF_CLOUD_LOCATION_WIFI_AP_CNT_MIN``), and the maximum is limited by the maximum size of the AT command, which is 8190 bytes, including the terminator character.

* The ``<MAC x>`` parameter is a string.
  It indicates the MAC address of a Wi-Fi access point and must be formatted as ``%02x:%02x:%02x:%02x:%02x:%02x`` (``WIFI_MAC_ADDR_TEMPLATE``).

* The ``<RSSI x>`` parameter is an optional integer.
  It indicates the signal strength of a Wi-Fi access point in dBm, between ``-128`` and ``0``.
  If provided, it must follow the MAC address parameter of the access point.
  Providing the RSSI parameters helps improve the accuracy of the Wi-Fi location.

Unsolicited notification
~~~~~~~~~~~~~~~~~~~~~~~~

::

   #XNRFCLOUDPOS: <status>[,<type>,<latitude>,<longitude>,<uncertainty>]

* The ``<status>`` parameter indicates the status of the location request.

  * ``0`` - Successful request. Other parameters are also present.
  * ``-1`` - Location request failed.
  * ``<positive integer>`` - Requesting location from the cloud failed with cloud error as defined in :c:enum:`nrf_cloud_error` values.

This is emitted when a successful response to a sent location request is received.

* The ``<type>`` parameter indicates the service used to fulfill the location request.

  * ``0`` (:c:enumerator:`LOCATION_TYPE_SINGLE_CELL`) - Single-cell cellular location.
  * ``1`` (:c:enumerator:`LOCATION_TYPE_MULTI_CELL`) - Multi-cell cellular location.
  * ``2`` (:c:enumerator:`LOCATION_TYPE_WIFI`) - Wi-Fi location.

* The ``<latitude>`` parameter represents the latitude in degrees.
* The ``<longitude>`` parameter represents the longitude in degrees.
* The ``<uncertainty>`` parameter represents the radius of the uncertainty circle around the location in meters, also known as Horizontal Positioning Error (HPE).

Example
~~~~~~~

::

  AT%XSYSTEMMODE=1,0,0,0

  OK
  AT+CFUN=1

  OK
  AT#XNRFCLOUD=1

  OK

  #XNRFCLOUD: 1,0
  AT#XNRFCLOUDPOS=1,0

  OK

  #XNRFCLOUDPOS: 0,0,35.455833,139.626111,1094

  AT#XNRFCLOUDPOS=5,0

  OK

  %NCELLMEAS: 0,"0199F10A","44020","107E",65535,3750,5,49,27,107504,3750,251,33,4,0,475,107,26,14,25,475,58,26,17,25,475,277,24,9,25,475,51,18,1,25

  %NCELLMEAS: 0,"01234567","44020","0340",50,175456,3400,34,5,24,1775066,1,0,"00143FAE","44020","0140",65535,0,6200,47,40,14,1775066,0,0

  %NCELLMEAS: 0,"00987654","44020","0240",50,1754746,5500,44,4,4,1463457,1,0,"002F4344","44020","0140",65535,0,6200,47,40,14,1775066,0,0,"001C0502","44013","5A00",65535,0,6400,130,29,18,1775124,0,0,"00136107","44013","5A00",65535,0,3600,202,26,13,234533,0,0

  #XNRFCLOUDPOS: 0,1,35.455833,139.626111,1094
  AT#XNRFCLOUDPOS=0,1,"40:9b:cd:c1:5a:40","00:90:fe:eb:4f:42"

  OK

  #XNRFCLOUDPOS: 0,2,35.457335,139.624443,60
  AT#XNRFCLOUDPOS=0,1,"40:9b:cd:c1:5a:40",-40,"00:90:fe:eb:4f:42",-69

  OK

  #XNRFCLOUDPOS: 0,2,35.457346,139.624449,20

Read command
------------

The read command is not supported.

Test command
------------

The test command is not supported.

.. _SM_AT_NRFCLOUDOBS:

nRF Cloud observability
=======================

The ``#XNRFCLOUDOBS*`` commands control the Memfault data that the device collects (metrics, events, logs, and, in builds that include it, a coredump) and its upload to nRF Cloud over the CoAP transport.

.. note::
   To use the ``#XNRFCLOUDOBS*`` commands, the following preconditions apply:

   * The commands that access the network (``#XNRFCLOUDOBSUPLOAD`` and ``#XNRFCLOUDOBSFORWARD``) require a connection to nRF Cloud.
     See ``AT#XNRFCLOUD``.
   * ``#XNRFCLOUDOBSDEVINFO``, ``#XNRFCLOUDOBSCRASH`` and ``#XNRFCLOUDOBSEXPORT`` additionally require the :ref:`CONFIG_SM_NRF_CLOUD_OBSERVABILITY_DEBUG <CONFIG_SM_NRF_CLOUD_OBSERVABILITY_DEBUG>` Kconfig option, which is disabled by default.

   Upload is host-driven.
   The automatic upload starts disabled, and the host enables it with ``AT#XNRFCLOUDOBSAUTO=1`` or uploads on demand with ``AT#XNRFCLOUDOBSUPLOAD``.

The ``<project_key>`` parameter, which several of the commands accept, is a string.
It is a 32-character Memfault project key.
When it is present and not empty, it overrides the server-side project-key routing, sending the data to the specified Memfault project.
For more information about Memfault project keys, see `Memfault Project Keys`_.
Find your project key in `Memfault Project Settings`_.

.. _SM_AT_NRFCLOUDOBSAUTO:

Automatic upload #XNRFCLOUDOBSAUTO
==================================

The ``#XNRFCLOUDOBSAUTO`` command configures the automatic upload of the buffered observability data.

The configuration is persistent, so it survives a reboot.

Set command
-----------

The set command enables or disables the automatic upload and configures its interval.

Syntax
~~~~~~

::

   AT#XNRFCLOUDOBSAUTO=<enable>[,<interval_seconds>][,<project_key>]

* The ``<enable>`` parameter can have the following integer values:

  * ``0`` - Disable the automatic upload.
  * ``1`` - Enable the automatic upload.

* The ``<interval_seconds>`` parameter is an integer from ``60`` to ``86400``.
  It is the interval between two uploads.
  When it is omitted, the stored interval is kept.
  Its initial value is set by the :ref:`CONFIG_SM_NRF_CLOUD_OBSERVABILITY_AUTO_INTERVAL_SECONDS <CONFIG_SM_NRF_CLOUD_OBSERVABILITY_AUTO_INTERVAL_SECONDS>` Kconfig option.

* The ``<project_key>`` parameter is a string.
  When it is omitted, the stored project key is kept, and an empty string clears it.

The first upload runs when the interval expires, not when the automatic upload is enabled.
An upload that falls while there is no connection to nRF Cloud is skipped, and the next one is scheduled as usual.

The automatic upload is silent: it sends no unsolicited notification.

Response
~~~~~~~~

The command returns ``OK``, also when the configuration could not be stored, in which case only its persistence is lost.

Read command
------------

The read command returns the configuration of the automatic upload.

Syntax
~~~~~~

::

   AT#XNRFCLOUDOBSAUTO?

Response
~~~~~~~~

::

   #XNRFCLOUDOBSAUTO: <enable>,<interval_seconds>,<project_key>

Example
~~~~~~~

::

  AT#XNRFCLOUDOBSAUTO=1,600

  OK
  AT#XNRFCLOUDOBSAUTO?

  #XNRFCLOUDOBSAUTO: 1,600,""

  OK

Test command
------------

The test command returns the supported syntax.

Syntax
~~~~~~

::

   AT#XNRFCLOUDOBSAUTO=?

Response
~~~~~~~~

::

   #XNRFCLOUDOBSAUTO: (0,1),(60-86400),<project_key>

.. _SM_AT_NRFCLOUDOBSUPLOAD:

On-demand upload #XNRFCLOUDOBSUPLOAD
====================================

The ``#XNRFCLOUDOBSUPLOAD`` command uploads the buffered observability data to nRF Cloud.

The captured logs are collected before the upload, so that they are included.

Set command
-----------

The set command uploads the buffered data.

Syntax
~~~~~~

::

   AT#XNRFCLOUDOBSUPLOAD[=<project_key>]

The command returns ``OK`` immediately and the upload runs asynchronously.
When it completes, an unsolicited notification is sent.

Unsolicited notification
~~~~~~~~~~~~~~~~~~~~~~~~

::

   #XNRFCLOUDOBSUPLOAD: <result>[,<bytes>]

* The ``<result>`` parameter is an integer.

  * ``0`` - Success.
    The ``<bytes>`` parameter follows and indicates the number of bytes uploaded, which is ``0`` when there was nothing buffered.
  * ``-1`` - Failure.
    The error code is shown in the log.

Example
~~~~~~~

::

  AT#XNRFCLOUDOBSUPLOAD

  OK

  #XNRFCLOUDOBSUPLOAD: 0,108

Read command
------------

The read command is not supported.

Test command
------------

The test command returns the supported syntax.

Syntax
~~~~~~

::

   AT#XNRFCLOUDOBSUPLOAD=?

Response
~~~~~~~~

::

   #XNRFCLOUDOBSUPLOAD: <project_key>

.. _SM_AT_NRFCLOUDOBSHEARTBEAT:

Metrics heartbeat #XNRFCLOUDOBSHEARTBEAT
========================================

The ``#XNRFCLOUDOBSHEARTBEAT`` command collects and finalizes a metrics heartbeat.

The data is buffered on the device until it is uploaded, so the command does not require a connection to nRF Cloud.

The heartbeat carries the LTE metrics that the modem reports: the modem firmware version, the network operator, RSRP, SNR, the current band, and the transmitted and received kilobytes.
Values that the modem does not report, for example when it is deactivated, are left out of the heartbeat.

Set command
-----------

The set command collects a heartbeat.

Syntax
~~~~~~

::

   AT#XNRFCLOUDOBSHEARTBEAT

Example
~~~~~~~

::

  AT#XNRFCLOUDOBSHEARTBEAT

  OK

Read command
------------

The read command is not supported.

Test command
------------

The test command is not supported.

.. _SM_AT_NRFCLOUDOBSFORWARD:

Chunk forwarding #XNRFCLOUDOBSFORWARD
=====================================

The ``#XNRFCLOUDOBSFORWARD`` command forwards a Memfault chunk produced by the host to nRF Cloud, using the |SM| connection.

.. note::
   nRF Cloud attributes the chunk to the authenticated device, so the observability data of the host is reported under the device serial of the |SM| device by default.
   To report it under a device serial of the host instead, build the host firmware with ``MEMFAULT_EVENT_INCLUDE_DEVICE_SERIAL`` set to ``1`` in its :file:`memfault_platform_config.h`.
   Every chunk then carries the device serial of the host, which Memfault uses to attribute the data.

Set command
-----------

The set command forwards a chunk.

Syntax
~~~~~~

::

   AT#XNRFCLOUDOBSFORWARD=<base64_chunk>[,<project_key>]

* The ``<base64_chunk>`` parameter is a string.
  It is the base64-encoded Memfault chunk to forward.

The command returns ``OK`` immediately and the chunk is posted asynchronously.
When the post completes, an unsolicited notification is sent.

Unsolicited notification
~~~~~~~~~~~~~~~~~~~~~~~~

::

   #XNRFCLOUDOBSFORWARD: <result>

* The ``<result>`` parameter is an integer.

  * ``0`` - Success.
  * ``-1`` - Failure.
    The chunk is not buffered, so the host must send it again to retry.

Example
~~~~~~~

Forward a chunk of the host, sent to a specific Memfault project::

  AT#XNRFCLOUDOBSFORWARD="CAKnAgIDAQpqdGVzdHNlcmlhbA==","<project_key>"

  OK

  #XNRFCLOUDOBSFORWARD: 0

Read command
------------

The read command is not supported.

Test command
------------

The test command returns the supported syntax.

Syntax
~~~~~~

::

   AT#XNRFCLOUDOBSFORWARD=?

Response
~~~~~~~~

::

   #XNRFCLOUDOBSFORWARD: <base64_chunk>,<project_key>

.. _SM_AT_NRFCLOUDOBSDEVINFO:

Memfault device information #XNRFCLOUDOBSDEVINFO
================================================

The ``#XNRFCLOUDOBSDEVINFO`` command returns the Memfault device information, which identifies the device and the firmware in the Memfault project.

This command requires the :ref:`CONFIG_SM_NRF_CLOUD_OBSERVABILITY_DEBUG <CONFIG_SM_NRF_CLOUD_OBSERVABILITY_DEBUG>` Kconfig option.

Set command
-----------

The set command returns the device information.

Syntax
~~~~~~

::

   AT#XNRFCLOUDOBSDEVINFO

Response
~~~~~~~~

::

   #XNRFCLOUDOBSDEVINFO: <device_serial>,<software_type>,<software_version>,<hardware_version>

All four parameters are strings.

Example
~~~~~~~

::

  AT#XNRFCLOUDOBSDEVINFO

  #XNRFCLOUDOBSDEVINFO: "50344654-3037-409f-802d-2206917f23d2","serial_modem","3.4.0","nrf9151dk"

  OK

Read command
------------

The read command is not supported.

Test command
------------

The test command is not supported.

.. _SM_AT_NRFCLOUDOBSCRASH:

Forced crash #XNRFCLOUDOBSCRASH
===============================

The ``#XNRFCLOUDOBSCRASH`` command forces a crash, so that the coredump capture and upload can be tested.

This command requires the :ref:`CONFIG_SM_NRF_CLOUD_OBSERVABILITY_DEBUG <CONFIG_SM_NRF_CLOUD_OBSERVABILITY_DEBUG>` Kconfig option.

Set command
-----------

The set command crashes the application.

Syntax
~~~~~~

::

   AT#XNRFCLOUDOBSCRASH[=<type>]

* The ``<type>`` parameter is an integer from ``0`` to ``4``.
  It defaults to ``0``, an assertion failure.

The device crashes and no response is returned, unless the crash type is invalid.

Read command
------------

The read command is not supported.

Test command
------------

The test command returns the supported syntax.

Syntax
~~~~~~

::

   AT#XNRFCLOUDOBSCRASH=?

Response
~~~~~~~~

::

   #XNRFCLOUDOBSCRASH: <type>

.. _SM_AT_NRFCLOUDOBSEXPORT:

Chunk export #XNRFCLOUDOBSEXPORT
================================

The ``#XNRFCLOUDOBSEXPORT`` command prints the buffered Memfault chunks to the AT interface instead of uploading them, in the Memfault chunk export format, which the Memfault tooling can parse.

This command requires the :ref:`CONFIG_SM_NRF_CLOUD_OBSERVABILITY_DEBUG <CONFIG_SM_NRF_CLOUD_OBSERVABILITY_DEBUG>` Kconfig option.

.. note::
   The command consumes the chunks.
   They are no longer available for ``#XNRFCLOUDOBSUPLOAD`` and the automatic upload.

Set command
-----------

The set command prints the buffered chunks.

Syntax
~~~~~~

::

   AT#XNRFCLOUDOBSEXPORT

Response
~~~~~~~~

The buffered chunks are returned, one per line::

   MC:<base64_chunk>:

Example
~~~~~~~

::

  AT#XNRFCLOUDOBSHEARTBEAT

  OK
  AT#XNRFCLOUDOBSEXPORT

  MC:CAKnAgIDAQpqdGVzdHNlcmlhbA==:

  OK

Read command
------------

The read command is not supported.

Test command
------------

The test command is not supported.
