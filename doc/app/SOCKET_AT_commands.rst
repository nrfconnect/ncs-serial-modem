.. _SM_AT_SOCKET:

Socket AT commands
******************

.. contents::
   :local:
   :depth: 2

This page describes socket-related AT commands.
The application can open up to 8 sockets.

For more information on the networking services, see the `Zephyr Network APIs`_.

Socket #XSOCKET
===============

The ``#XSOCKET`` command allows you to open a socket and to check the socket handle.

Set command
-----------

The set command allows you to open a socket.

Syntax
~~~~~~

::

   #XSOCKET=<op>,<type>,<role>[,<cid>]

* The ``<op>`` parameter can accept one of the following values:

  * ``1`` - Open a socket for IP protocol family version 4.
    Protocol family is ignored with ``<type>`` parameter value ``3``.
  * ``2`` - Open a socket for IP protocol family version 6.
    Protocol family is ignored with ``<type>`` parameter value ``3``.

* The ``<type>`` parameter can accept one of the following values:

  * ``1`` - Set ``SOCK_STREAM`` for the stream socket type using the TCP protocol.
  * ``2`` - Set ``SOCK_DGRAM`` for the datagram socket type using the UDP protocol.
  * ``3`` - Set ``SOCK_RAW`` for the raw socket type using a generic packet protocol.
    The ``<op>`` parameter can be either ``1`` or ``2`` as raw socket ignores the protocol family.

* The ``<role>`` parameter can accept one of the following values:

  * ``0`` - Client.
  * ``1`` - Server.

* The ``<cid>`` parameter is an integer indicating the used PDN connection.
  It represents ``cid`` in the ``+CGDCONT`` command.
  Its default value is ``0``.

Response syntax
~~~~~~~~~~~~~~~

::

   #XSOCKET: <handle>,<type>,<protocol>

* The ``<handle>`` value is an integer and can be interpreted as follows:

  * Positive or ``0`` - The socket opened successfully.
  * Negative - The socket failed to open.

* The ``<type>`` value can be one of the following integers:

  * ``1`` - Set ``SOCK_STREAM`` for the stream socket type using the TCP protocol.
  * ``2`` - Set ``SOCK_DGRAM`` for the datagram socket type using the UDP protocol.
  * ``3`` - Set ``SOCK_RAW`` for the raw socket type using a generic IP protocol.

* The ``<protocol>`` value can be one of the following integers:

  * ``0`` - IPPROTO_IP.
  * ``6`` - IPPROTO_TCP.
  * ``17`` - IPPROTO_UDP.

Examples
~~~~~~~~

::

   AT#XSOCKET=1,1,0
   #XSOCKET: 0,1,6
   OK
   AT#XSOCKET=1,2,0
   #XSOCKET: 1,2,17
   OK
   AT#XSOCKET=2,1,0
   #XSOCKET: 2,1,6
   OK
   AT#XSOCKET=1,3,0
   #XSOCKET: 3,3,0
   OK

Read command
------------

The read command allows you to check the socket handle.

Syntax
~~~~~~

::

   #XSOCKET?

Response syntax
~~~~~~~~~~~~~~~

::

   #XSOCKET: <handle>,<family>,<role>,<type>,<cid>

* The ``<handle>`` value is an integer.
  When positive or ``0``, the socket is valid.

* The ``<family>`` value is present only in the response to a request to open the socket.
  It can return one of the following values:

  * ``1`` - IP protocol family version 4.
  * ``2`` - IP protocol family version 6.
  * ``3`` - Packet family.

* The ``<role>`` value can be one of the following integers:

  * ``0`` - Client.
  * ``1`` - Server.

* The ``<type>`` value can be one of the following integers:

  * ``1`` - Set ``SOCK_STREAM`` for the stream socket type using the TCP protocol.
  * ``2`` - Set ``SOCK_DGRAM`` for the datagram socket type using the UDP protocol.
  * ``3`` - Set ``SOCK_RAW`` for the raw socket type using a generic packet protocol.

* The ``<cid>`` parameter is an integer indicating the used PDN connection.
  It represents ``cid`` in the ``+CGDCONT`` command.

Example
~~~~~~~~

::

   AT#XSOCKET?
   #XSOCKET: 0,1,0,1,0
   OK

Test command
------------

The test command tests the existence of the command and provides information about the type of its subparameters.

Syntax
~~~~~~

::

   #XSOCKET=?

Response syntax
~~~~~~~~~~~~~~~

::

   #XSOCKET: <handle>,<list of ops>,<list of types>,<list of roles>,<cid>

Example
~~~~~~~~

::

   AT#XSOCKET=?
   #XSOCKET: <handle>,(1,2),(1,2,3),(0,1),<cid>
   OK

Secure socket #XSSOCKET
=======================

The ``#XSSOCKET`` command allows you to open a secure socket and to check the socket handle.

.. note::
   TLS and DTLS servers are currently not supported.

Set command
-----------

The set command allows you to open a secure socket.

