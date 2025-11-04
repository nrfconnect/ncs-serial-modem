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
     * Rename ``CONFIG_SLM_CMUX_TX_BUFFER_SIZE`` to ``CONFIG_SM_URC_BUFFER_SIZE``

   * Code patches:

     * Renamed the file name from ``slm_`` to ``sm_`` and ``modem_slm`` to ``sm_host``.
     * Functions and other symbols in the code have been renamed accordingly making automatic patching to likely fail.

   * Default AT command terminator changed from ``\r\n`` (``CONFIG_SM_CR_LF_TERMINATION`` and ``CONFIG_SM_HOST_CR_LF_TERMINATION``) to ``\r`` (``CONFIG_SM_CR_TERMINATION`` and ``CONFIG_SM_HOST_CR_TERMINATION``).

   * Rename the following AT commands:

     * ``AT#GPS`` to ``AT#GNSS``
     * ``AT#XGPSDEL`` command has been renamed to ``AT#XGNSSDEL``

   * Removed the ``AT#XPOLL`` command.
     Use ``AT#XAPOLL`` instead.

   * PPP connection must be requested using the ``AT#XPPP=1`` command to get it started when the modem is put into online mode using the ``AT+CFUN=1`` command.
     The ``AT#XPPP=1`` command can be run before or after the ``AT+CFUN=1`` command.
     So PPP connection is not started automatically anymore when the ``AT+CFUN=1`` command is run.
     After the ``AT#XPPP=1`` command is run, the PPP connection is started when the ``AT+CFUN=1`` command is run and stopped when network is lost (for example, with ``AT+CFUN=4``, ``AT+CFUN=0`` or bad reception).
     When the network is regained (for example, with ``AT+CFUN=1``), the PPP connection is started again automatically.
     To permanently stop the PPP connection, either the remote peer should disconnect the PPP using LCP termination or the ``AT#XPPP=0`` command should be run.
     If PPP is terminated using LCP termination or the ``AT#XPPP=0`` command, the PPP connection can be started again with the ``AT#XPPP=1`` command.

DTR and RI GPIOs replace Power and Indicate pins
------------------------------------------------

The |SM| application uses DTR (Data Terminal Ready) and RI (Ring Indicator) pins to manage the UART power state instead of Power and Indicate pins used in the |NCS| SLM.

* Removed:

  * The Power pin, which was active low input, expected a short pulse and was configured with ``CONFIG_SLM_POWER_PIN``.
  * The Indicate pin, which was active low output, sent a pulse configured with ``CONFIG_SLM_INDICATE_TIME`` and was configured with ``CONFIG_SLM_INDICATE_PIN``.

* Added:

  * DTR pin, which is a level based input, that is configured in the devicetree with the ``dtr-gpios`` property.
  * RI pin, which is a pulse based output, that is configured in the devicetree with the ``ri-gpios`` property.

See :ref:`sm_dtr_ri` for more information on how DTR and RI pins work in the |SM| application.
See :ref:`sm_as_zephyr_modem` for information on how to configure DTR and RI pins when using the |SM| application as a Zephyr modem.

Socket AT commands updated to handle-based API
----------------------------------------------

