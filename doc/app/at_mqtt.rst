.. _SM_AT_MQTT:

MQTT client AT commands
***********************

.. contents::
   :local:
   :depth: 1

This page describes the AT commands used to operate the MQTT client.

.. note::

   The MQTT client holds no internal state information for publish and subscript.
   It will not retransmit packets or guarantee packet delivery according to QoS classes.
   This deviates from MQTT v3.1.1.

.. _SM_AT_MQTT_XMQTTEVT:

MQTT event notification #XMQTTEVT
=================================

MQTT client AT commands report protocol events as unsolicited notifications:

Unsolicited notification
------------------------

::

   #XMQTTEVT: <evt_type>,<result>

The parameters and their defined values are the following:

<evt_type>
   Integer.
   The MQTT event type:

   * ``0`` - Connection acknowledgment (CONNACK).
   * ``1`` - Disconnection (DISCONNECT).
     The MQTT client is disconnected from the MQTT broker when the |SM| sends this event.
     If the host receives this event without a preceding ``AT#XMQTTCON=0`` command, the host must issue ``AT#XMQTTCON=0`` before connecting again.
   * ``2`` - Incoming publish (PUBLISH).
     A subscribed topic received a message.
     The |SM| delivers the payload in a separate ``#XMQTTMSG`` notification.
     See :ref:`SM_AT_MQTT_XMQTTMSG`.
   * ``3`` - Publish acknowledgment (PUBACK), QoS 1.
   * ``4`` - Publish received (PUBREC), QoS 2.
   * ``5`` - Publish release (PUBREL), QoS 2.
   * ``6`` - Publish complete (PUBCOMP), QoS 2.
   * ``7`` - Subscribe acknowledgment (SUBACK).
   * ``8`` - Unsubscribe acknowledgment (UNSUBACK).
   * ``9`` - Ping response (PINGRESP).
     The |SM| can send this event at any time while connected, when the keep alive interval expires and the client pings the broker.

<result>
   * ``0`` - Success.
   * *Negative value* - Error code indicating the reason for the failure.

The commands describe which ``<evt_type>`` values each command can trigger.
All notifications use the syntax documented in this section.

MQTT configure #XMQTTCFG
========================

The ``#XMQTTCFG`` command allows you to configure the MQTT client before connecting to a broker.

Set command
-----------

The set command allows you to configure the MQTT client.

Syntax
~~~~~~

::

   AT#XMQTTCFG=<client_id>[,<keep_alive>[,<clean_session>]]

The parameters and their defined values are the following:

<client_id>
   String.
   The MQTT Client ID.
   If this command is not issued, |SM| uses the default value of ``sm_default_client_id``.

<keep_alive>
   Integer.
   The maximum Keep Alive time in seconds for MQTT.
   The default Keep Alive time is 60 seconds.

<clean_session>
   * ``0`` - Connect to a MQTT broker using a persistent session.
   * ``1`` - Connect to a MQTT broker using a clean session.

   The default is using a persistent session.

Examples
~~~~~~~~

::

   AT#XMQTTCFG="MyMQTT-Client-ID",300,1
   OK

Read command
------------

The read command shows MQTT client configuration information.

Syntax
~~~~~~

::

   AT#XMQTTCFG?

Response syntax
~~~~~~~~~~~~~~~

::

   #XMQTTCFG: <client_id>,<keep_alive>,<clean_session>

The parameters and their defined values are the following:

<client_id>
   String.
   The MQTT Client ID.

<keep_alive>
   Integer.
   The maximum Keep Alive time in seconds for MQTT.

<clean_session>
   * ``0`` - Connect to a MQTT broker using a persistent session.
   * ``1`` - Connect to a MQTT broker using a clean session.

Examples
~~~~~~~~

::

   AT#XMQTTCFG?
   #XMQTTCFG: "MyMQTT-Client-ID",60,0
   OK

Test command
------------

The test command tests the existence of the command and provides information about the type of its subparameters.

Syntax
~~~~~~

