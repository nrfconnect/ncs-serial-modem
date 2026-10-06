Release notes for |SM| v2.0.0
#############################

This page tracks changes and updates as compared to the latest official release (v1.0.1). For more information refer to the following section.

Changelog
*********

This release is based on the |NCS| v3.4.1 release.
It is also based on the `Memfault`_ SDK v1.45.0 release, which is updated from the v1.40.1 in the |NCS| v3.4.1 release.

* Added:

  * Updatable bootloaders, included by default.
  * HTTP client through ``AT#XHTTPC*`` commands.
  * CoAP client through ``AT#XCOAPC*`` commands.
  * nRF Cloud provisioning through ``AT#XNRFPROV`` command.
  * nRF Cloud FOTA through ``AT#XNRFCLOUDFOTA`` command.
  * nRF Cloud observability (Memfault) through ``AT#XNRFCLOUDOBS*`` commands.
  * TCP server support through ``AT#XLISTEN`` and ``AT#XACCEPT`` commands.
  * More standard and dynamic handling of CMUX channels with ``AT+CMUX`` and ``AT+CGDATA``.
  * Modem driver (``nordic,nrf91-sm-v2``) for Zephyr host using the ``AT+CMUX`` and ``AT+CGDATA`` commands.
  * ``AT#XCMUXURC`` for configuring the URC channel.
  * Support for using AT commands and receiving URCs from the Zephyr host.
    DFU support was also added to the SM PPP shell sample.
  * Shared UART for application log and modem trace.
    ``AT#XLOG`` and ``AT#XTRACE`` select which output to use.
  * Redacting of sensitive data from |SM| logs.
    See ``AT#XLOG=1`` vs. ``AT#XLOG=2``.
  * nRF91M1 build configuration.
  * The ``VERSION`` file, which changes how version information is reported (``AT#XSMVER``).
  * Support for printing heap statistics to the log with ``AT#XDBGSTATSMEM``.
  * nRF91M1 AT commands documentation.

* Updated:

  * nRF Cloud AT commands to use CoAP instead of MQTT.
  * UART to be the default application log output instead of RTT.
  * The default |SM| log level from INF to DBG.
  * Major refactoring of the internal AT host implementation.
  * Memory partitions from Partition Manager to devicetree (Partition Manager is deprecated in |NCS|).