Syntax
~~~~~~

::

   #XSSOCKET=<op>,<type>,<role>,<sec_tag>[,<peer_verify>[,<cid>]]

* The ``<op>`` parameter can accept one of the following values:

  * ``1`` - Open a socket for IP protocol family version 4.
  * ``2`` - Open a socket for IP protocol family version 6.

* The ``<type>`` parameter can accept one of the following values:

  * ``1`` - Set ``SOCK_STREAM`` for the stream socket type using the TLS 1.2 protocol.
  * ``2`` - Set ``SOCK_DGRAM`` for the datagram socket type using the DTLS 1.2 protocol.

* The ``<role>`` parameter can accept one of the following values:

  * ``0`` - Client.
  * ``1`` - Server.

* The ``<sec_tag>`` parameter is an integer.
  It indicates to the modem the credential of the security tag to be used for establishing a secure connection.
  It is associated with a credential, that is, a certificate or PSK. The credential should be stored on the modem side beforehand.
  Note that when ``<role>`` has a value of ``1``, ``<sec_tag>`` is not supported.

* The ``<peer_verify>`` parameter can accept one of the following values:

  * ``0`` - None (default for server role).
  * ``1`` - Optional.
  * ``2`` - Required (default for client role).

* The ``<cid>`` parameter is an integer indicating the used PDN connection.
  It represents ``cid`` in the ``+CGDCONT`` command.
  Its default value is ``0``.

Response syntax
~~~~~~~~~~~~~~~

::

   #XSSOCKET: <handle>,<type>,<protocol>

* The ``<handle>`` value is an integer and can be interpreted as follows:

  * Positive or ``0`` - The socket opened successfully.
  * Negative - The socket failed to open.

* The ``<type>`` value can be one of the following integers:

  * ``1`` - ``SOCK_STREAM`` for the stream socket type using the TLS 1.2 protocol.
  * ``2`` - ``SOCK_DGRAM`` for the datagram socket type using the DTLS 1.2 protocol.

* The ``<protocol>`` value can be one of the following integers:

  * ``258`` - IPPROTO_TLS_1_2.
  * ``273`` - IPPROTO_DTLS_1_2.

Examples
~~~~~~~~

::

   AT#XSSOCKET=1,1,0,16842753,2
   #XSSOCKET: 0,1,258
   OK
   AT#XSSOCKET=1,2,0,16842753
   #XSSOCKET: 1,2,273
   OK

Read command
------------

The read command allows you to check the secure socket handle.

Syntax
~~~~~~

::

   #XSSOCKET?

Response syntax
~~~~~~~~~~~~~~~

::

   #XSSOCKET: <handle>,<family>,<role>,<type>,<sec_tag>,<cid>

* The ``<handle>`` value is an integer.
  When positive or ``0``, the socket is valid.

* The ``<family>`` value can be one of the following integers:

  * ``1`` - IP protocol family version 4.
  * ``2`` - IP protocol family version 6.

* The ``<role>`` value can be one of the following integers:

  * ``0`` - Client
  * ``1`` - Server

* The ``<type>`` value can be one of the following integers:

  * ``1`` - ``SOCK_STREAM`` for the stream socket type using the TLS 1.2 protocol.
  * ``2`` - ``SOCK_DGRAM`` for the datagram socket type using the DTLS 1.2 protocol.

* The ``<sec_tag>`` value is an integer.
  It indicates to the modem the credential of the security tag to be used for establishing a secure connection.

* The ``<cid>`` value is an integer indicating the used PDN connection.
  It represents ``cid`` in the ``+CGDCONT`` command.

Example
~~~~~~~~

::

   AT#XSSOCKET?
   #XSSOCKET: 0,1,0,1,16842753,0
   OK

Test command
------------

The test command tests the existence of the command and provides information about the type of its subparameters.

Syntax
~~~~~~

::

   #XSSOCKET=?

Response syntax
~~~~~~~~~~~~~~~

::

   #XSSOCKET: <handle>,<list of ops>,<list of types>,<list of roles>,<sec_tag>,<peer_verify>,<cid>

Example
~~~~~~~~

::

   AT#XSSOCKET=?
   #XSSOCKET: <handle>,(1,2),(1,2),(0,1),<sec_tag>,<peer_verify>,<cid>
   OK

Close socket #XCLOSE
====================

The ``#XCLOSE`` command allows you to close one or all sockets.

Set command
-----------

The set command allows you to close a specific socket or all open sockets.

Syntax
~~~~~~

::

   #XCLOSE[=<handle>]

* The ``<handle>`` parameter is an optional integer that specifies the socket handle to close.
  This is the handle value returned from ``#XSOCKET`` or ``#XSSOCKET`` commands.
  If omitted, all open sockets will be closed.

Response syntax
~~~~~~~~~~~~~~~

::

   #XCLOSE: <handle>,<result>

* The ``<handle>`` value is an integer indicating the handle of the closed socket.

* The ``<result>`` value indicates the result of closing the socket.
  When ``0``, the socket closed successfully.

