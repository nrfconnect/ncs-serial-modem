.. _SM_AT_COAPC:

CoAP client AT commands
***********************

.. contents::
   :local:
   :depth: 2

.. note::

   These AT commands are `Experimental <Software maturity levels_>`_.

This page describes AT commands for the CoAP client.
The CoAP client operates on sockets managed by the :ref:`SM_AT_SOCKET`.
You can perform the following using the Socket AT commands:

* Create a UDP socket with ``AT#XSOCKET`` or ``AT#XSSOCKET``.
* Connect it with ``AT#XCONNECT``.
* Set socket options with ``AT#XSOCKETOPT`` or ``AT#XSSOCKETOPT``.
* Close with ``AT#XCLOSE``.

Only one CoAP request might be active at a time across all sockets.

CoAP request #XCOAPCREQ
=======================

The ``#XCOAPCREQ`` command starts an asynchronous CoAP request on a connected UDP socket.

Set command
-----------

The set command sends a CoAP request and returns ``OK`` immediately.
The server's response is delivered through unsolicited ``#XCOAPCDATA`` and ``#XCOAPCSTAT`` notifications.

Syntax
~~~~~~

::

   AT#XCOAPCREQ=<handle>,<path>,<method>[,<auto_reception>[,<format>[,<confirmable>[,<content_format>[,<payload_len>[,<opt_num_1>,<opt_val_1>[,<opt_num_2>,<opt_val_2>[...]]]]]]]]

* The ``<handle>`` parameter is an integer.
  It identifies the connected socket handle returned by ``AT#XSOCKET`` and connected by ``AT#XCONNECT``.

* The ``<path>`` parameter is a string.
  It specifies the URI-path of the resource, for example ``"/sensors/temperature"``.

* The ``<method>`` parameter must be one of the following values:

  * ``1`` - GET.
  * ``2`` - POST.
  * ``3`` - PUT.
  * ``4`` - DELETE.
  * ``5`` - FETCH.
  * ``6`` - PATCH.
  * ``7`` - iPATCH.

* The ``<auto_reception>`` parameter is an optional integer.
  When omitted, automatic reception is used.
  It can accept the following values:

  * ``1`` - Automatic mode (default).
    Response bytes are forwarded to the host automatically through ``#XCOAPCDATA`` URCs.
  * ``0`` - Manual mode.
    The firmware buffers each response block and emits ``#XCOAPCHEAD``.
    The host must then retrieve the block using ``AT#XCOAPCDATA``.

* The ``<format>`` parameter is an optional integer.
  It controls the encoding used to deliver response payload bytes to the host.
  It can accept the following values:

  * ``0`` - Binary (default).
    Response payload bytes are forwarded to the host as raw binary.
  * ``1`` - Hex string.
    Response payload bytes are encoded as a lowercase ASCII hex string before delivery.
    Each raw byte becomes two hex characters.
    The ``<length>`` field in ``#XCOAPCDATA`` URCs and responses always reports the raw byte count.
    The actual data transmitted to the host is ``2×<length>`` ASCII hex characters.

* The ``<confirmable>`` parameter is an optional integer.
  It can accept the following values:

  * ``0`` - Non-confirmable (NON) message (default).
  * ``1`` - Confirmable (CON) message.

* The ``<content_format>`` parameter is an optional integer.
  It specifies the CoAP Content-Format option number for the request payload (default ``0`` = ``text/plain``).
  Common values:

  * ``0`` - text/plain.
  * ``42`` - application/octet-stream.
  * ``50`` - application/json.
  * ``60`` - application/cbor.

