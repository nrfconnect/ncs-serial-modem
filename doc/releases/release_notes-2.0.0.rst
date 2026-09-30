Release notes for |SM| v2.0.0
#############################

This page tracks changes and updates as compared to the latest official release. For more information refer to the following section.

Changelog
*********

This release is based on the |NCS| v3.4.1 release.

* Added:

  * The ``VERSION`` file, which changes how version information is reported (``AT#XSMVER``).
  * HTTP client.
  * CoAP client.
  * nRF Cloud provisioning.
  * nRF Cloud FOTA.
  * nRF Cloud observability (Memfault) through ``AT#XNRFCLOUDOBS*`` commands.
  * TCP server support with ``AT#XLISTEN`` and ``AT#XACCEPT``.
  * Shared UART for application log and modem trace; ``AT#XLOG`` and ``AT#XTRACE`` select which output to use.
  * More standard and dynamic handling of CMUX channels with ``AT+CMUX`` and ``AT+CGDATA``.
  * ``AT#XCMUXURC`` for configuring the URC channel.
  * Updatable bootloaders, included by default.
  * Modem driver (``nordic,nrf91-sm-v2``) for Zephyr host using the ``AT+CMUX`` and ``AT+CGDATA`` commands.
  * Support for using AT commands and receiving URCs from the Zephyr host.
    DFU support was also added to the SM PPP shell sample.
  * nRF91M1 build configuration.
  * Support for printing heap statistics to the log with ``AT#XDBGSTATSMEM``.
  * Migration notes from the v1.0.0 release.

* Updated:

  * nRF Cloud AT commands to use CoAP instead of MQTT.
  * UART to be the default application log output instead of RTT.
  * Major refactoring of the internal AT host implementation.
  * Memory partitions from Partition Manager to devicetree (Partition Manager is deprecated in |NCS|).