When closing all sockets (no handle parameter provided), multiple responses will be sent, one for each socket that was closed.

Examples
~~~~~~~~

Close a specific socket:

::

   AT#XCLOSE=0
   #XCLOSE: 0,0
   OK

Close all open sockets:

::

   AT#XCLOSE
   #XCLOSE: 0,0
   #XCLOSE: 1,0
   #XCLOSE: 2,0
   OK

Read command
------------

The read command is not supported.

Test command
------------

The test command is not supported.

Socket options #XSOCKETOPT
==========================

The ``#XSOCKETOPT`` command allows you to get and set socket options.

Set command
-----------

The set command allows you to get and set socket options.

Syntax
~~~~~~

::

   #XSOCKETOPT=<handle>,<op>,<name>[,<value>]

* The ``<handle>`` parameter is an integer that specifies the socket handle returned from ``#XSOCKET`` or ``#XSSOCKET`` commands.

* The ``<op>`` parameter can accept one of the following values:

  * ``0`` - Get
  * ``1`` - Set

* The ``<name>`` parameter can accept one of the following values:

  * ``2`` - :c:macro:`AT_SO_REUSEADDR` (set-only).

    * ``<value>`` is an integer that indicates whether the reuse of local addresses is enabled.
      It is ``0`` for disabled or ``1`` for enabled.

  * ``20`` - :c:macro:`AT_SO_RCVTIMEO`.

    * ``<value>`` is an integer that indicates the receive timeout in seconds.

  * ``21`` - :c:macro:`AT_SO_SNDTIMEO`.

    * ``<value>`` is an integer that indicates the send timeout in seconds.

  * ``30`` - :c:macro:`AT_SO_SILENCE_ALL`.

    * ``<value>`` is an integer that indicates whether ICMP echo replies for IPv4 and IPv6 are disabled.
      It is ``0`` for allowing ICMP echo replies or ``1`` for disabling them.

  * ``31`` - :c:macro:`AT_SO_IP_ECHO_REPLY`.

    * ``<value>`` is an integer that indicates whether ICMP echo replies for IPv4 are enabled.
      It is ``0`` for disabled or ``1`` for enabled.

  * ``32`` - :c:macro:`AT_SO_IPV6_ECHO_REPLY`.

    * ``<value>`` is an integer that indicates whether ICMP echo replies for IPv6 are enabled.
      It is ``0`` for disabled or ``1`` for enabled.

  * ``40`` - :c:macro:`AT_SO_BINDTOPDN` (set-only).

    * ``<value>`` is an integer that indicates the packet data network ID to bind to.

  * ``55`` - :c:macro:`AT_SO_TCP_SRV_SESSTIMEO`.

    * ``<value>`` is an integer that indicates the TCP server session inactivity timeout for a socket.
      It accepts values from the range ``0`` to ``135``, where ``0`` is no timeout and ``135`` is 2 hours, 15 minutes.

  * ``61`` - :c:macro:`AT_SO_RAI` (set-only).
    Release Assistance Indication (RAI).

    * ``<value>`` The option accepts an integer, indicating the type of RAI.
      Accepted values for the option are:

      * ``1`` - :c:macro:`RAI_NO_DATA`.
        Indicates that the application does not intend to send more data.
        This socket option applies immediately and lets the modem exit connected mode more quickly.

      * ``2`` - :c:macro:`RAI_LAST`.
        Indicates that the application does not intend to send more data after the next call to :c:func:`send` or :c:func:`sendto`.
        This lets the modem exit connected mode more quickly after sending the data.

      * ``3`` - :c:macro:`RAI_ONE_RESP`.
        Indicates that the application is expecting to receive just one data packet after the next call to :c:func:`send` or :c:func:`sendto`.
        This lets the modem exit connected mode more quickly after having received the data.

      * ``4`` - :c:macro:`RAI_ONGOING`.
        Indicates that the application is expecting to receive just one data packet after the next call to :c:func:`send` or :c:func:`sendto`.
        This lets the modem exit connected mode more quickly after having received the data.

      * ``5`` - :c:macro:`RAI_WAIT_MORE`.
        Indicates that the socket is in active use by a server application.
        This lets the modem stay in connected mode longer.

  * ``62`` - :c:macro:`AT_SO_IPV6_DELAYED_ADDR_REFRESH`.

    * ``<value>`` is an integer that indicates whether delayed IPv6 address refresh is enabled.
      It is ``0`` for disabled or ``1`` for enabled.

See `nRF socket options <nrfxlib_nrf_sockets_>`_ for explanation of the supported options.

Examples
~~~~~~~~

::

   AT#XSOCKETOPT=0,1,20,30
   OK

::

   AT#XSOCKETOPT=0,0,20
   #XSOCKETOPT: 0,30
   OK

Read command
------------

The read command is not supported.

Test command
------------

The test command tests the existence of the command and provides information about the type of its subparameters.

Syntax
~~~~~~

::

   #XSOCKETOPT=?

