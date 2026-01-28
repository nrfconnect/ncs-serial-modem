.. _migration_3.1.x_SM:

Migration notes
###############

.. contents::
   :local:
   :depth: 3

This migration note helps you to move from |NCS| v3.1.x `Serial LTE modem (SLM) <Serial LTE modem_>`_ to the |SM|.

There are several breaking changes between |SM| and |NCS| SLM, such as renaming, file movement, AT command changes, overlay file changes, and so on.
The following sections cover all the changes that must be taken into account.

Background
**********

The base of the |SM| repository is a copy of |NCS| SLM and related components from the |NCS| main branch commit ``437f372b37849fe215243f8de48847d578976c13``, which is in practice a bit after the |NCS| release v3.1.0.

The following |NCS| files were copied into this repository:

* :file:`applications/serial_lte_modem` to :file:`app`
* :file:`lib/modem_slm` to :file:`lib/sm_at_client`
* :file:`samples/cellular/slm_shell` to :file:`samples/sm_at_client_shell`
* :file:`include/modem/modm_slm.h` to :file:`include/sm_at_client.h`
* :file:`doc/nrf/libraries/modem/modem_slm.rst` to :file:`doc/lib/sm_at_client.rst`

Required changes
****************

The following changes are mandatory to make your application work in the same way as in previous releases.

This section gives instructions on how to migrate from the |NCS| v3.1.x `SLM <Serial LTE modem_>`_ to the |SM|:

* Rename the following Kconfig options:

  * ``CONFIG_SLM_*`` to ``CONFIG_SM_*``
  * ``CONFIG_MODEM_SLM_*`` to ``CONFIG_SM_AT_CLIENT_*``
  * ``CONFIG_SLM_CMUX_TX_BUFFER_SIZE`` to ``CONFIG_SM_URC_BUFFER_SIZE``

* Code patches:

  * Renamed the file names from ``slm_`` to ``sm_`` and ``modem_slm`` to ``sm_at_client``.
  * Functions and other symbols in the code have been renamed accordingly making automatic patching to likely fail.

* Changed the default AT command terminator from ``\r\n`` (``CONFIG_SM_CR_LF_TERMINATION`` and ``CONFIG_SM_AT_CLIENT_CR_LF_TERMINATION``) to ``\r`` (``CONFIG_SM_CR_TERMINATION`` and ``CONFIG_SM_AT_CLIENT_CR_TERMINATION``).

* Rename the following AT commands:

  * ``AT#XGPS`` to ``AT#XGNSS``
  * ``AT#XGPSDEL`` to ``AT#XGNSSDEL``
  * ``AT#XSLMVER`` to ``AT#XSMVER``

DTR and RI GPIOs replace Power and Indicate pins
------------------------------------------------

The |SM| application uses DTR (Data Terminal Ready) and RI (Ring Indicator) pins to manage the UART power state instead of the Power and Indicate pins used in the |NCS| SLM.

* Removed:

  * The Power pin, which was an active low input, expected a short pulse and was configured with ``CONFIG_SLM_POWER_PIN``.
  * The Indicate pin, which was active low output, sent a pulse configured with ``CONFIG_SLM_INDICATE_TIME`` and was configured with ``CONFIG_SLM_INDICATE_PIN``.

* Added:

  * DTR pin, which is a level-based input that is configured in the devicetree with the ``dtr-gpios`` property.
  * RI pin, which is a pulse-based output that is configured in the devicetree with the ``ri-gpios`` property.

See :ref:`uart_configuration` for more information on how DTR and RI pins work in the |SM| application.
See :ref:`sm_as_zephyr_modem` for information on how to configure DTR and RI pins when using the |SM| application as a Zephyr modem.

Socket AT command changes
-------------------------

The socket AT commands have been updated to use a handle-based approach instead of socket selection ``AT#XSOCKETSELECT``.
This provides more flexibility and clearer socket management by directly referencing socket handles in all operations.
There are also other changes to the socket AT commands to improve functionality and usability.
Especially the ``AT#XSEND``/``AT#XSENDTO`` and ``AT#XRECV``/``AT#XRECVFROM`` commands have been updated significantly.

The following is the list of changes:

* Added socket closing:

  * ``AT#XCLOSE`` - New command to close individual sockets or all sockets at once.
  * Syntax: ``AT#XCLOSE[=<handle>]`` (handle is optional - omit to close all sockets).