* The ``<payload_len>`` parameter is an optional integer.
  When ``0`` or omitted and no option pairs follow, no request payload is sent and the command returns ``OK`` immediately.
  A positive integer specifies the total payload length in bytes; the command then returns ``OK`` and enters data mode.

  .. note::

     When option pairs follow, ``<payload_len>`` must be written explicitly.
     Use ``0`` if no payload is needed.
     Omitting it shifts the option pairs one position left, which always causes the command to return ``ERROR``.

  The host must send exactly this many bytes as the request payload.
  Data mode exits and ``#XDATAMODE: 0`` is reported when all bytes have been consumed.
  Data mode is the only mechanism for supplying the payload - there is no inline parameter alternative.

  The exact sending behavior depends on payload size:

  * Small payload (≤ ``CONFIG_COAP_CLIENT_BLOCK_SIZE``, default 512 bytes) - All bytes are buffered until data mode exits, then the CoAP request is sent as a single packet.
  * Large payload (> ``CONFIG_COAP_CLIENT_BLOCK_SIZE``) - The CoAP request starts transmitting while data mode is still active.
    As soon as the first full block of ``CONFIG_COAP_CLIENT_BLOCK_SIZE`` bytes has arrived, the first CoAP Block1 packet is sent to the server.
    Each subsequent block is sent as more data arrives, so the server exchange runs concurrently with the host's serial upload.

* The ``<opt_num_X>,<opt_val_X>`` parameters are optional CoAP option pairs:

  * When present, ``<payload_len>`` must also be present as the positional separator before the first pair.
  * ``<opt_num_X>`` is a decimal integer option number.
  * ``<opt_val_X>`` is a quoted string.
  * A value prefixed with ``0x`` or ``0X`` is decoded as raw hex bytes (for example, ``"0x3c"`` → byte ``0x3c``).
  * The hex part after the prefix must be non-empty and even-length, otherwise, the command returns ``ERROR``.
  * Any value without the ``0x`` prefix is used verbatim, including strings that look like hex (for example ``"abcd"`` → 4 ASCII bytes).
  * Common option numbers:

    * ``6`` - Observe (``"0x00"`` = register, ``"0x01"`` = deregister).
    * ``17`` - Accept (content format the client will accept, hex-encoded).
    * ``35`` - Proxy-Uri (full URI passed to a proxy, verbatim string).

  Examples:

  ::

     17,"0x00"                 Accept: text/plain (0x00)
     17,"0x3c"                 Accept: application/cbor (0x3c = 60)
     35,"coap://proxy/res"     Proxy-Uri verbatim string
     6,"0x00",17,"0x3c"        Observe register + Accept cbor

Unsolicited notification
~~~~~~~~~~~~~~~~~~~~~~~~

``#XCOAPCDATA`` is emitted in automatic mode for each received response block.

::

   #XCOAPCDATA: <handle>,<offset>,<length>

The notification line is terminated with ``\r\n``, the response data follows immediately, and a ``\r\n`` terminator follows the data bytes.
In binary mode (default), the data is ``<length>`` raw bytes.
In hex mode (``<format>=1``), the data is ``2×<length>`` lower-case ASCII hex characters.

* The ``<handle>`` parameter is an integer.
  It identifies the socket.
* The ``<offset>`` parameter is an integer.
  It contains the number of response bytes already delivered before this block.
* The ``<length>`` parameter is an integer.
  It contains the raw byte count of the response data in this block.
  In hex mode (``<format>=1``), the payload following the header line is ``2×<length>`` ASCII hex characters.

``#XCOAPCHEAD`` is emitted in manual mode for each received response block.

::

   #XCOAPCHEAD: <handle>,<code>,<block_len>

* The ``<handle>`` parameter is an integer.
  It identifies the socket.
* The ``<code>`` parameter is an integer.
  It contains the CoAP response code for this block.
* The ``<block_len>`` parameter is an integer.
  It contains the raw byte count of the buffered block.
  In hex mode (``<format>=1``), ``AT#XCOAPCDATA`` will deliver ``2×<block_len>`` ASCII hex characters.
  The host must call ``AT#XCOAPCDATA`` to retrieve this block before the next one arrives.

``#XCOAPCSTAT`` is emitted when the request completes, fails, or is cancelled.

::

   #XCOAPCSTAT: <handle>,<status_code>,<total_bytes>

* The ``<handle>`` parameter is an integer.
  It identifies the socket.
* The ``<status_code>`` parameter is an integer.
  It contains the CoAP response code on success, or ``-1`` on failure or cancel.
  CoAP response codes are encoded as ``(class << 5) | detail``.
  For example, ``2.05 Content`` is encoded as ``69`` and ``2.04 Changed`` as ``68``.
