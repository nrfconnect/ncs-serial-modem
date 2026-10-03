|SM| v1.0.0 Release Notes
#########################

* This is the first official release of the Serial Modem.
* Uses |NCS| v3.2.2 release.
* Added DFU AT commands.
* Added runtime setting for UART speed (``AT+IPR``).
* Added modem trace support for a CMUX channel.
* PPP starts on AT UART when used without CMUX.
  Option to use secondary UART removed.
* Changed UART pins for external MCU use case.
* Marked the following as experimental:

  * Thingy:91 X board target.
  * nRF Cloud AT commands
  * MQTT AT commands
  * LwM2M carrier lib
  * Memfault
  * :ref:`lib_sm_at_client`
