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

Response syntax
~~~~~~~~~~~~~~~

::

   #XMQTTEVT: <evt_type>,<result>

The parameters and their defined values are the following:

<evt_type>
   Integer.
   The type of the event.
   It can return the following values for the ``#XMQTTCON`` command:

   * ``0`` - Acknowledgment of connection request (CONNACK).
   * ``1`` - Disconnection notification (DISCONNECT).
     The MQTT client is disconnected from the MQTT broker once this event is notified.

<result>
   * ``0`` - Success.
   * *Negative value* - Error code indicating the reason for the failure.


Unsolicited notification
------------------------

::

   #XMQTTEVT=<evt_type>,<result>

The parameters and their defined values are the following:

<evt_type>
   Integer.
   The type of the event.
   After the connection is established, it can return ``9`` to indicate a ping response from the MQTT broker (PINGRESP).
   This is received when pinging (PINGREQ) the broker after the keep alive is reached.

<result>
   * ``0`` - Success.
   * *Negative value* - Error code indicating the reason for the failure.

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

MQTT subscribe #XMQTTSUB
========================

The ``#XMQTTSUB`` command allows you to subscribe to an MQTT topic.

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

Response syntax
~~~~~~~~~~~~~~~

::

   #XMQTTEVT: <evt_type>,<result>

The parameters and their defined values are the following:

<evt_type>
   * ``7`` - Acknowledgment of the subscribe request (SUBACK).

<result>
   * ``0`` - Success.
   * *Negative value* - Error code indicating the reason for the failure.

Unsolicited notifications
~~~~~~~~~~~~~~~~~~~~~~~~~

When the MQTT client has successfully subscribed to a topic and a message is published with the topic, the following unsolicited notifications are received:

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
   String or HEX.
   The message received from the topic.

::

   #XMQTTEVT: <evt_type>,<result>

The parameters and their defined values are the following:

<evt_type>
   * ``2`` - Message received on a topic the client is subscribed to (PUBLISH).
   * ``5`` - Release of a published message with QoS 2 (PUBREL).

<result>
   * ``0`` - Success.
   * *Negative value* - Error code indicating the reason for the failure.


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

Response syntax
~~~~~~~~~~~~~~~

::

   #XMQTTEVT: <evt_type>,<result>

The parameters and their defined values are the following:

<evt_type>
   Integer.
   It can return ``8`` for the ``#XMQTTUNSUB`` command to indicate an acknowledgment of the unsubscription request (UNSUBACK).

<result>
   * ``0`` - Success.
   * *Negative value* - Error code indicating the reason for the failure.

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
   The termination command :ref:`CONFIG_SM_DATAMODE_TERMINATOR <CONFIG_SM_DATAMODE_TERMINATOR>` is not used in this case.
   The value must not exceed the value configured in the :ref:`CONFIG_SM_DATAMODE_BUF_SIZE <CONFIG_SM_DATAMODE_BUF_SIZE>` Kconfig option, as the payload must fit within the data mode buffer to be published as a single message.
   The value ``0`` is equivalent to omitting the parameter.
   Specifying a non-zero ``<data_len>`` together with a non-empty ``<msg>`` results in an error.

Response syntax
~~~~~~~~~~~~~~~

::

   #XMQTTEVT: <evt_type>,<result>

The parameters and their defined values are the following:

<evt_type>
   * ``3`` - Acknowledgment for the published message with QoS 1 (PUBACK).
   * ``4`` - Reception confirmation for the published message with QoS 2 (PUBREC).
   * ``6`` - Confirmation to a publish release message with QoS 2 (PUBCOMP).

<result>
   * ``0`` - Success.
   * *Negative value* - Error code indicating the reason for the failure.

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