::

   AT#XMQTTCFG=?

Response syntax
~~~~~~~~~~~~~~~

::

   #XMQTTCFG: <client_id>,<keep_alive>,<clean_session>

Examples
~~~~~~~~

::

   AT#XMQTTCFG=?
   #XMQTTCFG: <client_id>,,<keep_alive>,<clean_session>
   OK


MQTT connect #XMQTTCON
======================

The ``#XMQTTCON`` command allows you to connect to and disconnect from the MQTT broker.

This command can send ``#XMQTTEVT`` with the following ``<evt_type>`` values:

* ``0`` - After a successful connect request (``<op>`` ``1`` or ``2``).
* ``1`` - After a disconnect request (``<op>`` ``0``), when the broker closes the connection, or when the client disconnects for other reasons.
* ``9`` - While connected, when the broker responds to a keep alive ping.

See :ref:`SM_AT_MQTT_XMQTTEVT` for the notification syntax and parameter descriptions.

Set command
-----------

The set command allows you to connect to and disconnect from the MQTT broker.

.. note::

   The ``#XMQTTCON`` command uses default PDN connection with ID ``0``.
   Raw sockets must not use the PDN connection at the same time.
   See :ref:`SM_AT_SOCKET_RAW_SOCKET_LIMITATION` for more information.

Syntax
~~~~~~

::

   AT#XMQTTCON=<op>[,<username>,<password>,<url>,<port>[,<sec_tag>]]

The parameters and their defined values are the following:

<op>
   * ``0`` - Disconnect from the MQTT broker.
   * ``1`` - Connect to the MQTT broker using IP protocol family version 4.
   * ``2`` - Connect to the MQTT broker using IP protocol family version 6.

<username>
   String.
   The MQTT client username.

<password>
   String.
   The MQTT client password in cleartext.

<url>
   String.
   The MQTT broker hostname.

<port>
   Integer.
   An unsigned 16-bit integer (0 - 65535) indicating the MQTT broker port.

<sec_tag>
   Integer.
   The credential of the security tag used for establishing a secure connection.

Examples
~~~~~~~~

::

   AT#XMQTTCFG="MyMQTT-Client-ID",300,1
   OK

   AT#XMQTTCON=1,"","","mqtt.server.com",1883
   OK
   #XMQTTEVT: 0,0

   Keep alive expires and broker responds to our ping:
   #XMQTTEVT: 9,0

::

   AT#XMQTTCON=0
   OK
   #XMQTTEVT: 1,0

When the MQTT broker closes the connection:

::

   #XMQTTEVT: 1,0

   AT#XMQTTCON=0
   OK

Read command
------------

The read command shows MQTT client information.

Syntax
~~~~~~

::

   AT#XMQTTCON?

Response syntax
~~~~~~~~~~~~~~~

::

   #XMQTTCON: <status>[,<client_id>,<url>,<port>[,<sec_tag>]]

The parameters and their defined values are the following:

<status>
   * ``0`` - MQTT is not connected.
   * ``1`` - MQTT is connected.

<client_id>
   String.
   The MQTT client ID.

<url>
   String.
   The MQTT broker hostname.
   Present only when ``<status>`` is ``1``.

<port>
   Integer.
   An unsigned 16-bit integer (0 - 65535) indicating the MQTT broker port.
   Present only when ``<status>`` is ``1``.

<sec_tag>
   Integer.
   The credential of the security tag used for establishing a secure connection.
   Present only when ``<status>`` is ``1``.

Examples
~~~~~~~~

::

   AT#XMQTTCON?
   #XMQTTCON: 1,"","","mqtt.server.com",1883
   OK

Test command
------------

The test command tests the existence of the command and provides information about the type of its subparameters.

Syntax
~~~~~~

::

   #XMQTTCON=?

Response syntax
~~~~~~~~~~~~~~~

::

   #XMQTTCON: (list of op),<username>,<password>,<url>,<port>,<sec_tag>

Examples
~~~~~~~~

