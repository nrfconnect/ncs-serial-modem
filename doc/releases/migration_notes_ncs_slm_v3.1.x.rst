.. _migration_3.1.x_SM:

Migration notes from |NCS| v3.1.x SLM
#####################################

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
* Replaced the use of ``CONFIG_NRF_CLOUD_LOCATION`` to |SM|-specific new Kconfig option :ref:`CONFIG_SM_NRF_CLOUD_LOCATION <CONFIG_SM_NRF_CLOUD_LOCATION>`.
  You can now use this option to enable the `nRF Cloud Location Services <nRF Cloud Location Services_>`_ for cloud-assisted geolocation, which supports cellular and Wi-Fi positioning.

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
  * RI pin, which is a level-based output that is configured in the devicetree with the ``ri-gpios`` property.

See :ref:`uart_configuration` for more information on how DTR and RI pins work in the |SM| application.
See :ref:`sm_cellular_modem` for information on how to configure DTR and RI pins when using the |SM| application as a Zephyr modem.

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

* Updated socket creation with ``AT#XSOCKET`` and ``AT#XSSOCKET``:

  * No longer supports closing sockets (``op=0`` removed).
    Only creates sockets and returns a handle.
  * The ``<op>`` parameter has been renamed to ``<family>`` since socket closing is no longer supported.
  * In ``AT#XSOCKET``, a new value ``3`` has been added for the ``<family>`` parameter to represent the packet family to be used with raw sockets.
    This value is not valid for secure sockets in ``AT#XSSOCKET``.

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

* ``AT#XLISTEN`` parameter changes:

  * Added ``<handle>`` parameter to the ``AT#XLISTEN`` command.

    * Old syntax: ``AT#XLISTEN``
    * New syntax: ``AT#XLISTEN=<handle>``

* ``AT#XACCEPT`` parameter changes:

  * Removed ``<timeout>`` parameter and added ``<handle>`` parameter to the ``AT#XACCEPT`` command.
    The command is now non-blocking and returns immediately with an error if there is no incoming connection to accept.

    * Old syntax: ``AT#XACCEPT=<timeout>``
    * New syntax: ``AT#XACCEPT=<handle>``

  * Response format now includes CID and port: ``#XACCEPT: <handle>,<cid>,"<peer_addr>",<peer_port>`` (previously ``#XACCEPT: <handle>,"<ip_addr>"``)

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

HTTP client changes
-------------------

The HTTP client has been redesigned from a dedicated connection-oriented interface to a socket-based interface.
The old |NCS| SLM HTTP client kept its own HTTP connection state with ``AT#XHTTPCCON``, sent requests with ``AT#XHTTPCREQ``, and delivered the raw HTTP response stream followed by ``#XHTTPCRSP`` notifications.
In |SM|, the HTTP client reuses Socket AT commands for transport setup and provides dedicated HTTP AT commands for requests and notifications, including headers, body data, and completion status.

The following is the list of changes:

* Removed ``AT#XHTTPCCON``.

  * Create a plain TCP socket with ``AT#XSOCKET`` or a TLS socket with ``AT#XSSOCKET``.
  * Configure TLS options such as security tag and peer verification with ``AT#XSSOCKETOPT``.
  * Establish the transport connection with ``AT#XCONNECT=<handle>,<host>,<port>``.
  * Close the connection with ``AT#XCLOSE`` when no more requests are needed.

* Updated ``AT#XHTTPCREQ`` to operate on an already connected socket.

  * Old syntax: ``AT#XHTTPCREQ=<method>,<resource>[,<headers>[,<content_type>,<content_length>[,<chunked_transfer>]]]``
  * New syntax: ``AT#XHTTPCREQ=<handle>,<url>,<method>[,<auto_reception>[,<body_len>[,<header 1>[,<header 2>[...]]]]]``

* Changed request parameter model.

  * ``<method>`` changed from a string such as ``"GET"`` or ``"POST"`` to an integer:

    * ``0`` - GET
    * ``1`` - POST
    * ``2`` - PUT
    * ``3`` - DELETE
    * ``4`` - HEAD

  * ``<resource>`` is replaced by a full ``<url>`` parameter.
  * Optional headers are no longer passed as one CRLF-delimited string.
    Each header is now passed as a separate optional parameter.
  * For POST and PUT, the body upload length is given with ``<body_len>``.
    The command then enters data mode and the host must send exactly that many bytes.
  * For GET or HEAD requests with extra headers, set ``<body_len>`` to ``0`` as a placeholder before the header parameters.

* Changed response delivery model.

  * Removed the old raw response plus ``#XHTTPCRSP: <received_byte_count>,<state>`` framing.
  * Response headers are now reported with ``#XHTTPCHEAD: <handle>,<status_code>,<content_length>``.
  * In automatic mode, response body chunks are reported with ``#XHTTPCDATA: <handle>,<offset>,<length>`` and the raw body bytes follow immediately.
  * In manual mode, the host pulls body chunks explicitly with ``AT#XHTTPCDATA=<handle>[,<length>]``.
  * Request completion including failure, timeout, or cancel is reported with ``#XHTTPCSTAT: <handle>,<status_code>,<total_bytes>``.

* Added request cancellation with ``AT#XHTTPCCANCEL=<handle>``.

