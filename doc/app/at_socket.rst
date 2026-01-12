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

   AT#XSOCKET=<op>,<type>,<role>[,<cid>]

* The ``<op>`` parameter can accept one of the following values:

  * ``1`` - Open a socket for IP protocol family version 4.
    The protocol family is ignored with the ``<type>`` parameter value ``3``.
  * ``2`` - Open a socket for IP protocol family version 6.
    The protocol family is ignored with the ``<type>`` parameter value ``3``.

* The ``<type>`` parameter can accept one of the following values:

  * ``1`` - Set ``SOCK_STREAM`` for the stream socket type using the TCP protocol.
  * ``2`` - Set ``SOCK_DGRAM`` for the datagram socket type using the UDP protocol.
  * ``3`` - Set ``SOCK_RAW`` for the raw socket type using a generic packet protocol.
    The ``<op>`` parameter can be either ``1`` or ``2``, as the raw socket ignores the protocol family.

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

* The ``<handle>`` parameter is an integer and can be interpreted as follows:

  * Positive or ``0`` - The socket opened successfully.
  * Negative - The socket failed to open.

* The ``<type>`` parameter can be one of the following integers:

  * ``1`` - Set ``SOCK_STREAM`` for the stream socket type using the TCP protocol.
  * ``2`` - Set ``SOCK_DGRAM`` for the datagram socket type using the UDP protocol.
  * ``3`` - Set ``SOCK_RAW`` for the raw socket type using a generic IP protocol.

* The ``<protocol>`` parameter can be one of the following integers:

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

   AT#XSOCKET?

Response syntax
~~~~~~~~~~~~~~~

::

   #XSOCKET: <handle>,<family>,<role>,<type>,<cid>

* The ``<handle>`` parameter is an integer.
  When positive or ``0``, the socket is valid.

* The ``<family>`` parameter is present only in the response to a request to open the socket.
  It can return one of the following values:

  * ``1`` - IP protocol family version 4.
  * ``2`` - IP protocol family version 6.
  * ``3`` - Packet family.

* The ``<role>`` parameter can be one of the following integers:

  * ``0`` - Client.
  * ``1`` - Server.

* The ``<type>`` parameter can be one of the following integers:

  * ``1`` - Set ``SOCK_STREAM`` for the stream socket type using the TCP protocol.
  * ``2`` - Set ``SOCK_DGRAM`` for the datagram socket type using the UDP protocol.
  * ``3`` - Set ``SOCK_RAW`` for the raw socket type using a generic packet protocol.

* The ``<cid>`` parameter is an integer indicating the used PDN connection.
  It represents ``cid`` in the ``+CGDCONT`` command.

Example
~~~~~~~

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

   AT#XSOCKET=?

Response syntax
~~~~~~~~~~~~~~~

::

   #XSOCKET: <handle>,<list of ops>,<list of types>,<list of roles>,<cid>

Example
~~~~~~~

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

   AT#XSSOCKET=<op>,<type>,<role>,<sec_tag>[,<peer_verify>[,<cid>]]

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
  It is associated with a credential, that is, a certificate or PSK.
  The credential must be stored on the modem side beforehand.

  .. note::
     When ``<role>`` has a value of ``1``, ``<sec_tag>`` is not supported.

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

* The ``<handle>`` parameter is an integer and can be interpreted as follows:

  * Positive or ``0`` - The socket opened successfully.
  * Negative - The socket failed to open.

* The ``<type>`` parameter can be one of the following integers:

  * ``1`` - ``SOCK_STREAM`` for the stream socket type using the TLS 1.2 protocol.
  * ``2`` - ``SOCK_DGRAM`` for the datagram socket type using the DTLS 1.2 protocol.

* The ``<protocol>`` parameter can be one of the following integers:

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

   AT#XSSOCKET?

Response syntax
~~~~~~~~~~~~~~~

::

   #XSSOCKET: <handle>,<family>,<role>,<type>,<sec_tag>,<cid>

* The ``<handle>`` parameter is an integer.
  When positive or ``0``, the socket is valid.

* The ``<family>`` parameter can be one of the following integers:

  * ``1`` - IP protocol family version 4.
  * ``2`` - IP protocol family version 6.

* The ``<role>`` parameter can be one of the following integers:

  * ``0`` - Client
  * ``1`` - Server

* The ``<type>`` parameter can be one of the following integers:

  * ``1`` - ``SOCK_STREAM`` for the stream socket type using the TLS 1.2 protocol.
  * ``2`` - ``SOCK_DGRAM`` for the datagram socket type using the DTLS 1.2 protocol.

* The ``<sec_tag>`` parameter is an integer.
  It indicates to the modem the credential of the security tag to be used for establishing a secure connection.

* The ``<cid>`` parameter is an integer indicating the used PDN connection.
  It represents ``cid`` in the ``+CGDCONT`` command.

Example
~~~~~~~

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

   AT#XSSOCKET=?

Response syntax
~~~~~~~~~~~~~~~

::

   #XSSOCKET: <handle>,<list of ops>,<list of types>,<list of roles>,<sec_tag>,<peer_verify>,<cid>