Response syntax
~~~~~~~~~~~~~~~

::

   #XSOCKETOPT: <handle>,<list of ops>,<name>,<value>

Example
~~~~~~~~

::

   AT#XSOCKETOPT=?
   #XSOCKETOPT: <handle>,(0,1),<name>,<value>
   OK

.. _SM_AT_SSOCKETOPT:

Secure socket options #XSSOCKETOPT
==================================

The ``#XSSOCKETOPT`` command allows you to get and set socket options for secure sockets.

Set command
-----------

The set command allows you to get and set socket options for secure sockets.

Syntax
~~~~~~

::

   #XSSOCKETOPT=<handle>,<op>,<name>[,<value>]

* The ``<handle>`` parameter is an integer that specifies the socket handle returned from ``#XSSOCKET`` command.

* The ``<op>`` parameter can accept one of the following values:

  * ``0`` - Get.
  * ``1`` - Set.

* The ``<name>`` parameter can accept one of the following values:

  * ``2`` - :c:macro:`AT_TLS_HOSTNAME`.

    * ``<value>`` is a string that indicates the hostname to check against during TLS handshakes.
      It can be ``NULL`` to disable hostname verification.

  * ``4`` - :c:macro:`AT_TLS_CIPHERSUITE_USED` (get-only).
    The TLS cipher suite chosen during the TLS handshake.
    This option is only supported with modem firmware 2.0.0 and newer.

  * ``5`` - :c:macro:`AT_TLS_PEER_VERIFY`.

    * ``<value>`` is an integer that indicates what peer verification level should be used.
      It is ``0`` for none, ``1`` for optional or ``2`` for required.

  * ``12`` - :c:macro:`AT_TLS_SESSION_CACHE`.

    * ``<value>`` is an integer that indicates whether TLS session caching should be used.
      It is ``0`` for disabled or ``1`` for enabled.

  * ``13`` - :c:macro:`AT_TLS_SESSION_CACHE_PURGE` (set-only).
    Indicates that the TLS session cache should be deleted.

    * ``<value>`` can be any integer value.

  * ``14`` - :c:macro:`AT_TLS_DTLS_CID` (set-only).

    * ``<value>`` is an integer that indicates the DTLS connection identifier setting.
      It can be one of the following values:

      * ``0`` - :c:macro:`TLS_DTLS_CID_DISABLED`.
      * ``1`` - :c:macro:`TLS_DTLS_CID_SUPPORTED`.
      * ``2`` - :c:macro:`TLS_DTLS_CID_ENABLED`.

    This option is only supported with modem firmware 1.3.5 and newer.
    See `NRF_SO_SEC_DTLS_CID <nrfxlib_dtls_cid_settings_>`_ for more details regarding the allowed values.

  * ``15`` - :c:macro:`AT_TLS_DTLS_CID_STATUS` (get-only).
    It is the DTLS connection identifier status.
    It can be retrieved after the DTLS handshake.
    This option is only supported with modem firmware 1.3.5 and newer.
    See `NRF_SO_SEC_DTLS_CID_STATUS <nrfxlib_dtls_cid_status_>`_ for more details regarding the returned values.

  * ``18`` - :c:macro:`AT_TLS_DTLS_HANDSHAKE_TIMEO`.

    * ``<value>`` is an integer that indicates the DTLS handshake timeout in seconds.
      It can be one of the following values: ``1``, ``3``, ``7``, ``15``, ``31``, ``63``, ``123``.

See `nRF socket options <nrfxlib_nrf_sockets_>`_ for explanation of the supported options.


Example
~~~~~~~~

::

   AT#XSSOCKETOPT=0,1,5,2
   OK

Read command
------------

The read command is not supported.

Test command
------------

The test command tests the existence of the command and provides information about the type of its subparameters.

Syntax
~~~~~~

::

   #XSSOCKETOPT=?

Response syntax
~~~~~~~~~~~~~~~

::

   #XSSOCKETOPT: <handle>,<list of ops>,<name>,<value>

Example
~~~~~~~~

::

   AT#XSSOCKETOPT=?
   #XSSOCKETOPT: <handle>,(0,1),<name>,<value>
   OK


Socket binding #XBIND
=====================

The ``#XBIND`` command allows you to bind a socket with a local port.

This command can be used with TCP servers and both UDP clients and servers.

Set command
-----------

The set command allows you to bind a socket with a local port.

Syntax
~~~~~~

::

   #XBIND=<handle>,<port>

* The ``<handle>`` parameter is an integer that specifies the socket handle returned from ``#XSOCKET`` or ``#XSSOCKET`` commands.

* The ``<port>`` parameter is an unsigned 16-bit integer (0 - 65535).
  It represents the specific port to use for binding the socket.

Example
~~~~~~~~

::

   AT#XBIND=0,1234
   OK

Read command
------------

The read command is not supported.

Test command
------------

The test command is not supported.

Connection #XCONNECT
====================

The ``#XCONNECT`` command allows you to connect to a server and to check the connection status.