::

   AT#XMQTTCON=?
   #XMQTTCON: (0,1,2),<username>,<password>,<url>,<port>,<sec_tag>
   OK

.. _SM_AT_MQTT_XMQTTMSG:

Incoming message notification #XMQTTMSG
=======================================

When the MQTT client has successfully subscribed to a topic using :ref:`SM_AT_MQTT_XMQTTSUB` and a message is published on that topic, the |SM| sends ``#XMQTTMSG`` unsolicited notification.

Unsolicited notification
------------------------

::

   #XMQTTMSG: <topic_length>,<message_length>
   <topic_received>
   <message>

The parameters and their defined values are the following:

<topic_length>
   Integer.
   The length of the ``<topic_received>`` field.

<message_length>
   Integer.
   The length of the ``<message>`` field.

<topic_received>
   String.
   The topic that received the message.

<message>
   Raw payload bytes.
   The message received from the topic, forwarded without conversion or escaping.
   Read exactly ``<message_length>`` bytes, as the payload can contain NUL bytes and line breaks.

.. _SM_AT_MQTT_XMQTTSUB:

MQTT subscribe #XMQTTSUB
========================

The ``#XMQTTSUB`` command allows you to subscribe to an MQTT topic.

On successful subscription, the |SM| sends ``#XMQTTEVT`` with ``<evt_type>`` ``7`` (SUBACK).

When a message is published on a subscribed topic, the |SM| sends ``#XMQTTMSG`` and ``#XMQTTEVT`` with ``<evt_type>`` ``2`` (PUBLISH).
For incoming QoS 2 messages, the |SM| also sends ``#XMQTTEVT`` with ``<evt_type>`` ``5`` (PUBREL).

See :ref:`SM_AT_MQTT_XMQTTEVT` and :ref:`SM_AT_MQTT_XMQTTMSG` for the notification syntax and parameter descriptions.

Set command
-----------

The set command allows you to subscribe to an MQTT topic.

Syntax
~~~~~~

::

   AT#XMQTTSUB=<topic>,<qos>

The parameters and their defined values are the following:

<topic>
   String.
   The topic to subscribe to.

<qos>
   The MQTT Quality of Service type.

   * ``0`` - Lowest Quality of Service.
     No acknowledgment of the reception is needed for the published message.
   * ``1`` - Medium Quality of Service.
     If the acknowledgment of the reception is expected for the published message, publishing duplicate messages is permitted.
   * ``2`` - Highest Quality of Service.
     The acknowledgment of the reception is expected and the message should be published only once.

Examples
~~~~~~~~

::

   AT#XMQTTSUB="nrf91/sm/mqtt/topic0",0
   OK
   #XMQTTEVT: 7,0

   Message with QoS0 is received:
   #XMQTTMSG: 21,7
   nrf91/sm/mqtt/topic0
   message
   #XMQTTEVT: 2,0

::

   AT#XMQTTSUB="nrf91/sm/mqtt/topic1",1
   OK
   #XMQTTEVT: 7,0

   Message with QoS1 is received:
   #XMQTTMSG: 21,7
   nrf91/sm/mqtt/topic1
   message

   #XMQTTEVT: 2,0

::

   AT#XMQTTSUB="nrf91/sm/mqtt/topic2",2
   OK
   #XMQTTEVT: 7,0

   Message with QoS2 is received:
   #XMQTTMSG: 21,7
   nrf91/sm/mqtt/topic2
   message

   #XMQTTEVT: 2,0

   #XMQTTEVT: 5,0

Read command
------------

The read command is not supported.

Test command
------------

The test command is not supported.

MQTT unsubscribe #XMQTTUNSUB
============================

The ``#XMQTTUNSUB`` command allows you to unsubscribe from an MQTT topic.

On successful unsubscription, the |SM| sends ``#XMQTTEVT`` with ``<evt_type>`` ``8`` (UNSUBACK).

See :ref:`SM_AT_MQTT_XMQTTEVT` for the notification syntax and parameter descriptions.

Set command
-----------