Example
~~~~~~~

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

   AT#XCLOSE[=<handle>]

* The ``<handle>`` parameter is an optional integer that specifies the socket handle to close.
  This is the handle value returned from ``#XSOCKET`` or ``#XSSOCKET`` commands.
  If omitted, all open sockets will be closed.

Response syntax
~~~~~~~~~~~~~~~

::

   #XCLOSE: <handle>,<result>

* The ``<handle>`` parameter is an integer indicating the handle of the closed socket.

* The ``<result>`` parameter indicates the result of closing the socket.
  When ``0``, the socket was closed successfully.

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

   AT#XSOCKETOPT=<handle>,<op>,<name>[,<value>]

* The ``<handle>`` parameter is an integer that specifies the socket handle returned from ``#XSOCKET`` or ``#XSSOCKET`` commands.

* The ``<op>`` parameter can accept one of the following values:

  * ``0`` - Get
  * ``1`` - Set

* The ``<name>`` parameter can accept one of the following values:

  * ``2`` - ``AT_SO_REUSEADDR`` (set-only).

    * ``<value>`` is an integer that indicates whether the reuse of local addresses is enabled.
      It is ``0`` for disabled or ``1`` for enabled.

  * ``20`` - ``AT_SO_RCVTIMEO``.

    * ``<value>`` is an integer that indicates the receive timeout in seconds.

  * ``21`` - ``AT_SO_SNDTIMEO``.

    * ``<value>`` is an integer that indicates the send timeout in seconds.

  * ``30`` - ``AT_SO_SILENCE_ALL``.

    * ``<value>`` is an integer that indicates whether ICMP echo replies for IPv4 and IPv6 are disabled.
      It is ``0`` for allowing ICMP echo replies or ``1`` for disabling them.

  * ``31`` - ``AT_SO_IP_ECHO_REPLY``.

    * ``<value>`` is an integer that indicates whether ICMP echo replies for IPv4 are enabled.
      It is ``0`` for disabled or ``1`` for enabled.

  * ``32`` - ``AT_SO_IPV6_ECHO_REPLY``.

    * ``<value>`` is an integer that indicates whether ICMP echo replies for IPv6 are enabled.
      It is ``0`` for disabled or ``1`` for enabled.

  * ``40`` - ``AT_SO_BINDTOPDN`` (set-only).

    * ``<value>`` is an integer that indicates the packet data network ID to bind to.

  * ``61`` - ``AT_SO_RAI`` (set-only).
    Release Assistance Indication (RAI).

    * ``<value>`` The option accepts an integer, indicating the type of RAI.
      Accepted values for the option are:

      * ``1`` - ``RAI_NO_DATA``.
        Indicates that the application does not intend to send more data.
        This socket option applies immediately and lets the modem exit connected mode more quickly.

      * ``2`` - ``RAI_LAST``.
        Indicates that the application does not intend to send more data after the next call to :c:func:`send` or :c:func:`sendto`.
        This lets the modem exit connected mode more quickly after sending the data.

      * ``3`` - ``RAI_ONE_RESP``.
        Indicates that the application is expecting to receive just one data packet after the next call to :c:func:`send` or :c:func:`sendto`.
        This lets the modem exit connected mode more quickly after having received the data.

      * ``4`` - ``RAI_ONGOING``.
        Indicates that the application is expecting to receive just one data packet after the next call to :c:func:`send` or :c:func:`sendto`.
        This lets the modem exit connected mode more quickly after having received the data.

      * ``5`` - ``RAI_WAIT_MORE``.
        Indicates that the socket is in active use by a server application.
        This lets the modem stay in connected mode longer.

  * ``62`` - ``AT_SO_IPV6_DELAYED_ADDR_REFRESH``.

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

   AT#XSOCKETOPT=?

Response syntax
~~~~~~~~~~~~~~~

::

   #XSOCKETOPT: <handle>,<list of ops>,<name>,<value>

Example
~~~~~~~

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

   AT#XSSOCKETOPT=<handle>,<op>,<name>[,<value>]

* The ``<handle>`` parameter is an integer that specifies the socket handle returned from ``#XSSOCKET`` command.

* The ``<op>`` parameter can accept one of the following values:

  * ``0`` - Get.
  * ``1`` - Set.