This command is for TCP and UDP clients.

Set command
-----------

The set command allows you to connect to a TCP or UDP server.

Syntax
~~~~~~

::

   #XCONNECT=<handle>,<url>,<port>

* The ``<handle>`` parameter is an integer that specifies the socket handle returned from ``#XSOCKET`` or ``#XSSOCKET`` commands.

* The ``<url>`` parameter is a string.
  It indicates the hostname or the IP address of the server.
  The maximum supported size of the hostname is 128 bytes.
  When using IP addresses, it supports both IPv4 and IPv6.

* The ``<port>`` parameter is an unsigned 16-bit integer (0 - 65535).
  It represents the port of the TCP or UDP service on the remote server.

Response syntax
~~~~~~~~~~~~~~~

::

   #XCONNECT: <handle>,<status>

* The ``<handle>`` value is an integer indicating the socket handle.

* The ``<status>`` value is an integer.
  It can return one of the following values:

  * ``1`` - Connected.
  * ``0`` - Disconnected.

Examples
~~~~~~~~

::

   AT#XCONNECT=0,"test.server.com",1234
   #XCONNECT: 0,1
   OK

::

   AT#XCONNECT=1,"192.168.0.1",1234
   #XCONNECT: 1,1
   OK

::

   AT#XCONNECT=2,"2a02:c207:2051:8976::1",4567
   #XCONNECT: 2,1
   OK

Read command
------------

The read command is not supported.

Test command
------------

The test command is not supported.

Set listen mode #XLISTEN
========================

The ``#XLISTEN`` command allows you to put the TCP socket in listening mode for incoming connections.

This command is for TCP servers.

Set command
-----------

The set command allows you to put the TCP socket in listening mode for incoming connections.

Syntax
~~~~~~

::

   #XLISTEN=<handle>

* The ``<handle>`` parameter is an integer that specifies the socket handle returned from ``#XSOCKET`` or ``#XSSOCKET`` commands.

Response syntax
~~~~~~~~~~~~~~~

There is no response.

Example
~~~~~~~~

::

   AT#XLISTEN=0
   OK

Read command
------------

The read command is not supported.

Test command
------------

The test command is not supported.

Accept connection #XACCEPT
==========================

The ``#XACCEPT`` command allows you to accept an incoming connection from a TCP client.

This command is for TCP servers.

Set command
-----------

The set command allows you to wait for the TCP client to connect.

Syntax
~~~~~~

::

   #XACCEPT=<handle>,<timeout>

* The ``<handle>`` parameter is an integer that specifies the socket handle returned from ``#XSOCKET`` or ``#XSSOCKET`` commands.

* The ``<timeout>`` value sets the timeout value in seconds.
  ``0`` means no timeout, and it makes this request become blocking.

Response syntax
~~~~~~~~~~~~~~~

::

   #XACCEPT: <handle>,<ip_addr>

* The ``<handle>`` value is an integer.
  It represents the socket handle of the accepted connection.

* The ``<ip_addr>`` value indicates the IP address of the peer host.

Example
~~~~~~~~

::

   AT#XACCEPT=0,60
   #XACCEPT: 0,"192.168.0.2"
   OK

Read command
------------

The read command is not supported.

Test command
------------

The test command is not supported.

Send data #XSEND
================

The ``#XSEND`` command allows you to send data over TCP and UDP connections.

Set command
-----------

The set command allows you to send data over the connection.

Syntax
~~~~~~

::

   #XSEND=<handle>,<mode>,<flags>[,<data>]

* The ``<handle>`` parameter is an integer that specifies the socket handle returned from ``#XSOCKET`` or ``#XSSOCKET`` commands.

* The ``<mode>`` parameter specifies the data sending mode:

  * ``0`` - String mode. Data is provided directly in the command as the ``<data>`` parameter.
  * ``1`` - Hex string mode. Data is provided as a hexadecimal string in the ``<data>`` parameter.
  * ``2`` - Data mode. |SM| enters ``sm_data_mode`` for data input.

* The ``<flags>`` parameter sets the sending behavior.
  It can be set to the following value:

  * ``0`` - No flags set.
  * ``512`` - Blocks send operation until the request is acknowledged.
    The request will not return until the send operation is completed by lower layers, or until the timeout given by the AT_SO_SNDTIMEO socket option, is reached.
    Valid timeout values are 1 to 600 seconds.

* The ``<data>`` parameter is required when ``<mode>`` is ``0`` (string mode) or ``1`` (hex string mode).
  For string mode (``0``), it is a string that contains the data to be sent.
  For hex string mode (``1``), it is a hexadecimal string representation of the data to be sent.
  The maximum payload size in hexadecimal string mode is up to 2800 characters (1400 bytes).
  For large packets, it is recommended to use data mode (``2``) since AT parser's memory limits the maximum size of data that can be sent in string or hex string modes.
  This parameter is not used when ``<mode>`` is ``2`` (data mode).

Response syntax
~~~~~~~~~~~~~~~

