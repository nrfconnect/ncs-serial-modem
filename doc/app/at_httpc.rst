.. _SM_AT_HTTPC:

HTTP client AT commands
***********************

.. contents::
   :local:
   :depth: 2

.. note::

   These AT commands are `Experimental <Software maturity levels_>`_.

This page describes AT commands for the HTTP client.
The HTTP client operates on sockets managed by the :ref:`SM_AT_SOCKET`.
You can perform the following using the Socket AT commands:

* Create a socket with ``AT#XSOCKET`` or ``AT#XSSOCKET``.
* Connect it with ``AT#XCONNECT``.
* Set socket options with ``AT#XSOCKETOPT`` or ``AT#XSSOCKETOPT``.
* Close with ``AT#XCLOSE``.

HTTP request #XHTTPCREQ
=======================

The ``#XHTTPCREQ`` command starts an asynchronous HTTP or HTTPS request on a socket connected with ``AT#XCONNECT``.

Set command
-----------

The set command starts an HTTP or HTTPS request.

Syntax
~~~~~~

::

   AT#XHTTPCREQ=<socket_fd>,<url>,<method>[,<auto_reception>[,<body_len>[,<header 1>[,<header 2>[...]]]]]

* The ``<socket_fd>`` parameter is an integer.
  It identifies the connected socket file descriptor returned by ``AT#XSOCKET`` or ``AT#XSSOCKET``.

* The ``<url>`` parameter is a string.
  It specifies the full URL of the request, for example, ``http://host/path`` or ``https://host/path``.

* The ``<method>`` parameter must be one of the following values:

  * ``0`` - GET.
  * ``1`` - POST.
  * ``2`` - PUT.
  * ``3`` - DELETE.
  * ``4`` - HEAD.

* The ``<auto_reception>`` parameter is an optional integer.
  When omitted, automatic reception is used.
  It can accept the following values:

  * ``1`` - Automatic mode (default).
    Response body bytes are received and forwarded to the host automatically.
  * ``0`` - Manual mode.
    Response body reception is paused after the headers are parsed and ``#XHTTPCHEAD`` is emitted.
    The host must then retrieve body data in chunks using ``AT#XHTTPCDATA`` commands.

* The ``<body_len>`` parameter is an optional integer.
  It is required as a placeholder when ``<header>`` parameters follow.
  It can accept the following values:

  * ``0`` - No request body.
  * Positive integer - Length of the request body in bytes.
    Valid for POST and PUT only.
    When set, the HTTP request headers are sent to the server immediately when the AT command
    is processed.
    The command then responds with ``OK`` and enters data mode.
    Body bytes are forwarded to the server as they are received from the host in data mode.
    The host must send exactly this many bytes as the request body.
    Data mode exits and ``#XDATAMODE: 0`` is reported when all bytes have been processed.

* The ``<header X>`` parameter is an optional string.
  It specifies an additional HTTP request header.
  Empty parameters between headers (``<header 1>``, ``<header 2>``,, ``<header 3>``, ...) are ignored.

.. note::

   The HTTP client uses HTTP/1.1 and relies on the protocol default of persistent (keep-alive)
   connections.
   Multiple sequential requests can therefore be made on the same connected socket without
   reconnecting.
   To close the connection after a response, pass ``"Connection: close"`` as a custom header.

Response syntax
~~~~~~~~~~~~~~~

::

   #XHTTPCREQ: <socket_fd>
   OK

* The ``<socket_fd>`` parameter is an integer.
  It identifies the socket.

Unsolicited notification
~~~~~~~~~~~~~~~~~~~~~~~~

``#XHTTPCHEAD`` is emitted when response headers have been parsed::

   #XHTTPCHEAD: <socket_fd>,<status_code>,<content_length>

* The ``<socket_fd>`` parameter is an integer.
  It identifies the socket.
* The ``<status_code>`` parameter is an integer.
  It contains the HTTP status code returned by the server.
* The ``<content_length>`` parameter is an integer.
  It contains the value of the ``Content-Length`` response header, or ``-1`` when no such header is present.