* The ``<name>`` parameter can accept one of the following values:

  * ``2`` - ``AT_TLS_HOSTNAME``

    * ``<value>`` is a string that indicates the hostname to check against during TLS handshakes.
      It can be ``NULL`` to disable hostname verification.

  * ``4`` - ``AT_TLS_CIPHERSUITE_USED`` (get-only).
    The TLS cipher suite is chosen during the TLS handshake.
    This option is only supported with modem firmware v2.0.0 and newer.

  * ``5`` - ``AT_TLS_PEER_VERIFY``.

    * ``<value>`` is an integer that indicates the peer verification level.
      It is ``0`` for none, ``1`` for optional, or ``2`` for required.

  * ``12`` - ``AT_TLS_SESSION_CACHE``.

    * ``<value>`` is an integer that indicates whether to use TLS session caching.
      It is ``0`` for disabled or ``1`` for enabled.

  * ``13`` - ``AT_TLS_SESSION_CACHE_PURGE`` (set-only).
    Indicates that the TLS session cache must be deleted.

    * ``<value>`` can be any integer value.

  * ``14`` - ``AT_TLS_DTLS_CID`` (set-only).

    * ``<value>`` is an integer that indicates the DTLS connection identifier setting.
      It can be one of the following values:

      * ``0`` - ``TLS_DTLS_CID_DISABLED``.
      * ``1`` - ``TLS_DTLS_CID_SUPPORTED``.
      * ``2`` - ``TLS_DTLS_CID_ENABLED``.

    This option is only supported with modem firmware v1.3.5 and newer.
    See `NRF_SO_SEC_DTLS_CID <nrfxlib_dtls_cid_settings_>`_ for more details regarding the allowed values.

  * ``15`` - ``AT_TLS_DTLS_CID_STATUS`` (get-only).
    It is the DTLS connection identifier status.
    It can be retrieved after the DTLS handshake.
    This option is only supported with modem firmware 1.3.5 and newer.
    See `NRF_SO_SEC_DTLS_CID_STATUS <nrfxlib_dtls_cid_status_>`_ for more details regarding the returned values.

  * ``18`` - ``AT_TLS_DTLS_HANDSHAKE_TIMEO``.

    * ``<value>`` is an integer that indicates the DTLS handshake timeout in seconds.
      It can be one of the following values: ``1``, ``3``, ``7``, ``15``, ``31``, ``63``, ``123``.

  * ``22`` - ``AT_TLS_DTLS_FRAG_EXT``.

    * ``<value>`` is an integer that indicates the use of the DTLS fragmentation extension specified in RFC 6066.
      It can be one of the following values:

      * ``0`` - ``DTLS_FRAG_EXT_DISABLED``.
      * ``1`` - ``DTLS_FRAG_EXT_512_ENABLED``.
      * ``2`` - ``DTLS_FRAG_EXT_1024_ENABLED``.

      This is only supported by the following modem firmware:

        * mfw_nrf91x1 v2.0.4 or later
        * mfw_nrf9151-ntn

See `nRF socket options <nrfxlib_nrf_sockets_>`_ for explanation of the supported options.


Example
~~~~~~~

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

   AT#XSSOCKETOPT=?

Response syntax
~~~~~~~~~~~~~~~

::

   #XSSOCKETOPT: <handle>,<list of ops>,<name>,<value>

Example
~~~~~~~

::

   AT#XSSOCKETOPT=?
   #XSSOCKETOPT: <handle>,(0,1),<name>,<value>
   OK


Socket binding #XBIND
=====================

The ``#XBIND`` command allows you to bind a socket with a local port.

You can use this command with TCP and UDP, and it is needed for incoming UDP data, where the remote end targets a particular port.

Set command
-----------

The set command allows you to bind a socket with a local port.

Syntax
~~~~~~

::

   AT#XBIND=<handle>,<port>

* The ``<handle>`` parameter is an integer that specifies the socket handle returned from ``#XSOCKET`` or ``#XSSOCKET`` commands.

* The ``<port>`` parameter is an unsigned 16-bit integer (0 - 65535).
  It represents the specific port to use for binding the socket.

Example
~~~~~~~

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

This command is for TCP and UDP client sockets.

Set command
-----------

The set command allows you to connect to a TCP or UDP server.

Syntax
~~~~~~

::

   AT#XCONNECT=<handle>,<url>,<port>

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

* The ``<handle>`` parameter is an integer indicating the socket handle.

* The ``<status>`` parameter is an integer.
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

Send data #XSEND
================

The ``#XSEND`` command allows you to send data over TCP, UDP, and raw sockets.

Set command
-----------

The set command allows you to send data over the connection.

Syntax
~~~~~~

::

   AT#XSEND=<handle>,<mode>,<flags>,<data> when ``<mode>`` is ``0`` or ``1``

   AT#XSEND=<handle>,<mode>,<flags>[,<data_len>] when ``<mode>`` is ``2``

* The ``<handle>`` parameter is an integer that specifies the socket handle returned from ``#XSOCKET`` or ``#XSSOCKET`` commands.

* The ``<mode>`` parameter specifies the data sending mode:

  * ``0`` - String mode.
    Data is provided directly in the command as the ``<data>`` parameter.
  * ``1`` - Hex string mode.
    Data is provided as a hexadecimal string in the ``<data>`` parameter.
  * ``2`` - Data mode.
    |SM| enters ``sm_data_mode`` for data input.

* The ``<flags>`` parameter sets the sending behavior.
  You can set it to one of the following values:

  * ``0`` - No flags set.
    The request is complete when the data is pushed to the modem buffer.
  * ``512`` - Blocks send operation until the request is acknowledged by the network.
    The request will not return until the data is pushed to the network, or acknowledged by the network (for TCP), or the timeout given by the ``AT_SO_SNDTIMEO`` socket option is reached.
    Valid timeout values are 1 to 600 seconds.
  * ``8192`` - Send unsolicited ``#XSENDNTF`` notification when the request is acknowledged by the network.
    Unsolicited notification will be sent when the data is pushed to the network, or acknowledged by the network (for TCP), or the timeout given by the ``AT_SO_SNDTIMEO`` socket option is reached.
    Valid timeout values are 1 to 600 seconds.
    Further sends for the socket are blocked until the unsolicited notification is received.