::

   #XSEND: <handle>,<size>

* The ``<handle>`` value is an integer indicating the socket handle.

* The ``<size>`` value is an integer.
  It represents the actual number of bytes that has been sent.

Example
~~~~~~~~

::

   AT#XSEND=0,0,0,"Test TCP"
   #XSEND: 0,8
   OK

   AT#XSEND=0,1,0,"48656C6C6F"
   #XSEND: 0,5
   OK

   AT#XSEND=1,2,512
   OK
   Test datamode with flags
   +++

Read command
------------

The read command is not supported.

Test command
------------

The test command is not supported.

Receive data #XRECV
===================

The ``#XRECV`` command allows you to receive data over TCP or UDP connections.

Set command
-----------

The set command allows you to receive data over the connection.

Syntax
~~~~~~

::

   #XRECV=<handle>,<mode>,<flags>,<timeout>[,<data_len>]

* The ``<handle>`` parameter is an integer that specifies the socket handle returned from ``#XSOCKET`` or ``#XSSOCKET`` commands.

* The ``<mode>`` parameter specifies the receive mode:

  * ``0`` - Binary mode. Data is received as binary data.
  * ``1`` - Hex string mode. Data is received as a hexadecimal string representation.

* The ``<flags>`` parameter sets the receiving behavior based on the BSD socket definition.
  It can be set to one of the following values:

  * ``0`` - No flags set.
  * ``2`` - Read data without removing it from the socket input queue.
  * ``64`` - Override the operation to non-blocking.
  * ``256`` (TCP only) - Block until the full amount of data can be returned.

* The ``<timeout>`` parameter sets the timeout value in seconds.
  When ``0``, it means no timeout, and it makes this request block indefinitely.

* The ``<data_len>`` parameter is optional and sets the maximum number of bytes to receive.
  The maximum value is 2048 bytes, which is also the default value when the parameter is omitted.

Response syntax
~~~~~~~~~~~~~~~

::

   #XRECV: <handle>,<mode>,<size>
   <data>

* The ``<handle>`` value is an integer indicating the socket handle.

* The ``<mode>`` value is an integer indicating the receive mode used.

* The ``<data>`` value is a string that contains the data being received.

* The ``<size>`` value is an integer that represents the actual number of bytes received.
  In case of hex string mode, it represents the number of bytes before conversion to hexadecimal format.

Example
~~~~~~~~

::

   AT#XRECV=0,0,0,10
   #XRECV: 0,0,7
   Test OK
   OK

   AT#XRECV=0,1,0,10
   #XRECV: 0,1,5
   74205A6F63
   OK

Read command
------------

The read command is not supported.

Test command
------------

The test command is not supported.

UDP send data #XSENDTO
======================

The ``#XSENDTO`` command allows you to send data over UDP.

Set command
-----------

The set command allows you to send data over UDP.

Syntax
~~~~~~

::

   #XSENDTO=<handle>,<mode>,<flags>,<url>,<port>[,<data>]

* The ``<handle>`` parameter is an integer that specifies the socket handle returned from ``#XSOCKET`` or ``#XSSOCKET`` commands.

* The ``<mode>`` parameter specifies the data sending mode:

  * ``0`` - String mode. Data is provided directly in the command as the ``<data>`` parameter.
  * ``1`` - Hex string mode. Data is provided as a hexadecimal string in the ``<data>`` parameter.
  * ``2`` - Data mode. |SM| enters ``sm_data_mode`` for data input.

* The ``<flags>`` parameter sets the sending behavior.
  It can be set to the following value:

  * ``0`` - No flags set.
  * ``512`` - Blocks send operation until the request is acknowledged.
    The request will not return until the send operation is completed by lower layers, or until the timeout given by the AT_SO_SNDTIMEO socket option, is reached.
    Valid timeout values are 1 to 600 seconds.

* The ``<url>`` parameter is a string.
  It indicates the hostname or the IP address of the remote peer.
  The maximum size of the hostname is 128 bytes.
  When using IP addresses, it supports both IPv4 and IPv6.

* The ``<port>`` parameter is an unsigned 16-bit integer (0 - 65535).
  It represents the port of the UDP service on remote peer.

* The ``<data>`` parameter is required when ``<mode>`` is ``0`` (string mode) or ``1`` (hex string mode).
  For string mode (``0``), it is a string that contains the data to be sent.
  For hex string mode (``1``), it is a hexadecimal string representation of the data to be sent.
  The maximum payload size in hexadecimal string mode is up to 2800 characters (1400 bytes).
  For large packets, it is recommended to use data mode (``2``) since AT parser's memory limits the maximum size of data that can be sent in string or hex string modes.
  This parameter is not used when ``<mode>`` is ``2`` (data mode).

* UDP packets that exceed 1500 bytes, including headers, may be dropped by the network due to MTU (Maximum Transmission Unit) restrictions.

Response syntax
~~~~~~~~~~~~~~~

::

   #XSENDTO: <handle>,<size>