``#XHTTPCDATA`` is emitted in automatic mode for each received body chunk::

   #XHTTPCDATA: <socket_fd>,<offset>,<length>

The notification line is terminated with ``\r\n`` and the raw body bytes follow immediately with no additional separator.

* The ``<socket_fd>`` parameter is an integer.
  It identifies the socket.
* The ``<offset>`` parameter is an integer.
  It contains the number of body bytes already delivered before this chunk.
* The ``<length>`` parameter is an integer.
  It contains the number of body bytes in this chunk.

``#XHTTPCSTAT`` is emitted when the request completes, fails, or is cancelled::

   #XHTTPCSTAT: <socket_fd>,<status_code>,<total_bytes>

* The ``<socket_fd>`` parameter is an integer.
  It identifies the socket.
* The ``<status_code>`` parameter is an integer.
  It contains the HTTP status code on success, or ``-1`` on failure, cancel, or timeout.
* The ``<total_bytes>`` parameter is an integer.
  It contains the total number of body bytes received.

.. note::

   For responses that carry no body by definition — ``HEAD`` requests, ``1xx``,
   ``204 No Content``, and ``304 Not Modified`` — ``#XHTTPCHEAD`` is emitted immediately
   followed by ``#XHTTPCSTAT``.  No ``#XHTTPCDATA`` notification is generated.

Examples
~~~~~~~~

The following examples demonstrate how to perform an HTTP GET request and receive the response in automatic or manual mode on a connected socket.

HTTP GET (automatic mode):

::

   AT#XHTTPCREQ=0,<url>,0
   #XHTTPCREQ: 0
   OK

   #XHTTPCHEAD: 0,200,261

   #XHTTPCDATA: 0,0,261
   <261 bytes>

   #XHTTPCSTAT: 0,200,261

HTTP GET (manual mode):

::

   AT#XHTTPCREQ=0,<url>,0,0
   #XHTTPCREQ: 0
   OK

   #XHTTPCHEAD: 0,200,261

   AT#XHTTPCDATA=0
   #XHTTPCDATA: 0,0,261
   <261 bytes>
   OK

   #XHTTPCSTAT: 0,200,261

HTTP POST with JSON body and custom header:

::

   AT#XHTTPCREQ=0,<url>,1,1,15,"Content-Type: application/json"
   #XHTTPCREQ: 0
   OK
   {"key":"value"}
   #XDATAMODE: 0

   #XHTTPCHEAD: 0,200,432

   #XHTTPCDATA: 0,0,432
   <432 bytes>

   #XHTTPCSTAT: 0,200,432

HTTP GET with Range header (``body_len=0`` is required as a placeholder when headers follow a GET):

::

   AT#XHTTPCREQ=0,<url>,0,1,0,"Range: bytes=0-127"
   #XHTTPCREQ: 0
   OK

   #XHTTPCHEAD: 0,206,128

   #XHTTPCDATA: 0,0,128
   <128 bytes>

   #XHTTPCSTAT: 0,206,128

   AT#XHTTPCREQ=0,<url>,0,1,0,"Range: bytes=128-255"
   #XHTTPCREQ: 0
   OK

   #XHTTPCHEAD: 0,206,128

   #XHTTPCDATA: 0,0,128
   <128 bytes>

   #XHTTPCSTAT: 0,206,128


