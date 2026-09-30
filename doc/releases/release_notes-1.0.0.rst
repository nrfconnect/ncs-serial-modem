Release notes for |SM| v1.0.0
#############################

This page tracks changes and updates as compared to the latest official release. For more information refer to the following section.

Changelog
*********

This is the first official release of the Serial Modem.
It is based on the |NCS| v3.2.2 release.

* Added:

  * DFU AT commands.
  * Runtime setting for UART speed (``AT+IPR``).
  * Modem trace support for a CMUX channel.

* Updated:

  * PPP starts on AT UART when used without CMUX.
    Option to use secondary UART removed.
  * UART pins for external MCU use case.
  * Marked the following as experimental:

    * Thingy:91 X board target.
    * nRF Cloud AT commands
    * MQTT AT commands
    * LwM2M carrier lib
    * Memfault
    * :ref:`lib_sm_at_client`