* The ``<data>`` parameter is required when ``<mode>`` is ``0`` (string mode) or ``1`` (hex string mode).
  For string mode (``0``), it is a string that contains the data to be sent.
  For hex string mode (``1``), it is a hexadecimal string representation of the data to be sent.
  The maximum payload size in hexadecimal string mode is up to 2800 characters (1400 bytes).
  For large packets, it is recommended to use data mode (``2``) since :ref:`CONFIG_SM_AT_BUF_SIZE <CONFIG_SM_AT_BUF_SIZE>` limits the maximum size of data that can be sent in string or hex string modes.
  This parameter is not used when ``<mode>`` is ``2`` (data mode).

* The ``<data_len>`` parameter is optional and only used when ``<mode>`` is ``2`` (data mode).
  It sets the number of bytes to send in data mode.
  When the required number of bytes are sent, the data mode is exited.
  The termination command :ref:`CONFIG_SM_DATAMODE_TERMINATOR <CONFIG_SM_DATAMODE_TERMINATOR>` is not used in this case.

.. note::

   UDP packets that exceed the Maximum Transmission Unit (MTU) of any network segment along their path might be dropped or fragmented, increasing the risk of packet loss.
   Ethernet networks have an MTU of 1500 bytes, which allows a maximum UDP payload of 1472 bytes for IPv4 and 1452 bytes for IPv6.
   With DTLS sockets, the usable payload size is further reduced due to encryption overhead.

   The cellular network MTU can be queried with the ``AT+CGCONTRDP`` command, but some networks might still drop packets smaller than the reported MTU.

   A UDP payload size of 1200 bytes is commonly recommended, especially for IPv6, as it ensures the total packet size remains below the IPv6 minimum MTU of 1280 bytes after accounting for headers and DTLS overhead.
   Keeping UDP packet sizes well below the theoretical maximum increases the likelihood of successful transmission.
   You can even use 1024 bytes as a safe size for UDP packets.

Response syntax
~~~~~~~~~~~~~~~

::

   #XSEND: <handle>,<result_type>,<size>

* The ``<handle>`` parameter is an integer indicating the socket handle.

* The ``<result_type>`` parameter is an integer indicating the type of result:

  * ``0`` - Indicates that there are no further notifications.
  * ``1`` - Indicates that an unsolicited notification will be sent when the network acknowledged send is completed.

* The ``<size>`` parameter is an integer.
  It represents the actual number of bytes that has been sent.


Unsolicited notification
~~~~~~~~~~~~~~~~~~~~~~~~

For network acknowledged sends (when the ``8192`` flag is used), an unsolicited notification is sent when the send operation is completed.

::

   #XSENDNTF: <handle>,<status>,<size>

* The ``<handle>`` parameter is an integer indicating the socket handle.

* The ``<status>`` parameter is an integer indicating the status of the send acknowledged by the network.
  It can have one of the following values:

  * ``0`` - Success
  * ``-1`` - Failure

* The ``<size>`` parameter is an integer indicating the size of the data sent.


Example
~~~~~~~

::

   AT#XSEND=0,0,0,"Test TCP"
   #XSEND: 0,0,8
   OK

   AT#XSEND=0,1,0,"48656C6C6F"
   #XSEND: 0,0,5
   OK

   AT#XSEND=1,0,8192,"Test notification"
   #XSEND: 1,1,17
   OK
   #XSENDNTF: 1,0,17

   AT#XSEND=1,2,8192
   OK
   Test datamode with flags+++
   #XDATAMODE: 0
   #XSENDNTF: 1,0,24

Read command
------------

The read command is not supported.

Test command
------------

The test command is not supported.

Receive data #XRECV
===================

The ``#XRECV`` command allows you to receive data over TCP, UDP, and raw sockets.

Set command
-----------

The set command allows you to receive data over the connection.

Syntax
~~~~~~

::

   AT#XRECV=<handle>,<mode>,<flags>,<timeout>[,<data_len>]

* The ``<handle>`` parameter is an integer that specifies the socket handle returned from ``#XSOCKET`` or ``#XSSOCKET`` commands.

* The ``<mode>`` parameter specifies the receive mode:

  * ``0`` - Binary mode. Data is received as binary data.
  * ``1`` - Hex string mode. Data is received as a hexadecimal string representation.

* The ``<flags>`` parameter sets the receiving behavior based on the BSD socket definition.
  You can set it to one of the following values:

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

.. sm_recv_response_start

::

   #XRECV: <handle>,<mode>,<size>
   <data>

* The ``<handle>`` parameter is an integer indicating the socket handle.

* The ``<mode>`` parameter is an integer indicating the receive mode used.

* The ``<size>`` parameter is an integer that represents the actual number of bytes received.
  In case of hex string mode, it represents the number of bytes before conversion to hexadecimal format.