HTTP HEAD (no body — ``#XHTTPCSTAT`` follows immediately after ``#XHTTPCHEAD``):

::

   AT#XHTTPCREQ=0,<url>,4
   #XHTTPCREQ: 0
   OK

   #XHTTPCHEAD: 0,200,261

   #XHTTPCSTAT: 0,200,0

Test command
------------

The test command tests the existence of the command and provides information about the type of its subparameters.

Syntax
~~~~~~

::

   AT#XHTTPCREQ=?

Response syntax
~~~~~~~~~~~~~~~

::

   #XHTTPCREQ: <socket_fd>,<url>,<method>[,<auto_reception>[,<body_len>[,<header>]...]]

Example
~~~~~~~

::

   AT#XHTTPCREQ=?
   #XHTTPCREQ: <socket_fd>,<url>,<method>[,<auto_reception>[,<body_len>[,<header>]...]]
   OK

HTTP data pull #XHTTPCDATA
==========================

The ``#XHTTPCDATA`` command pulls the next body chunk for a manual-mode HTTP request.

Set command
-----------

The set command performs one non-blocking receive on the socket of a manual-mode request and forwards the available data to the host.

Syntax
~~~~~~

::

   AT#XHTTPCDATA=<socket_fd>[,<length>]

* The ``<socket_fd>`` parameter is an integer.
  It identifies the socket used when starting the manual-mode request.

* The ``<length>`` parameter is an optional integer.
  It specifies the maximum number of bytes to receive in this pull.
  When omitted, the full internal receive buffer size is used.

Response syntax
~~~~~~~~~~~~~~~

When body data is available::

   #XHTTPCDATA: <socket_fd>,<offset>,<length>
   <length bytes of raw body data>
   OK

The ``#XHTTPCDATA:`` line is terminated with ``\r\n``.
``<length>`` raw body bytes follow immediately with no additional separator.
``OK`` follows on its own line after the body bytes.

When the socket buffer is temporarily empty (EAGAIN)::

   #XHTTPCDATA: <socket_fd>,<offset>,0
   OK

* The ``<socket_fd>`` parameter is an integer.
  It identifies the socket.
* The ``<offset>`` parameter is an integer.
  It contains the number of body bytes already delivered before this pull.
* The ``<length>`` parameter is an integer.
  It contains the number of body bytes delivered in this pull.
  A value of ``0`` means the socket buffer is currently empty.

When all body bytes have been delivered, ``#XHTTPCSTAT`` is sent as a URC after
the final ``OK``. This happens either when the server closes the connection, or
when the ``Content-Length`` bytes have all been forwarded.

.. note::

   Retry the pull after a brief delay when ``#XHTTPCDATA: <socket_fd>,<offset>,0`` is received.
   Use ``AT#XHTTPCCANCEL=<socket_fd>`` to cancel if no more data is needed.

Example
~~~~~~~

::

   AT#XHTTPCDATA=0,256
   #XHTTPCDATA: 0,0,256
   <256 bytes>
   OK

   AT#XHTTPCDATA=0,256
   #XHTTPCDATA: 0,256,256
   <256 bytes>
   OK

   #XHTTPCSTAT: 0,200,512

Test command
------------

The test command tests the existence of the command and provides information about the type of its subparameters.

Syntax
~~~~~~

::

   AT#XHTTPCDATA=?

Response syntax
~~~~~~~~~~~~~~~

::

   #XHTTPCDATA: <socket_fd>[,<length>]

Example
~~~~~~~

::

   AT#XHTTPCDATA=?
   #XHTTPCDATA: <socket_fd>[,<length>]
   OK

HTTP request cancel #XHTTPCCANCEL
=================================

The ``#XHTTPCCANCEL`` command cancels an active HTTP request.

Set command
-----------

The set command cancels an active request.

Syntax
~~~~~~

::

   AT#XHTTPCCANCEL=<socket_fd>

* The ``<socket_fd>`` parameter is an integer.
  It identifies the socket of the request to cancel.

An unsolicited ``#XHTTPCSTAT: <socket_fd>,-1,<total_bytes>`` notification is emitted after cancellation.

Example
~~~~~~~

::

   AT#XHTTPCCANCEL=0
   OK
   #XHTTPCSTAT: 0,-1,0

Test command
------------

The test command tests the existence of the command and provides information about the type of its subparameters.

Syntax
~~~~~~

::

   AT#XHTTPCCANCEL=?

Response syntax
~~~~~~~~~~~~~~~

::

   #XHTTPCCANCEL: <socket_fd>

Example
~~~~~~~

::

   AT#XHTTPCCANCEL=?
   #XHTTPCCANCEL: <socket_fd>
   OK