* The ``<handle>`` value is an integer indicating the socket handle.

* The ``<size>`` value is an integer.
  It represents the actual number of bytes that has been sent.

Example
~~~~~~~~

::

   AT#XSENDTO=0,0,0,"test.server.com",1234,"Test UDP"
   #XSENDTO: 0,8
   OK

   AT#XSENDTO=0,1,0,"test.server.com",1234,"48656C6C6F"
   #XSENDTO: 0,5
   OK

Read command
------------

The read command is not supported.

Test command
------------

The test command is not supported.

UDP receive data #XRECVFROM
===========================

The ``#XRECVFROM`` command allows you to receive data over UDP.

Set command
-----------

The set command allows you to receive data over UDP.

Syntax
~~~~~~

::

   #XRECVFROM=<handle>,<mode>,<flags>,<timeout>[,<data_len>]

* The ``<handle>`` parameter is an integer that specifies the socket handle returned from ``#XSOCKET`` or ``#XSSOCKET`` commands.

* The ``<mode>`` parameter specifies the receive mode:

  * ``0`` - Binary mode. Data is received as binary data.
  * ``1`` - Hex string mode. Data is received as a hexadecimal string representation.

* The ``<flags>`` parameter sets the receiving behavior based on the BSD socket definition.
  It can be set to one of the following values:

  * ``0`` - No flags set.
  * ``2`` - Read data without removing it from the socket input queue.
  * ``64`` - Override the operation to non-blocking.

* The ``<timeout>`` parameter sets the timeout value in seconds.
  When ``0``, it means no timeout, and it makes this request block indefinitely.

* The ``<data_len>`` parameter is optional and sets the maximum number of bytes to receive.
  The maximum value is 2048 bytes, which is also the default value when the parameter is omitted.

Response syntax
~~~~~~~~~~~~~~~

::

   #XRECVFROM: <handle>,<mode>,<size>,"<ip_addr>",<port>
   <data>

* The ``<handle>`` value is an integer indicating the socket handle.

* The ``<mode>`` value is an integer indicating the receive mode used.

* The ``<data>`` value is a string that contains the data being received.

* The ``<size>`` value is an integer that represents the actual number of bytes received.
  In case of hex string mode, it represents the number of bytes before conversion to hexadecimal format.

* The ``<ip_addr>`` value is a string that represents the IPv4 or IPv6 address of the remote peer.

* The ``<port>`` value is an integer that represents the UDP port of the remote peer.

Example
~~~~~~~~

::

   AT#XRECVFROM=0,0,0,10
   #XRECVFROM: 0,0,7,"192.168.1.100",24210
   Test OK
   OK

   AT#XRECVFROM=0,1,0,10
   #XRECVFROM: 0,1,7,"192.168.1.100",24210
   54657374205A4D
   OK

Read command
------------

The read command is not supported.

Test command
------------

The test command is not supported.

Asynchronous socket polling #XAPOLL
===================================

The ``#XAPOLL`` command allows you to receive Unsolicited Result Code (URC) notifications for events on all opened sockets or for selected sockets that have already been opened.

.. note::

    The ``#XAPOLL`` command is not usable at the same time with the socket AT commands that use poll internally (``#XACCEPT``).

Set command
-----------

The set command allows you to activate or deactivate asynchronous polling for sockets.

Activating asynchronous polling when it is already running, will stop the current polling and start a new one with the new parameters.

Syntax
~~~~~~

::

   #XAPOLL=<op>,<events>[,<handle1>[,<handle2> ...<handle8>]

* The ``<op>`` value can accept one of the following values:

  * ``0`` - Stop asynchronous polling.
  * ``1`` - Start asynchronous polling.

* The ``<events>`` value is an integer, which is interpreted as a bit field.
  It represents the events to poll for, which can be a combination of ``POLLIN`` and ``POLLOUT``.
  Permanent error and closure events (``POLLERR``, ``POLLHUP``, and ``POLLNVAL``) are always polled.
  The value can be any combination of the following values summed up:

  * ``0`` - Poll the default events.
  * ``1`` - Read events (``POLLIN``) are polled, in addition to the default events.
  * ``4`` - Write events (``POLLOUT``) are polled, in addition to the default events.

* The ``<handleN>`` value sets the socket handle to poll.
  Handles are sent in the ``AT#XSOCKET`` or ``AT#XSSOCKET`` responses.
  Handles can also be obtained with ``AT#XSOCKET?`` or ``AT#XSSOCKET?`` commands.
  If no handles are specified, all open sockets will be polled, including any new sockets that are created after ``#XAPOLL`` has been started.

Response syntax
~~~~~~~~~~~~~~~

When the asynchronous socket events are enabled, |SM| sends events as URC notifications.

* For ``POLLIN`` events, the URC notification is sent only for the first incoming data on the socket.
  ``AT#XRECV`` or ``AT#XRECVFROM`` command will re-enable the URC notification for the next incoming data.