Migration example:

     Old approach (|NCS| SLM):

     .. code-block::

        AT#XHTTPCCON=1,"postman-echo.com",80
        #XHTTPCCON: 1
        OK

        AT#XHTTPCREQ="GET","/get?foo1=bar1&foo2=bar2"
        OK
        #XHTTPCREQ: 0

        HTTP/1.1 200 OK
        <headers>

        #XHTTPCRSP: 244,1
        <244 bytes of body>

     New approach (|SM|):

     .. code-block::

        AT#XSOCKET=1,1,0
        #XSOCKET: 0,1,6
        OK

        AT#XCONNECT=0,"postman-echo.com",80
        #XCONNECT: 0,1
        OK

        AT#XHTTPCREQ=0,"http://postman-echo.com/get?foo1=bar1&foo2=bar2",0
        #XHTTPCREQ: 0
        OK

        #XHTTPCHEAD: 0,200,244

        #XHTTPCDATA: 0,0,244
        <244 bytes of body>

        #XHTTPCSTAT: 0,200,244

For full details of the new interface, see :ref:`SM_AT_HTTPC` and :ref:`SM_AT_SOCKET`.

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
    The following AT commands have been removed:

    * ``AT#XTCPSVR``
    * ``AT#XTCPHANGUP``
    * ``AT#XUDPSVR``

    The ``AT#XLISTEN`` and ``AT#XACCEPT`` commands have been reintroduced and you can use them to implement TCP server functionality using the socket AT commands.
    However, there is no support for TLS or DTLS servers, as the nRF91 modem does not support TLS server sockets.

    You can replace this functionality by using the socket AT commands.

    Migration examples:

    * TCP IPv4 server

      |NCS| SLM approach:

      .. code-block::

         AT#XTCPSVR=1,1000

         #XTCPSVR: 0,"started"

         OK

         #XTCPSVR: "192.0.2.1","connected"

         #XTCPDATA: 9
         echo this

         AT#XTCPSEND="echo this"

         #XTCPSEND: 9

         OK

         AT#XTCPSVR?

         #XTCPSVR: 0,2,1

         OK

         AT#XTCPHANGUP=2

         #XTCPSVR: 0,"disconnected"

         OK

         AT#XTCPSVR=0

         #XTCPSVR: 0,"stopped"

         OK


      |SM| approach:

      .. code-block::

         AT#XSOCKET=1,1,0

         #XSOCKET: 0,1,6

         OK

         AT#XAPOLL=,1,1

         OK

         AT#XBIND=0,1000

         OK

         AT#XLISTEN=0

         OK

         #XAPOLL: 0,1

         AT#XACCEPT=0

         #XACCEPT: 1,0,"192.0.2.1",54321

         OK

         #XAPOLL: 1,1

         AT#XRECV=1,0,0,1

         #XRECV: 1,0,9
         echo this

         OK

         AT#XSEND=1,0,0,"echo this"

         #XSEND: 1,0,9

         OK

         AT#XCLOSE=1

         #XCLOSE: 1,0

         OK

         AT#XCLOSE=0

         #XCLOSE: 0,0

         OK

    * UDP IPv6 server

      |NCS| SLM approach:

      .. code-block::

         AT#XUDPSVR=2,1235

         #XUDPSVR: 0,"started"

         OK

         #XUDPDATA: 9,"2001:db8::1",54321
         echo this

         AT#XUDPSEND="echo this"

         #XUDPSEND: 9

         OK

         AT#XUDPSVR=0

         #XUDPSVR: 0,"stopped"

         OK

      |SM| approach:

      .. code-block::

         AT#XSOCKET=2,2,0

         #XSOCKET: 0,2,17

         OK

         AT#XAPOLL=0,1,1

         OK

         AT#XBIND=0,1235

         OK

         #XAPOLL: 0,1

         AT#XRECVFROM=0,0,0,1

         #XRECVFROM: 0,0,9,"2001:db8::1",54321
         echo this

         OK

         AT#XSENDTO=0,0,0,"2001:db8::1",54321,"echo this"

         #XSENDTO: 0,9

         OK

         AT#XCLOSE=0

         #XCLOSE: 0,0

         OK

  * FTP and TFTP clients, including ``AT#XFTP`` and ``AT#XTFTP`` commands.
  * The ``AT#XGPIO`` AT command.
  * The ``AT#XPOLL`` command.
    Use ``AT#XAPOLL`` instead.
  * The ``CONFIG_SLM_DATAMODE_URC`` Kconfig option.
  * The ``CONFIG_SLM_START_SLEEP`` Kconfig option.
  * The :file:`overlay-zephyr-modem.conf` file as the default behavior of the |SM| application is compatible with the Zephyr modem driver.
  * The :file:`overlay-ppp-cmux-linux.conf` overlay file.
    Use the :file:`overlay-ppp.conf` and :file:`overlay-cmux.conf` files instead.
  * The :file:`sm_auto_connect.h` header file.
    Use the ``CONFIG_SM_AUTO_CONNECT*`` Kconfig options to configure automatic network attach.
  * The ``CONFIG_SM_SKIP_READY_MSG`` Kconfig option.
    The ``Ready\r\n`` message is always sent when the |SM| application is ready to accept AT commands.
  * The ``CONFIG_SM_AT_MAX_PARAM`` Kconfig option.
    This Kconfig option has not been relevant in the latest versions of |NCS| SLM, as the AT parser library is now used.
  * The ``CONFIG_SM_GNSS_OUTPUT_NMEA_ON_CMUX_CHANNEL`` Kconfig option.
    You can see the NMEA messages in debug logs with the ``CONFIG_SM_GNSS_OUTPUT_NMEA_SATELLITES`` Kconfig option, which is enabled by default if the ``CONFIG_SM_LOG_LEVEL_DBG`` Kconfig option is set.