* The ``<data>`` parameter is a string that contains the data being received.

.. sm_recv_response_end

Example
~~~~~~~

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

   AT#XSENDTO=<handle>,<mode>,<flags>,<url>,<port>,<data> when ``<mode>`` is ``0`` or ``1``

   AT#XSENDTO=<handle>,<mode>,<flags>,<url>,<port>[,<data_len>] when ``<mode>`` is ``2``

* The ``<handle>`` parameter is an integer that specifies the socket handle returned from ``#XSOCKET`` or ``#XSSOCKET`` commands.

* The ``<mode>`` parameter specifies the data sending mode:

  * ``0`` - String mode. Data is provided directly in the command as the ``<data>`` parameter.
  * ``1`` - Hex string mode. Data is provided as a hexadecimal string in the ``<data>`` parameter.
  * ``2`` - Data mode. |SM| enters ``sm_data_mode`` for data input.

* The ``<flags>`` parameter sets the sending behavior.
  You can set it to one of the following values:

  * ``0`` - No flags set. The request is complete when the data is pushed to the modem buffer.
  * ``512`` - Blocks send operation until the request is acknowledged by network.
    The request will not return until the data is pushed to the network, or acknowledged by network (for TCP), or the timeout given by the AT_SO_SNDTIMEO socket option, is reached.
    Valid timeout values are 1 to 600 seconds.
  * ``8192`` - Send unsolicited ``#XSENDNTF`` notification when the request is acknowledged by network.
    Unsolicited notification will be sent when the data is pushed to the network, or acknowledged by network (for TCP), or the timeout given by the AT_SO_SNDTIMEO socket option, is reached.
    Valid timeout values are 1 to 600 seconds.
    Further sends for the socket are blocked until the unsolicited notification is received.

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

* The ``<data_len>`` parameter is optional and only used when ``<mode>`` is ``2`` (data mode).
  It sets the number of bytes to send in data mode.
  When required number of bytes are sent, the data mode is exited.
  The termination command :ref:`CONFIG_SM_DATAMODE_TERMINATOR <CONFIG_SM_DATAMODE_TERMINATOR>` is not used in this case.

.. note::

   UDP packets that exceed the Maximum Transmission Unit (MTU) of any network segment along their path may be dropped or fragmented, increasing the risk of packet loss.
   Ethernet networks have an MTU of 1500 bytes, which allows a maximum UDP payload of 1472 bytes for IPv4 and 1452 bytes for IPv6.
   With DTLS sockets, the usable payload size is further reduced due to encryption overhead.

   The cellular network MTU can be queried with the ``AT+CGCONTRDP`` command, but some networks may still drop packets smaller than the reported MTU.

   A UDP payload size of 1200 bytes is commonly recommended, especially for IPv6, as it ensures the total packet size remains below the IPv6 minimum MTU of 1280 bytes, after accounting for headers and DTLS overhead.
   Keeping UDP packet sizes well below the theoretical maximum increases the likelihood of successful transmission.
   Even 1024 bytes could be used as a safe size for UDP packets.

.. note::

   With DTLS connections, the connection cannot be established with the ``#XSENDTO`` command.
   Instead, it must be established using the ``#XCONNECT`` command.
   This is a limitation in the nRF91 modem firmware.

Response syntax
~~~~~~~~~~~~~~~

::

   #XSENDTO: <handle>,<result_type>,<size>

* The ``<handle>`` parameter is an integer indicating the socket handle.

* The ``<result_type>`` parameter is an integer indicating the type of result:

  * ``0`` - Indicates that there are no further notifications.
  * ``1`` - Indicates that an unsolicited notification will be sent when the network acknowledged send is completed.

* The ``<size>`` parameter is an integer.
  It represents the actual number of bytes that have been sent.

Unsolicited notification
~~~~~~~~~~~~~~~~~~~~~~~~

For network acknowledged sends (when the ``8192`` flag is used), an unsolicited notification is sent when the send operation is completed.

::

   #XSENDNTF: <handle>,<status>,<size>

* The ``<handle>`` parameter is an integer indicating the socket handle.

* The ``<status>`` parameter is an integer indicating the status of the send acknowledged by the network.
  It can have one of the following values:

  * ``0`` - Success
  * ``-1`` - Failure

* The ``<size>`` parameter is an integer indicating the size of the data sent.

Example
~~~~~~~

::

   AT#XSENDTO=0,0,0,"test.server.com",1234,"Test UDP"
   #XSENDTO: 0,0,8
   OK

   AT#XSENDTO=0,1,0,"test.server.com",1234,"48656C6C6F"
   #XSENDTO: 0,0,5
   OK

   AT#XSENDTO=0,0,8192,"test.server.com",1234,"Test notification"
   #XSENDTO: 0,1,17
   OK
   #XSENDNTF: 0,0,17


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

   AT#XRECVFROM=<handle>,<mode>,<flags>,<timeout>[,<data_len>]

* The ``<handle>`` parameter is an integer that specifies the socket handle returned from ``#XSOCKET`` or ``#XSSOCKET`` commands.

* The ``<mode>`` parameter specifies the receive mode:

  * ``0`` - Binary mode.
    Data is received as binary data.
  * ``1`` - Hex string mode.
    Data is received as a hexadecimal string representation.

* The ``<flags>`` parameter sets the receiving behavior based on the BSD socket definition.
  You can set it to one of the following values:

  * ``0`` - No flags set.
  * ``2`` - Read data without removing it from the socket input queue.
  * ``64`` - Override the operation to non-blocking.

* The ``<timeout>`` parameter sets the timeout value in seconds.
  When ``0``, it means no timeout, and it makes this request block indefinitely.

* The ``<data_len>`` parameter is optional and sets the maximum number of bytes to receive.
  The maximum value is 2048 bytes, which is also the default value when the parameter is omitted.

Response syntax
~~~~~~~~~~~~~~~

.. sm_recvfrom_response_start

::

   #XRECVFROM: <handle>,<mode>,<size>,"<ip_addr>",<port>
   <data>

* The ``<handle>`` parameter is an integer indicating the socket handle.

* The ``<mode>`` parameter is an integer indicating the receive mode used.

* The ``<size>`` parameter is an integer that represents the actual number of bytes received.
  In the case of hex string mode, it represents the number of bytes before conversion to hexadecimal format.

* The ``<ip_addr>`` parameter is a string that represents the IPv4 or IPv6 address of the remote peer.

* The ``<port>`` parameter is an integer that represents the UDP port of the remote peer.

* The ``<data>`` parameter is a string that contains the data being received.

.. sm_recvfrom_response_end

Example
~~~~~~~

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

The ``#XAPOLL`` command allows you to receive Unsolicited Result Code (URC) notifications for poll events on sockets.

Set command
-----------

The set command allows you to activate or deactivate asynchronous polling for sockets.

Syntax
~~~~~~

::

   AT#XAPOLL=[<handle>],<op>,[<events>]

* The ``<handle>`` parameter is an integer that sets the socket handle to poll (optional).
  Handles are sent in the ``AT#XSOCKET`` or ``AT#XSSOCKET`` responses.
  Handles can also be obtained using the ``AT#XSOCKET?`` or ``AT#XSSOCKET?`` command.
  If the handle is omitted, the operation applies to all open sockets and to any new sockets that are created.

* The ``<op>`` parameter can accept one of the following values:

  * ``0`` - Stop asynchronous polling.
  * ``1`` - Start asynchronous polling.

* The ``<events>`` parameter is an optional integer, which is interpreted as a bit field.
  It represents the events to poll for, which can be a combination of ``POLLIN`` and ``POLLOUT``.
  Permanent error and closure events (``POLLERR``, ``POLLHUP``, and ``POLLNVAL``) are always polled.
  The value can be any combination of the following values summed up:

  * ``0`` - Poll the default events.
  * ``1`` - Read events (``POLLIN``) are polled, in addition to the default events.
  * ``4`` - Write events (``POLLOUT``) are polled, in addition to the default events.


Response syntax
~~~~~~~~~~~~~~~

When the asynchronous socket events are enabled, |SM| sends events as URC notifications.

* For ``POLLIN`` events, the URC notification is sent only for the first incoming data on the socket.
  ``AT#XRECV`` or ``AT#XRECVFROM`` command will re-enable the URC notification for the next incoming data.

* For ``POLLOUT`` events, the URC notification is sent only for the first time when the socket is ready for writing.
  ``AT#XSEND`` or ``AT#XSENDTO`` command will re-enable the URC notification for the next time when the socket is ready for writing.

* For ``POLLERR``, ``POLLHUP``, and ``POLLNVAL`` events, the URC notification is sent only once for each socket.
  No further URC notifications will be sent for the same socket.

.. note::
   When closing the socket with ``#XCLOSE``, no closure event will be sent.

::

   #XAPOLL: <handle>,<revents>

* The ``<handle>`` parameter is an integer.
  It is the handle of the socket that has events.

* The ``<revents>`` parameter is an integer, which must be interpreted as a bit field.
  It represents the returned events as a combination of ``POLLIN`` (1), ``POLLOUT`` (4), ``POLLERR`` (8), ``POLLHUP`` (16), and ``POLLNVAL`` (32) summed up.
  Hexadecimal representation is avoided to support AT command parsers that do not support hexadecimal values.

Example
~~~~~~~

