.. _sm_data_mode:

Data mode
#########

.. contents::
   :local:
   :depth: 2

The |SM| (SM) application can run in the two operation modes defined by the AT command set: AT-command mode and data mode.

When running in data mode, the application does the following:

* It considers all the data received from the MCU over the UART bus as arbitrary data to be streamed through the LTE network by various service modules.
* It buffers the URCs received from modem and threads and sends them to the MCU only after exiting data mode.
* For the socket that is in data mode with automatic data reception, the data streamed from a remote service is considered binary data to be sent to the MCU over the UART.

Overview
********

You can manually switch between AT-command mode and data mode.
However, the |SM| data mode is applied automatically when any of the following AT commands are issued:

* Socket ``AT#XSEND`` and ``AT#XSENDTO``
* MQTT publish ``AT#XMQTTPUB``
* nRF Cloud send message ``AT#XNRFCLOUD``
* DFU write ``AT#XDFUWRITE``
* LwM2M carrier library app data send ``AT#XCARRIER``

Entering data mode
==================

The |SM| application enters data mode when an AT command to send data out does not carry the payload.
See the following examples:

* ``AT#XSEND=0,2,0`` makes |SM| enter data mode to receive arbitrary data to transmit for socket 0.
* ``AT#XSEND=0,2,0,<data_len>`` makes |SM| enter data mode to receive arbitrary data of the specified length to transmit for socket 0.
* ``AT#XSEND=0,0,0,"data"`` makes |SM| transmit data in normal AT Command mode for socket 0.

.. note::
   If the data contains either  ``,`` or ``"`` as characters, it can only be sent in data mode.
   A typical use case is to send JSON messages.

Other examples:

* ``AT#XMQTTPUB=<topic>,"",<qos>,<retain>``
* ``AT#XNRFCLOUD=2``
* ``AT#XDFUWRITE=0,0,4096``
* ``AT#XCARRIER="app_data_set"``

The |SM| application sends an *OK* response when it successfully enters data mode.

Sending data in data mode
=========================

Any arbitrary data received from the MCU is sent to LTE network *as-is*.

.. note::
   If the sending operation fails due to a network problem while in data mode, the |SM| application moves to a state where the data received from UART is dropped until the MCU sends the termination command :ref:`CONFIG_SM_DATAMODE_TERMINATOR <CONFIG_SM_DATAMODE_TERMINATOR>`.

Exiting data mode
=================

To exit the data mode without the specification of ``<data_len>``, the MCU sends the termination command set by the :ref:`CONFIG_SM_DATAMODE_TERMINATOR <CONFIG_SM_DATAMODE_TERMINATOR>` configuration option over UART.

The pattern string could be sent alone or as an affix to the data.
The pattern string must be sent in full.

If ``<data_len>`` is specified in the AT command and the specified data length is reached, the |SM| application exits data mode. Termination command is not used in this case.

When exiting the data mode, the |SM| application sends the ``#XDATAMODE`` unsolicited notification.

After exiting the data mode, the |SM| application returns to the AT command mode.

.. note::
   The |SM| application sends the termination string :ref:`CONFIG_SM_DATAMODE_TERMINATOR <CONFIG_SM_DATAMODE_TERMINATOR>` and moves to a state where the data received on the UART is dropped in the following scenarios:

   * The socket in data mode encounters an error.

   For |SM| to stop dropping the data received from UART and move to AT-command mode, the MCU needs to send the termination command :ref:`CONFIG_SM_DATAMODE_TERMINATOR <CONFIG_SM_DATAMODE_TERMINATOR>` back to the |SM| application.

Triggering the transmission
===========================

The |SM| application buffers all the arbitrary data received from the UART bus before initiating the transmission.

The transmission of the buffered data to the LTE network is triggered in the following scenarios:

* Time limit when the defined inactivity timer times out.
* Reception of the termination string.
* Filling of the data mode buffer.

If there is no time limit configured, the minimum required value applies.
For more information, see the :ref:`sm_data_mode_ctrl` command.

Flow control in data mode
=========================

When |SM| fills its UART receive buffers, it disables UART reception. If ``hw-flow-control`` is enabled for the UART, hardware flow control is imposed. Without hardware flow control, the |SM| application will drop incoming data while the UART reception is disabled.
|SM| reenables UART reception when the data has been moved to the data mode buffer.
If the data mode buffer fills, the data are transmitted to the LTE network.

.. note::
   There is no unsolicited notification defined for this event.
   UART hardware flow control is responsible for imposing and revoking flow control.

The data mode buffer size is controlled by :ref:`CONFIG_SM_DATAMODE_BUF_SIZE <CONFIG_SM_DATAMODE_BUF_SIZE>`.

.. note::
   The whole buffer is sent in a single operation.
   When transmitting UDP packets, only one complete packet must reside in the data mode buffer at any time.

Configuration options
*********************

Check and configure the following configuration options for data mode:

.. _CONFIG_SM_DATAMODE_TERMINATOR:

CONFIG_SM_DATAMODE_TERMINATOR - Pattern string to terminate data mode
   This option specifies a pattern string to terminate data mode.
   The default pattern string is ``+++``.

.. _CONFIG_SM_DATAMODE_BUF_SIZE:

CONFIG_SM_DATAMODE_BUF_SIZE - Buffer size for data mode
   This option defines the buffer size for the data mode.
   The default value is 4096.

Data mode AT commands
*********************

The following command list describes data mode-related AT commands.

.. _sm_data_mode_ctrl:
.. _sm_data_mode_at_cmd_start:

Data mode control #XDATACTRL
============================

The ``#XDATACTRL`` command allows you to configure the time limit used to trigger data transmissions.
It can be applied only after entering data mode.

When the time limit is configured, small-size packets will be sent only after the timeout.

Set command
-----------

The set command allows you to configure the time limit for the data mode.

Syntax
~~~~~~

::

   AT#XDATACTRL=<time_limit>

* The ``<time_limit>`` parameter sets the timeout value in milliseconds.
  The default value is the minimum required value, based on the configured UART baud rate.
  This value must be long enough to allow for a DMA transmission of an UART receive (RX) buffer (:ref:`CONFIG_SM_UART_RX_BUF_SIZE <CONFIG_SM_UART_RX_BUF_SIZE>`).

Read command
------------

The read command allows you to check the current time limit configuration and the minimum value required, based on the configured UART baud rate.

Syntax
~~~~~~

::

   AT#XDATACTRL?

Response syntax
~~~~~~~~~~~~~~~

::

   #XDATACTRL: <current_time_limit>,<minimal_time_limit>

Test command
------------

The test command tests the existence of the command and provides information about the type of its subparameters.

Syntax
~~~~~~

::

   AT#XDATACTRL=?

Response syntax
~~~~~~~~~~~~~~~

::

   #XDATACTRL=<time_limit>

Exit data mode #XDATAMODE
=========================

When the application receives the termination command :ref:`CONFIG_SM_DATAMODE_TERMINATOR <CONFIG_SM_DATAMODE_TERMINATOR>` in data mode, it sends the ``#XDATAMODE`` unsolicited notification.

Unsolicited notification
------------------------

::

   #XDATAMODE: <status>

* The ``<status>`` parameter is an integer that indicates the status of the data mode operation.
  It can have one of the following values:

  * ``0`` - Success
  * ``-1`` - Failure

Example
~~~~~~~

::

   AT#XSEND=0,2,0

   OK
   Test datamode
   +++
   #XDATAMODE: 0

.. _sm_data_mode_at_cmd_end:
