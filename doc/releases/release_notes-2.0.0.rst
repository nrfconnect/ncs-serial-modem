|SM| v2.0.0 (working draft)
###########################

* Uses |NCS| v3.4.1 release.
* Added a ``VERSION`` file, which changes how version information is reported (``AT#XSMVER``).
* Added HTTP client.
* Added CoAP client.
* Updated nRF Cloud AT commands to use CoAP instead of MQTT.
* Added nRF Cloud provisioning.
* Added support for nRF Cloud observability (Memfault) through ``AT#XNRFCLOUDOBS*`` commands.
* Added TCP server support with ``AT#XLISTEN`` and ``AT#XACCEPT``.
* Added nRF91M1 build configuration.
* Added shared UART for application log and modem trace; ``AT#XLOG`` and ``AT#XTRACE`` select which output to use.
* More standard and dynamic handling of CMUX channels with ``AT+CMUX`` and ``AT+CGDATA``.
* Added ``AT#XCMUXURC`` for configuring the URC channel.
* Major refactoring of the internal AT host implementation.
* Added updatable bootloaders, included by default.
* Added modem driver (``nordic,nrf91-sm-v2``) for Zephyr host utilizing ``AT+CMUX`` and ``AT+CGDATA``.
* UART is the default application log output instead of RTT.
* Memory partitions moved from Partition Manager to devicetree (Partition Manager is deprecated in NCS).
* Added support for printing heap statistics to the log with ``AT#XDBGSTATSMEM``.
* Added support for using AT commands and receiving URCs from the Zephyr host; DFU support was also added to the SM PPP shell sample.
* Added migration notes from the v1.0.0 release.