* Updated socket creation:

  * ``AT#XSOCKET`` - No longer supports closing sockets (``op=0`` removed).
    Only creates sockets and returns a handle.
  * ``AT#XSSOCKET`` - No longer supports closing sockets (``op=0`` removed).
    Only creates secure sockets and returns a handle.

* Removed commands:

  * ``AT#XSOCKETSELECT`` - Socket selection is no longer needed. Each command now directly specifies the socket handle.

* ``AT#XSEND`` command parameter changes:

  * Added ``<handle>``, ``<mode>`` parameters, and optional ``<data_len>`` parameter (for data mode) to the ``AT#XSEND`` command.
    Changed parameter order.

    * Old syntax - ``AT#XSEND[=<data>][,<flags>]``
    * New syntax for string and hex string modes - ``AT#XSEND=<handle>,<mode>,<flags>,<url>,<port>,<data>``
    * New syntax for data mode - ``AT#XSEND=<handle>,<mode>,<flags>,<url>,<port>[,<data_len>]``

  * Added result type to the ``#XSEND`` response.

    * Old response: ``#XSEND: <size>``
    * New response: ``#XSEND: <handle>,<result_type>,<size>``

  * A new ``#XSENDNTF`` notification will be sent when the network acknowledges the send operation.
    This notification is requested with the ``<flags>`` parameter in the ``AT#XSEND`` or ``AT#XSENDTO`` commands.

    * Syntax: ``#XSENDNTF: <handle>,<status>,<size>``

* ``AT#XSENDTO`` parameter changes:

  * Added ``<handle>``, ``<mode>`` parameters, and optional ``<data_len>`` parameter (for data mode) to the ``AT#XSENDTO`` command.
    Changed parameter order.

    * Old syntax: ``AT#XSENDTO=<url>,<port>[,<data>][,<flags>]``
    * New syntax for string and hex string modes: ``AT#XSENDTO=<handle>,<mode>,<flags>,<url>,<port>,<data>``
    * New syntax for data mode: ``AT#XSENDTO=<handle>,<mode>,<flags>,<url>,<port>[,<data_len>]``

* ``AT#XRECV`` parameter changes:

  * Added ``<handle>``, ``<mode>`` parameters, and optional ``<data_len>`` parameter to the ``AT#XRECV`` command.
    Changed parameter order.

    * Old syntax: ``AT#XRECV=<timeout>[,<flags>]``
    * New syntax: ``AT#XRECV=<handle>,<mode>,<flags>,<timeout>[,<data_len>]``