* The ``<total_bytes>`` parameter is an integer.
  It contains the total number of raw response payload bytes received.
  In hex mode (``<format>=1``), this is the raw byte count, not the number of hex characters delivered.

.. note::

   Block2 response transfer (server-side fragmentation of large responses) is handled automatically by the CoAP client library.
   All blocks are forwarded to the host in sequence.
   ``#XCOAPCSTAT`` is emitted only after the final block is received and delivered.

Examples
~~~~~~~~

CoAP GET (automatic mode, binary):

::

   AT#XSOCKET=1,2,0
   #XSOCKET: 0,2,17
   OK

   AT#XCONNECT=0,"coap.example.com",5683
   #XCONNECT: 0,1
   OK

   AT#XCOAPCREQ=0,"/sensors/temperature",1
   OK

   #XCOAPCDATA: 0,0,12
   23.5 Celsius

   #XCOAPCSTAT: 0,69,12

   AT#XCLOSE=0
   OK

.. note::

   In automatic mode, a ``\r\n`` terminator is appended after the data bytes of each ``#XCOAPCDATA`` notification.

CoAP GET (automatic mode, hex-encoded response, ``format=1``):

::

   AT#XCOAPCREQ=0,"/sensors/temperature",1,1,1
   OK

   #XCOAPCDATA: 0,0,12
   32332e352043656c73697573

   #XCOAPCSTAT: 0,69,12

.. note::

   The hex string and the line break after it represent the ``2×<length>`` hex characters followed by the ``\r\n`` terminator.

CoAP GET (manual mode):

::

   AT#XCOAPCREQ=0,"/sensors/temperature",1,0
   OK

   #XCOAPCHEAD: 0,69,12

   AT#XCOAPCDATA=0
   #XCOAPCDATA: 0,0,12
   23.5 Celsius
   OK

   #XCOAPCSTAT: 0,69,12

CoAP POST with JSON payload (``content_format=50`` = ``application/json``):

::

   AT#XCOAPCREQ=0,"/data",2,1,0,0,50,13
   OK
   {"value": 42}
   #XDATAMODE: 0

   #XCOAPCSTAT: 0,68,0

CoAP GET through proxy with Proxy-URI option (option 35, value is a plain URI string):

::

   AT#XCOAPCREQ=0,"proxy",1,1,0,0,0,0,35,"coap://remote-host/resource"
   OK

   #XCOAPCDATA: 0,0,128
   <128 bytes>

   #XCOAPCSTAT: 0,69,128

CoAP GET with Observe and Accept options (option 6 = Observe register ``"0x00"``, option 17 = Accept ``"0x3c"`` = 0x3c = application/cbor):

::

   AT#XCOAPCREQ=0,"/obs",1,1,0,1,0,0,6,"0x00",17,"0x3c"
   OK

   #XCOAPCDATA: 0,0,18
   <18 bytes CBOR>

   #XCOAPCSTAT: 0,69,18

Test command
------------

The test command tests the existence of the command and provides information about the type of its subparameters.

Syntax
~~~~~~

::

   AT#XCOAPCREQ=?

Response syntax
~~~~~~~~~~~~~~~

::

   #XCOAPCREQ: <handle>,<path>,<method>[,<auto_reception>[,<format>[,<confirmable>[,<content_format>[,<payload_len>[,<opt_num>,<opt_val>...]]]]]]

Example
~~~~~~~

::

   AT#XCOAPCREQ=?
   #XCOAPCREQ: <handle>,<path>,<method>[,<auto_reception>[,<format>[,<confirmable>[,<content_format>[,<payload_len>[,<opt_num>,<opt_val>...]]]]]]
   OK

CoAP data pull #XCOAPCDATA
==========================

The ``#XCOAPCDATA`` command pulls a buffered response block for a manual-mode CoAP request.

Set command
-----------

The set command delivers one buffered response block to the host and unblocks the CoAP client to request the next Block2 block, if any.

Syntax
~~~~~~

::

   AT#XCOAPCDATA=<handle>[,<length>]