The socket AT commands have been updated to use a handle-based approach instead of socket selection.
This provides more flexibility and clearer socket management by directly referencing socket handles in all operations.

   * **Removed commands:**

     * ``AT#XSOCKETSELECT`` - Socket selection is no longer needed. Each command now directly specifies the socket handle.

   * **Updated socket creation:**

     * ``AT#XSOCKET`` - No longer supports closing sockets (``op=0`` removed). Only creates sockets and returns a handle.
     * ``AT#XSSOCKET`` - No longer supports closing sockets (``op=0`` removed). Only creates secure sockets and returns a handle.

   * **New socket closing:**

     * ``AT#XCLOSE`` - New command to close individual sockets or all sockets at once.
     * Syntax: ``AT#XCLOSE[=<handle>]`` (handle is optional - omit to close all sockets)

   * **AT#XSEND command parameter changes:**

    Added ``<handle>`` and ``<mode>`` parameters to the ``AT#XSEND`` command. Changed parameter order.

     * Old syntax: ``AT#XSEND[=<data>][,<flags>]``
     * New syntax: ``AT#XSEND=<handle>,<mode>,<flags>[,<data>]``

   * **AT#XSENDTO parameter changes:**

    Added ``<handle>`` and ``<mode>`` parameters to the ``AT#XSENDTO`` command. Changed parameter order.

    * Old syntax: ``AT#XSENDTO=<url>,<port>[,<data>][,<flags>]``
    * New syntax: ``AT#XSENDTO=<handle>,<mode>,<flags>,<url>,<port>[,<data>]``

   * **AT#XRECV parameter changes:**

    Added ``<handle>``, ``<mode>`` parameters and optional ``<data_len>`` parameter to the ``AT#XRECV`` command. Changed parameter order.

    * Old syntax: ``AT#XRECV=<timeout>[,<flags>]``
    * New syntax: ``AT#XRECV=<handle>,<mode>,<flags>,<timeout>[,<data_len>]``

   * **AT#XRECVFROM parameter changes:**

    Added ``<handle>``, ``<mode>`` parameters and optional ``<data_len>`` parameter to the ``AT#XRECVFROM`` command. Changed parameter order.

    * Old syntax: ``AT#XRECVFROM=<timeout>[,<flags>]``
    * New syntax: ``AT#XRECVFROM=<handle>,<mode>,<flags>,<timeout>[,<data_len>]``

   * **New mode parameter for send/receive commands:**

    The ``<mode>`` parameter provides different data handling modes:

    **For send commands (AT#XSEND, AT#XSENDTO):**
     * ``0`` - String mode. Data provided directly as string parameter.
     * ``1`` - Hex string mode. Data provided as hexadecimal string representation.
     * ``2`` - Data mode. Enter data input mode for binary data.

    **For receive commands (AT#XRECV, AT#XRECVFROM):**
     * ``0`` - Binary mode. Data received as binary data.
     * ``1`` - Hex string mode. Data received as hexadecimal string representation.

   * **All socket operations now require handle parameter:**

     * ``AT#XSOCKETOPT=<handle>,<op>,<name>[,<value>]`` (handle parameter added)
     * ``AT#XSSOCKETOPT=<handle>,<op>,<name>[,<value>]`` (handle parameter added)
     * ``AT#XBIND=<handle>,<port>`` (handle parameter added)
     * ``AT#XCONNECT=<handle>,<url>,<port>`` (handle parameter added)
     * ``AT#XLISTEN=<handle>`` (handle parameter added)
     * ``AT#XACCEPT=<handle>,<timeout>`` (handle parameter added)
     * ``AT#XSEND=<handle>,<mode>,<flags>[,<data>]`` (handle parameter added, mode parameter added, parameter order changed)
     * ``AT#XRECV=<handle>,<mode>,<flags>,<timeout>[,<data_len>]`` (handle parameter added, mode parameter added, optional data_len parameter added, parameter order changed)
     * ``AT#XSENDTO=<handle>,<mode>,<flags>,<url>,<port>[,<data>]`` (handle parameter added, mode parameter added, parameter order changed)
     * ``AT#XRECVFROM=<handle>,<mode>,<flags>,<timeout>[,<data_len>]`` (handle parameter added, mode parameter added, optional data_len parameter added, parameter order changed)

   * **Response format changes:**

     * ``AT#XSOCKETOPT`` - Response to get options now includes socket handle: ``#XSOCKETOPT: <handle>,<value>`` (previously just ``#XSOCKETOPT: <value>``)
     * ``AT#XSSOCKETOPT`` - Response to get options now includes socket handle: ``#XSSOCKETOPT: <handle>,<value>`` (previously just ``#XSSOCKETOPT: <value>``)
     * ``AT#XCONNECT`` - Response now includes socket handle: ``#XCONNECT: <handle>,<status>`` (previously just ``#XCONNECT: <status>``)
     * ``AT#XSEND`` - Response now includes socket handle: ``#XSEND: <handle>,<size>`` (previously just ``#XSEND: <size>``)
     * ``AT#XRECV`` - Response now includes socket handle and mode: ``#XRECV: <handle>,<mode>,<size>`` (previously just ``#XRECV: <size>``)
     * ``AT#XSENDTO`` - Response now includes socket handle: ``#XSENDTO: <handle>,<size>`` (previously just ``#XSENDTO: <size>``)
     * ``AT#XRECVFROM`` - Response now includes socket handle and mode: ``#XRECVFROM: <handle>,<mode>,<size>,"<ip_addr>",<port>`` (previously just ``#XRECVFROM: <size>,"<ip_addr>",<port>``)

   * **Migration example:**

     **Old approach (NCS SLM):**

     .. code-block::

        AT#XSOCKET=1,1,0          // Open socket, returns handle 1
        AT#XCONNECT="server",80   // Connect socket handle 1
        AT#XSEND="data"           // Send on socket handle 1
        AT#XSOCKET=1,1,0          // Open socket, returns handle 2
        AT#XCONNECT="server",80   // Connect socket handle 2
        AT#XRECV=10               // Receive data from socket handle 2 with 10s timeout, no flags
        AT#XSOCKETSELECT=1        // Select socket handle 1
        AT#XSOCKET=0              // Close selected socket handle 1

     **New approach (Serial Modem add-on):**

     .. code-block::

        AT#XSOCKET=1,1,0          // Open socket, returns handle 1
        AT#XCONNECT=1,"server",80 // Connect socket handle 1
        AT#XSEND=1,0,0,"data"     // Send on socket handle 1
        AT#XSOCKET=1,1,0          // Open socket, returns handle 2
        AT#XCONNECT=2,"server",80 // Connect socket handle 2
        AT#XRECV=2,0,0,10         // Receive data from socket handle 2 with mode 0, no flags, 10s timeout
        AT#XCLOSE=1               // Close socket handle 1

Removed features
****************

This section lists features that have been removed from the |addon| compared to the |NCS| v3.1.x `Serial LTE modem (SLM) <Serial LTE modem_>`_.
If you need any of those features with this |addon|, please contact customer support and describe your use case.
Removed features:

   * Board support for the following chipsets: ``nrf9161dk/nrf9161/ns``, ``nrf9160dk/nrf9160/ns``, ``thingy91/nrf9160/ns`` and ``nrf9131ek/nrf9131/ns``.
     * Use ``nrf9151dk/nrf9151/ns`` instead.
   * Native TLS support including ``overlay-native_tls.conf``.
     ``<sec_tag>`` parameter has been removed from ``XTCPSVR`` and ``#XUDPSVR`` commands.