::

   // Start asynchronous polling for all sockets with POLLIN and POLLOUT events.
   AT#XAPOLL=,1,5

   OK

   // Create a TCP socket and connect to the test server.
   AT#XSOCKET=1,1,0

   #XSOCKET: 0,1,6

   OK

   AT#XCONNECT=0,"test.server.com",1234

   #XCONNECT: 0,1

   OK

   #XAPOLL: 0,4

   // Send data to the test server, which will echo it back.
   AT#XSEND=0,0,0,"echo"

   #XSEND: 0,0,4

   OK

   #XAPOLL: 0,4

   // Test server sends the data back and closes the connection. POLLIN and POLLHUP events are received.
   #XAPOLL: 0,17

   AT#XRECV=0,0,0,1

   #XRECV: 0,0,4
   echo
   OK

   // Close the TCP socket.
   AT#XCLOSE=0

   #XCLOSE: 0,0

   OK

   // Create a UDP socket and send network acknowledge data to another test server, which does not echo.
   AT#XSOCKET=2,2,0

   #XSOCKET: 0,2,17

   OK

   #XAPOLL: 0,4

   AT#XCONNECT=0,"no_echo.test.server.com",1234

   #XCONNECT: 0,1

   OK

   // Send data to the test server with network acknowledged send flag.
   AT#XSEND=0,0,8192,"no echo"

   #XSEND: 0,1,7

   OK

   // Unsolicited notification for network acknowledged send.
   #XSENDNTF: 0,0,7

   #XAPOLL: 0,4

   // Close the UDP socket.
   AT#XCLOSE=0

   #XCLOSE: 0,0

   OK

   // Stop asynchronous polling for all sockets.
   AT#XAPOLL=,0

   OK

Read command
------------

The read command lists the socket handles with the events that are being polled.

Syntax
~~~~~~

::

   AT#XAPOLL?

Response syntax
~~~~~~~~~~~~~~~

::

   #XAPOLL: <handle>,<events>

* The ``<handle>`` parameter is an integer.
  It is the handle of the socket that is being polled.

* The ``<events>`` parameter is an integer, which must be interpreted as a bit field.
  It represents the events that are being polled, which can be any combination of ``POLLIN`` and ``POLLOUT``.
  Permanent error and closure events (``POLLERR``, ``POLLHUP``, and ``POLLNVAL``) are always polled.
  The value can be any combination of the following values:

  * ``0`` - Poll the default events.
  * ``1`` - Poll read events (``POLLIN``) in addition to the default events.
  * ``4`` - Poll write events (``POLLOUT``) in addition to the default events.


Example
~~~~~~~

::

   // Start asynchronous polling for all sockets with POLLIN event.
   AT#XAPOLL=,1,1

   OK

   // Create socket 0.
   AT#XSOCKET=1,1,0

   #XSOCKET: 0,1,6

   OK

   // Create socket 1 to show that poll events can be configured per socket.
   AT#XSOCKET=1,1,0

   #XSOCKET: 1,1,6

   OK

   // Activate only POLLOUT event polling for socket 1.
   AT#XAPOLL=1,1,4

   OK

   // Create socket 2 to show that POLLIN for all sockets is still in effect.
   AT#XSOCKET=1,1,0

   #XSOCKET: 2,1,6

   OK

   // Read the current poll settings.
   AT#XAPOLL?

   #XAPOLL: 0,1

   #XAPOLL: 1,4

   #XAPOLL: 2,1

   OK

   // Stop asynchronous polling for all sockets.
   AT#XAPOLL=,0

   OK

   // Close all sockets.
   AT#XCLOSE

   #XCLOSE: 0,0

   #XCLOSE: 1,0

   #XCLOSE: 2,0

   OK

Test command
------------

The test command provides information about the command and its parameters.

Syntax
~~~~~~

::

   AT#XAPOLL=?

Response syntax
~~~~~~~~~~~~~~~

::

   #XAPOLL: <handle>,(stop/start),(events)

Example
~~~~~~~

::

   AT#XAPOLL=?

   #XAPOLL: <handle>,(0,1),(0,1,4,5)

   OK

Configure socket receive #XRECVCFG
==================================

The socket receive configuration command allows you to configure the following aspects of a socket:

* Automatic data reception
* Automatic data reception in hex format

Set command
-----------

The set command allows you to configure the socket receive configuration of all sockets or a specific socket.

Syntax
~~~~~~

::

   AT#XRECVCFG=[<handle>],<auto_reception_flags>[,<hex_format>]

* The ``<handle>`` parameter is an integer that identifies the socket handle.
  If omitted, the command applies to all opened sockets, whether already open or opened in the future.
* The ``<auto_reception_flags>`` parameter is an integer that specifies the automatic reception flags.
  It can be a combination of the following values summed up:

  * ``0`` - No automatic data reception.
  * ``1`` - Automatic data reception in AT-command mode.
  * ``2`` - Automatic data reception in data mode.

* The ``<hex_format>`` parameter is an integer that specifies the hex format for automatically received data.
  It applies only when automatic data reception is enabled.
  It can be one of the following values:

  * ``0`` - Data is received in binary format (default).
  * ``1`` - Data is received in hex string format (supported only in AT-command mode).

Response syntax
~~~~~~~~~~~~~~~

When in the AT-command mode and the automatic data reception is enabled, |SM| sends ``#XRECV`` and ``#XRECVFROM`` responses as URC notifications when data is received.
``#XRECV`` is used for TCP, raw, and connected UDP sockets, while ``#XRECVFROM`` is used for unconnected UDP sockets.

.. include:: at_socket.rst
   :start-after: sm_recv_response_start
   :end-before: sm_recv_response_end

.. include:: at_socket.rst
   :start-after: sm_recvfrom_response_start
   :end-before: sm_recvfrom_response_end