* The ``<handle>`` parameter is an integer.
  It identifies the socket used when starting the manual-mode request.

* The ``<length>`` parameter is an optional integer.
  It specifies the maximum number of bytes to return in this pull.
  When omitted, the full block buffer size (``CONFIG_COAP_CLIENT_BLOCK_SIZE``, default 512 bytes) is used.

Response syntax
~~~~~~~~~~~~~~~

When a block is available:

::

   #XCOAPCDATA: <handle>,<offset>,<length>
   <data>
   OK

In binary mode (default), ``<data>`` is ``<length>`` raw bytes.
In hex mode (``<format>=1``), ``<data>`` is ``2×<length>`` lower-case ASCII hex characters.

The ``#XCOAPCDATA:`` line is terminated with ``\r\n``.
``<length>`` raw response bytes follow immediately with no additional separator.
``OK`` follows on its own line after the response bytes.

When no block has been buffered yet (the firmware is still waiting for the server response).

::

   #XCOAPCDATA: <handle>,<offset>,<length>
   OK

* The ``<handle>`` parameter is an integer.
  It identifies the socket.
* The ``<offset>`` parameter is an integer.
  It contains the number of response bytes already delivered before this pull.
* The ``<length>`` parameter is an integer.
  It contains the number of response bytes delivered in this pull.
  A value of ``0`` means no block is ready yet.

After the last block is drained, ``#XCOAPCSTAT`` is emitted as a URC after the final ``OK``.

.. note::

   If ``AT#XCOAPCDATA`` is not called within 30 seconds of a ``#XCOAPCHEAD`` notification, the request is aborted and ``#XCOAPCSTAT: <handle>,-1,<total_bytes>`` is emitted.

Example
~~~~~~~

::

   AT#XCOAPCREQ=0,"/large",1,0
   OK

   #XCOAPCHEAD: 0,69,512

   AT#XCOAPCDATA=0,512
   #XCOAPCDATA: 0,0,512
   <512 bytes>
   OK

   #XCOAPCHEAD: 0,69,512

   AT#XCOAPCDATA=0,512
   #XCOAPCDATA: 0,512,512
   <512 bytes>
   OK

   #XCOAPCSTAT: 0,69,1024

Test command
------------

The test command tests the existence of the command and provides information about the type of its subparameters.

Syntax
~~~~~~

::

   AT#XCOAPCDATA=?

Response syntax
~~~~~~~~~~~~~~~

::

   #XCOAPCDATA: <handle>[,<length>]

Example
~~~~~~~

::

   AT#XCOAPCDATA=?
   #XCOAPCDATA: <handle>[,<length>]
   OK

CoAP request cancel #XCOAPCCANCEL
==================================

The ``#XCOAPCCANCEL`` command cancels the active CoAP request.

Set command
-----------

The set command cancels the currently active CoAP request.

Syntax
~~~~~~

::

   AT#XCOAPCCANCEL=<handle>

* The ``<handle>`` parameter is an integer.
  It must match the socket handle that was passed to ``AT#XCOAPCREQ`` when the request was started.
  This acts as an ownership assertion: the command returns ``ERROR`` if no request is active or if the active request belongs to a different handle.

.. note::

   Only one CoAP request can be active at a time.
   The ``<handle>`` check is an ownership guard, not a per-socket filter.
   Cancellation always applies to the single in-flight request.

An unsolicited ``#XCOAPCSTAT: <handle>,-1,<total_bytes>`` notification is emitted after cancellation, where ``<total_bytes>`` is the number of response bytes already delivered to the host.

Example
~~~~~~~

::

   AT#XCOAPCCANCEL=0
   OK
   #XCOAPCSTAT: 0,-1,0

Test command
------------

The test command tests the existence of the command and provides information about the type of its subparameters.

Syntax
~~~~~~

::

   AT#XCOAPCCANCEL=?

Response syntax
~~~~~~~~~~~~~~~

::

   #XCOAPCCANCEL: <handle>

Example
~~~~~~~

::

   AT#XCOAPCCANCEL=?
   #XCOAPCCANCEL: <handle>
   OK