* ``AT#XRECVFROM`` parameter changes:

  * Added ``<handle>``, ``<mode>`` parameters, and optional ``<data_len>`` parameter to the ``AT#XRECVFROM`` command.
    Changed parameter order.

    * Old syntax: ``AT#XRECVFROM=<timeout>[,<flags>]``
    * New syntax: ``AT#XRECVFROM=<handle>,<mode>,<flags>,<timeout>[,<data_len>]``

  * Added the new ``<mode>`` parameter for send or receive commands:

    * For send commands (AT#XSEND, AT#XSENDTO):

      * ``0`` - String mode.
        Data provided directly as a string parameter.
      * ``1`` - Hex string mode.
        Data provided as a hexadecimal string representation.
      * ``2`` - Data mode.
        Enter data input mode for binary data.

    * For receive commands (AT#XRECV, AT#XRECVFROM):

      * ``0`` - Binary mode.
        Data received as binary data.
      * ``1`` - Hex string mode.
        Data received as a hexadecimal string representation.

* ``AT#XAPOLL`` parameter changes:

  * Added ``<handle>`` as the first parameter to the ``AT#XAPOLL`` command.
    If a handle is provided, the operation applies only to that specific socket.
    If no handle is provided, operation applies to all open sockets.
    Removed support for multiple socket handles in a single command.

    * Old syntax: ``AT#XAPOLL=<op>[,<events>[,<handle1>[,<handle2> ...<handle8>]``
    * New syntax: ``AT#XAPOLL=[<handle>],<op>[,<events>]``

  * Other socket operations now require handle parameter:

    * ``AT#XSOCKETOPT=<handle>,<op>,<name>[,<value>]`` (handle parameter added)
    * ``AT#XSSOCKETOPT=<handle>,<op>,<name>[,<value>]`` (handle parameter added)
    * ``AT#XBIND=<handle>,<port>`` (handle parameter added)
    * ``AT#XCONNECT=<handle>,<url>,<port>`` (handle parameter added)

  * Response format changes:

    * ``AT#XSOCKETOPT`` - Response to get options now includes socket handle: ``#XSOCKETOPT: <handle>,<value>`` (previously just ``#XSOCKETOPT: <value>``)
    * ``AT#XSSOCKETOPT`` - Response to get options now includes socket handle: ``#XSSOCKETOPT: <handle>,<value>`` (previously just ``#XSSOCKETOPT: <value>``)
    * ``AT#XCONNECT`` - Response now includes socket handle: ``#XCONNECT: <handle>,<status>`` (previously just ``#XCONNECT: <status>``)
    * ``AT#XSEND`` - Response now includes socket handle: ``#XSEND: <handle>,<size>`` (previously just ``#XSEND: <size>``)
    * ``AT#XRECV`` - Response now includes socket handle and mode: ``#XRECV: <handle>,<mode>,<size>`` (previously just ``#XRECV: <size>``)
    * ``AT#XSENDTO`` - Response now includes socket handle: ``#XSENDTO: <handle>,<size>`` (previously just ``#XSENDTO: <size>``)
    * ``AT#XRECVFROM`` - Response now includes socket handle and mode: ``#XRECVFROM: <handle>,<mode>,<size>,"<ip_addr>",<port>`` (previously just ``#XRECVFROM: <size>,"<ip_addr>",<port>``)

Migration example:

     Old approach (|NCS| SLM):

     .. code-block::

        AT#XSOCKET=1,1,0          // Open socket, returns handle 1
        AT#XCONNECT="server",80   // Connect socket handle 1
        AT#XSEND="data"           // Send on socket handle 1
        AT#XSOCKET=1,1,0          // Open socket, returns handle 2
        AT#XCONNECT="server",80   // Connect socket handle 2
        AT#XRECV=10               // Receive data from socket handle 2 with 10s timeout, no flags
        AT#XSOCKETSELECT=1        // Select socket handle 1
        AT#XSOCKET=0              // Close selected socket handle 1

     New approach (|SM|):

     .. code-block::

        AT#XSOCKET=1,1,0          // Open socket, returns handle 1
        AT#XCONNECT=1,"server",80 // Connect socket handle 1
        AT#XSEND=1,0,0,"data"     // Send on socket handle 1
        AT#XSOCKET=1,1,0          // Open socket, returns handle 2
        AT#XCONNECT=2,"server",80 // Connect socket handle 2
        AT#XRECV=2,0,0,10         // Receive data from socket handle 2 with mode 0, no flags, 10s timeout
        AT#XCLOSE=1               // Close socket handle 1

PPP connection management changes
---------------------------------

  * To start the PPP connection, run the ``AT#XPPP=1`` command when the modem is put into online mode using the ``AT+CFUN=1`` command.
    The ``AT#XPPP=1`` command can be run before or after the ``AT+CFUN=1`` command.
    So PPP connection is not started automatically anymore when the ``AT+CFUN=1`` command is run.
    After the ``AT#XPPP=1`` command is run, the PPP connection is started when the ``AT+CFUN=1`` command is run and stopped when the network is lost (for example, with ``AT+CFUN=4``, ``AT+CFUN=0``, or bad reception).
    When the network is regained (for example, with ``AT+CFUN=1``), the PPP connection is started again automatically.
    To permanently stop the PPP connection, either the remote peer must disconnect the PPP using LCP termination or the ``AT#XPPP=0`` command must be run.
    If PPP is terminated using LCP termination or the ``AT#XPPP=0`` command, the PPP connection can be started again with the ``AT#XPPP=1`` command.

  * The default behavior of CMUX channels has changed if DLCI 1 is used for PPP.
    Now when PPP is shut down, the CMUX channel 1 switches to AT command mode, and channel 2 is not used for AT commands anymore.

  * Removed:

    * The :file:`overlay-zephyr-modem.conf` file as the default behavior of the |SM| application is compatible with the Zephyr modem driver.
    * The :file:`overlay-ppp-cmux-linux.conf` overlay file.
      Use the :file:`overlay-ppp.conf` and :file:`overlay-cmux.conf` files instead.

Removed features
****************

This section lists features that have been removed from the |SM| compared to the |NCS| v3.1.x `Serial LTE modem (SLM) <Serial LTE modem_>`_.
If you need any of those features with this |SM|, please contact customer support and describe your use case.

* Removed:

  * Support for the ``nrf9161dk``, ``nrf9160dk``, ``thingy91``, and ``nrf9131ek`` boards.

    * Use ``nrf9151dk`` instead.

  * Support for the ``nrf5340dk``, ``nrf52840dk``, and ``nrf7002dk`` boards from the :ref:`sm_at_client_shell_sample`.

    * Use ``nrf54l15dk`` instead.

  * Native TLS support including ``overlay-native_tls.conf``.

  * TCP and UDP clients.
    This includes the removal of the following AT commands:

    * ``AT#XTCPCLI``
    * ``AT#XTCPSEND``
    * ``AT#XUDPCLI``
    * ``AT#XUDPSEND``

    The following URC notifications have also been removed:

    * ``#XTCPDATA``
    * ``#XUDPDATA``

    You can replace this functionality by using the socket AT commands.

    Migration examples:

    * TCP IPv4 client

      |NCS| SLM approach:

      .. code-block::

         AT#XTCPCLI=1,"test.server.com",1234

         #XTCPCLI: 0,"connected"

         OK

         AT#XTCPSEND="echo this"

         #XTCPSEND: 9

         OK

         #XTCPDATA: 9
         echo this

         AT#XTCPCLI=0

         OK

         #XTCPCLI: 0,"disconnected"

      |SM| approach:

      .. code-block::

         AT#XSOCKET=1,1,0

         #XSOCKET: 0,1,6

         OK

         AT#XRECVCFG=0,3

         OK

         AT#XCONNECT=0,"test.server.com",1234

         #XCONNECT: 0,1

         OK

         AT#XSEND=0,0,0,"echo this"

         #XSEND: 0,0,9

         OK

         #XRECV: 0,0,9
         echo this

         AT#XCLOSE

         #XCLOSE: 0,0

         OK

    * DTLS IPv6 client

      |NCS| SLM approach:

      .. code-block::

         AT#XUDPCLI=2,"test.server.com",1235,1000

         #XUDPCLI: 0,"connected"

         OK

         AT#XUDPSEND="echo this"

         #XUDPSEND: 9

         OK

         #XUDPDATA: 9,"::",0
         echo this

        AT#XUDPCLI=0

        OK

      |SM| approach:

      .. code-block::

        AT#XSSOCKET=2,2,0,1000

         #XSSOCKET: 0,2,273

         OK

         AT#XRECVCFG=0,3

         OK

         AT#XCONNECT=0,"test.server.com",1235

         #XCONNECT: 0,1

         OK

         AT#XSEND=0,0,0,"echo this"

         #XSEND: 0,0,9

         OK

         #XRECV: 0,0,9
         echo this

         AT#XCLOSE=0

         #XCLOSE: 0,0

         OK

    You can set the parameters such as ``<hostname_verify>`` and ``<use_dtls_cid>`` using the ``AT#XSSOCKETOPT`` command.

  * TCP and UDP servers.
    This includes the removal of the following AT commands:

    * ``AT#XTCPSVR``
    * ``AT#XTCPHANGUP``
    * ``AT#XUDPSVR``
    * ``AT#XLISTEN``
    * ``AT#XACCEPT``

    There is no direct replacement for these commands.
    In addition, the ``AT_SO_TCP_SRV_SESSTIMEO`` socket option has been removed.

  * HTTP client functionality, including ``AT#XHTTPCCON`` and ``AT#XHTTPCREQ`` commands, and ``#XHTTPCRSP`` notification.
  * FTP and TFTP clients, including ``AT#XFTP`` and ``AT#XTFTP`` commands.
  * The ``AT#XGPIO`` AT command.
  * The ``AT#XPOLL`` command.
    Use ``AT#XAPOLL`` instead.
  * The ``CONFIG_SLM_DATAMODE_URC`` Kconfig option.
  * The ``CONFIG_SLM_START_SLEEP`` Kconfig option.
  * The :file:`overlay-zephyr-modem.conf` file as the default behavior of the |SM| application is compatible with the Zephyr modem driver.
  * The :file:`overlay-ppp-cmux-linux.conf` overlay file.
    Use the :file:`overlay-ppp.conf` and :file:`overlay-cmux.conf` files instead.