* For ``POLLOUT`` events, the URC notification is sent only for the first time when the socket is ready for writing.
  ``AT#XSEND`` or ``AT#XSENDTO`` command will re-enable the URC notification for the next time when the socket is ready for writing.

* For ``POLLERR``, ``POLLHUP``, and ``POLLNVAL`` events, the URC notification is sent only once for each socket.
  No further URC notifications will be sent for the same socket.

::

   #XAPOLL: <handle>,<revents>

* The ``<handle>`` value is an integer.
  It is the handle of the socket that has events.

* The ``<revents>`` value is an integer, which must be interpreted as a bit field.
  It represents the returned events as a combination of ``POLLIN`` (1), ``POLLOUT`` (4), ``POLLERR`` (8), ``POLLHUP`` (16), and ``POLLNVAL`` (32) summed up.
  Hexadecimal representation is avoided to support AT command parsers that do not support hexadecimal values.

Example
~~~~~~~

::

   AT#XAPOLL=1,5

   OK

   AT#XSOCKET=1,1,0

   #XSOCKET: 1,1,6

   OK

   AT#XCONNECT=1,"test.server.com",1234

   #XCONNECT: 1,1

   OK

   #XAPOLL: 1,4

   // Send data to the test server, which will echo it back.
   AT#XSEND=1,"echo"

   #XSEND: 1,4

   OK

   #XAPOLL: 1,4

   // Test server sends the data back and closes the connection. POLLIN and POLLHUP events are received.
   #XAPOLL: 1,17

   AT#XRECV=1,1

   #XRECV: 1,4
   echo
   OK

   AT#XCLOSE=1

   #XCLOSE: 1,0

   OK

   // #XAPOLL: 1,32 (POLLNVAL) is not received here as a closure event POLLHUP was already received.

   AT#XAPOLL=0

   OK

Read command
------------

The read command allows you to check the status of asynchronous polling.

Syntax
~~~~~~

::

   #XAPOLL?

Response syntax
~~~~~~~~~~~~~~~

::

   #XAPOLL: <running>,<events>,[<handle1> ...<handle8>]

* The ``<running>`` value can be one of the following integers:

  * ``0`` - Asynchronous polling is not running.
  * ``1`` - Asynchronous polling is running.

* The ``<events>`` value is an integer, which must be interpreted as a bit field.
  It represents the events that are being polled, which can be any combination of ``POLLIN`` and ``POLLOUT``.
  Permanent error and closure events (``POLLERR``, ``POLLHUP``, and ``POLLNVAL``) are always polled.
  The value can be any combination of the following values:

  * ``0`` - Poll the default events.
  * ``1`` - Poll read events (``POLLIN``) in addition to the default events.
  * ``4`` - Poll write events (``POLLOUT``) in addition to the default events.

* The ``<handleN>`` values return the socket handles that are being polled.

Example
~~~~~~~~

::

   AT#XSOCKET=1,1,0


   #XSOCKET: 0,1,6

   OK
   AT#XAPOLL=1,1


   OK
   AT#XAPOLL?


   #XAPOLL: 1,1,0

   OK

Test command
------------

The test command provides information about the command and its parameters.

Syntax
~~~~~~

::

   #XAPOLL=?

Response syntax
~~~~~~~~~~~~~~~

::

   #XAPOLL: <stop/start>,<events>,<handle1>,<handle2>,...

Example
~~~~~~~~

::

   AT#XAPOLL=?


   #XAPOLL: (0,1),<0,1,4,5>,<handle1>,<handle2>,...

   OK

Resolve hostname #XGETADDRINFO
==============================

The ``#XGETADDRINFO`` command allows you to resolve hostnames to IPv4 and IPv6 addresses.

Set command
-----------

The set command allows you to resolve hostnames to IPv4 and IPv6 addresses.

Syntax
~~~~~~

::

   #XGETADDRINFO=<hostname>[,<address_family>]

* The ``<hostname>`` parameter is a string.
* The ``<address_family>`` parameter is an integer that gives a hint for DNS query on address family.

  * ``0`` means unspecified address family.
  * ``1`` means IPv4 address family.
  * ``2`` means IPv6 address family.

  If ``<address_family>`` is not specified, there will be no hint given for DNS query.

Response syntax
~~~~~~~~~~~~~~~

::

   #XGETADDRINFO: "<ip_addresses>"

* The ``<ip_addresses>`` value is a string.
  It indicates the IPv4 or IPv6 address of the resolved hostname.

Example
~~~~~~~~

::

   AT#XGETADDRINFO="google.com"
   #XGETADDRINFO: "142.251.42.142"
   OK
   AT#XGETADDRINFO="google.com",0
   #XGETADDRINFO: "172.217.31.142"
   OK
   AT#XGETADDRINFO="google.com",1
   #XGETADDRINFO: "142.251.42.142"
   OK
   AT#XGETADDRINFO="ipv6.google.com",2
   #XGETADDRINFO: "2404:6800:4004:824::200e"
   OK

Read command
------------

The read command is not supported.

Test command
------------

The test command is not supported.