The set command allows you to unsubscribe from an MQTT topic.

Syntax
~~~~~~

::

   AT#XMQTTUNSUB=<topic>


The parameters and their defined values are the following:

<topic>
   String.
   The topic to unsubscribe from.

Examples
~~~~~~~~

::

   AT#XMQTTUNSUB="nrf91/sm/mqtt/topic0"
   OK
   #XMQTTEVT: 8,0

Read command
------------

The read command is not supported.

Test command
------------

The test command is not supported.

MQTT publish #XMQTTPUB
======================

The ``#XMQTTPUB`` command allows you to publish messages on MQTT topics.

Depending on the QoS level, this command can send ``#XMQTTEVT`` with the following ``<evt_type>`` values:

* ``3`` - QoS 1 (PUBACK).
* ``4`` and ``6`` - QoS 2 (PUBREC, then PUBCOMP).

QoS ``0`` does not produce an ``#XMQTTEVT`` notification.

See :ref:`SM_AT_MQTT_XMQTTEVT` for the notification syntax and parameter descriptions.

Set command
-----------

The set command allows you to publish messages on MQTT topics.

Syntax
~~~~~~

::

   AT#XMQTTPUB=<topic>[,<msg>[,<qos>[,<retain>[,<data_len>]]]]


The parameters and their defined values are the following:

<topic>
   String.
   The topic on which data is published.

<msg>
   String.
   The payload on the topic being published.
   If the payload is empty (for example, ``""``), |SM| enters :ref:`sm_data_mode`.

<qos>
   * ``0`` - Lowest Quality of Service (default value).
     No acknowledgment of the reception is needed for the published message.
   * ``1`` - Medium Quality of Service.
     If the acknowledgment of the reception is expected for the published message, publishing duplicate messages is permitted.
   * ``2`` - Highest Quality of Service.
     The acknowledgment of the reception is expected and the message should be published only once.

<retain>
   Integer.
   Its default value is ``0``.
   When ``1``, it indicates that the broker should store the message persistently.

<data_len>
   Integer.
   Optional, only used when ``<msg>`` is empty (data mode).
   Sets the number of bytes of payload to publish in data mode.
   When the required number of bytes are received, the payload is published and the data mode is exited.

   .. only:: not nrf91m1

      The termination command :ref:`CONFIG_SM_DATAMODE_TERMINATOR <CONFIG_SM_DATAMODE_TERMINATOR>` is not used in this case.
      The value must not exceed the value configured in the :ref:`CONFIG_SM_DATAMODE_BUF_SIZE <CONFIG_SM_DATAMODE_BUF_SIZE>` Kconfig option, as the payload must fit within the data mode buffer to be published as a single message.

   .. only:: nrf91m1

      The termination command ``+++`` is not used in this case.
      The value must not exceed 4096 bytes, as the payload must fit within the data mode buffer to be published as a single message.

   The value ``0`` is equivalent to omitting the parameter.
   Specifying a non-zero ``<data_len>`` together with a non-empty ``<msg>`` results in an error.

Examples
~~~~~~~~

::

   AT#XMQTTPUB="nrf91/sm/mqtt/topic0","Test message with QoS 0",0,0
   OK

::

   AT#XMQTTPUB="nrf91/sm/mqtt/topic0"
   OK
   {"msg":"Test Json publish"}+++
   #XDATAMODE: 0

::

   AT#XMQTTPUB="nrf91/sm/mqtt/topic0","",0,0,23
   OK
   Test message, 23 bytes.
   #XDATAMODE: 0

::

   AT#XMQTTPUB="nrf91/sm/mqtt/topic1","Test message with QoS 1",1,0
   OK
   #XMQTTEVT: 3,0

::

   AT#XMQTTPUB="nrf91/sm/mqtt/topic2","",2,0
   OK
   Test message with QoS 2+++
   #XDATAMODE: 0
   #XMQTTEVT: 4,0
   #XMQTTEVT: 6,0

Read command
------------

The read command is not supported.

Test command
------------

The test command is not supported.