.. note::
   ``<CR><LF>`` is sent after ``<data>``.
   This differs from the standard ``#XRECV`` and ``#XRECVFROM`` responses where ``<CR><LF>`` is sent before ``OK``.

When in data mode and the automatic data reception is enabled, |SM| sends the received data as is, without any additional headers or formatting.

Example
~~~~~~~

::

   // Enable automatic data reception in AT-command mode in binary format for all sockets.
   AT#XRECVCFG=,1,0

   OK

   AT#XSOCKET=1,1,0

   #XSOCKET: 0,1,6

   OK

   AT#XCONNECT=0,"test.server.com",1234

   #XCONNECT: 0,1

   OK

   // Send data to the test server, which will echo it back.
   AT#XSEND=0,0,0,"Test"

   #XSEND: 0,0,4

   OK

   // Data is automatically received.
   #XRECV: 0,0,4
   Test

   // Enable automatic data reception in AT-command mode in hex string format for socket 0.
   AT#XRECVCFG=0,1,1

   OK

   // Send data to the test server, which will echo it back.
   AT#XSEND=0,0,0,"Test hex"

   #XSEND: 0,0,8

   OK

   // Data is automatically received in hex string format.
   #XRECV: 0,1,8
   5465737420686578

   // Enable automatic reception of data in data mode.
   AT#XRECVCFG=0,2,0

   OK

   // Enter data mode and send data to the test server, which will echo it back.
   AT#XSEND=0,2,0

   OK
   DATA TEST
   DATA TEST
   +++
   #XDATAMODE: 0

   // Create a new UDP socket. Data reception in AT-command mode in binary format is enabled for it.
   AT#XSOCKET=1,2,0

   #XSOCKET: 1,2,17

   OK

   // Send data with unconnected UDP socket to the test server, which will echo it back with a delay.
   AT#XSENDTO=1,0,0,"test.server.com",1235,"Delayed UDP data"

   #XSENDTO: 1,0,16

   OK

   // Enter data mode with socket 0.
   AT#XSEND=0,2,0

   OK

   // Long operations during data mode.

   // Exiting the data mode allows the delayed UDP data to be received.
   +++
   #XDATAMODE: 0

   // Unconnected UDP socket automatically receives the delayed data with RECVFROM.
   #XRECVFROM: 1,0,16,"111.112.113.114",1235
   Delayed UDP data

   // Disable automatic data reception for all sockets.
   AT#XRECVCFG=,0

   OK

   AT#XCLOSE

   #XCLOSE: 0,0

   #XCLOSE: 1,0

   OK

Read command
------------

The read command allows you to check the receive configuration settings of sockets.

Syntax
~~~~~~

::

   AT#XRECVCFG?

Response syntax
~~~~~~~~~~~~~~~

::

   #XRECVCFG: <handle>,<auto_reception_flags>,<hex_format>

* The ``<handle>`` parameter is an integer that identifies the socket handle.
* The ``<auto_reception_flags>`` parameter is an integer that specifies the automatic reception flags.
  It is a combination of the following values summed up:

  * ``0`` - No automatic data reception.
  * ``1`` - Automatic data reception in AT-command mode.
  * ``2`` - Automatic data reception in data mode.

* The ``<hex_format>`` parameter is an integer that specifies the hex format for automatically received data.
  It can be one of the following values:

  * ``0`` - Data is received in binary format.
  * ``1`` - Data is received in hex string format (supported only in AT-command mode).

Example
~~~~~~~

::

   AT#XRECVCFG=,1,0

   OK
   AT#XSOCKET=1,1,0

   #XSOCKET: 0,1,6

   OK
   AT#XSOCKET=1,1,0

   #XSOCKET: 1,1,6

   OK
   // Enable automatic data reception in data mode for socket 1.
   AT#XRECVCFG=1,2

   OK
   AT#XRECVCFG?

   #XRECVCFG: 0,1,0

   #XRECVCFG: 1,2,0

   OK
   // Disable automatic reception for all sockets.
   AT#XRECVCFG=,0

   OK
   AT#XRECVCFG?

   OK

Test command
------------

The test command provides information about the command and its parameters.

Syntax
~~~~~~

::

   AT#XRECVCFG=?

Response syntax
~~~~~~~~~~~~~~~

::

   #XRECVCFG: <handle>,(0,1,2,3),(0,1)

Example
~~~~~~~

::

   AT#XRECVCFG=?

   #XRECVCFG: <handle>,(0,1,2,3),(0,1)

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

   AT#XGETADDRINFO=<hostname>[,<address_family>]

* The ``<hostname>`` parameter is a string.
* The ``<address_family>`` parameter is an optional integer that gives a hint for DNS query on address family.

  * ``0`` means unspecified address family.
  * ``1`` means IPv4 address family.
  * ``2`` means IPv6 address family.

  If ``<address_family>`` is not specified, there will be no hint given for the DNS query.

Response syntax
~~~~~~~~~~~~~~~

::

   #XGETADDRINFO: "<ip_addresses>"

* The ``<ip_addresses>`` parameter is a string.
  It indicates the IPv4 or IPv6 address of the resolved hostname.

Example
~~~~~~~

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
