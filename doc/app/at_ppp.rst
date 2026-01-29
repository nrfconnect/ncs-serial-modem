.. _SM_AT_PPP:

PPP AT commands
****************

.. contents::
   :local:
   :depth: 2

This page describes AT commands related to the Point-to-Point Protocol (PPP).

.. note::

   To use the nRF91 Series SiP as a standalone modem in Zephyr, see the :ref:`sm_as_zephyr_modem` documentation.

PPP is enabled in |SM| by compiling it with the appropriate configuration files, depending on your use case (with or without CMUX).
See the :ref:`sm_config_files` section for more information.

Control PPP #XPPP
=================

Set command
-----------

The set command request activation or deactivation of PPP link, and optionally define the PDN connection used for PPP.
PPP link is established on the second channel (DLC channel 2) when CMUX is used.
If the PPP link is preferred on the first channel (DLC channel 1), you must use the ``AT#XCMUX=2`` command to switch the AT command channel to DLC channel 2 before starting PPP.

If PPP is started without CMUX, the current UART switches to PPP mode.


.. note::

   When a PPP start has been issued, the PPP connection is automatically activated and deactivated when the PDN connection requested for PPP is established and lost, respectively.
   This will continue until a PPP stop is issued by either the user by the ``AT#XPPP=0`` command or by the remote peer disconnecting the PPP using LCP termination.

.. note::

   When PPP is started without CMUX, the current UART cannot be used for AT commands until PPP is stopped by LCP termination.

Syntax
~~~~~~

::

   AT#XPPP=<op>[,<cid>]

* The ``<op>`` parameter can be the following:

  * ``0`` - Stop PPP.
  * ``1`` - Start PPP.

* The ``<cid>`` parameter is an integer indicating the PDN connection to be used for PPP.
  It represents ``cid`` in the ``+CGDCONT`` command.
  Its default value is ``0``, which represents the default PDN connection.

Unsolicited notification
~~~~~~~~~~~~~~~~~~~~~~~~

.. sm_ppp_status_notif_start

::

   #XPPP: <running>,<peer_connected>,<cid>

* The ``<running>`` parameter is an integer that indicates whether PPP is running.
  It is ``1`` for running or ``0`` for stopped.

* The ``<peer_connected>`` parameter is an integer that indicates whether a peer is connected to PPP.
  It is ``1`` for connected or ``0`` for not connected.

* The ``<cid>`` parameter is an integer that indicates the PDN connection used for PPP.

When you activate a PDN connection used for PPP, the ``#XPPP: 1,0,<cid>`` notification is sent and the PPP process starts and takes ownership of the associated serial channel.
If you use the CMUX , the PPP connection starts at the alternative DLC channel, which can be controlled by the ``AT#XCMUX`` command.
Without CMUX, the PPP starts on the current UART, and AT commands cannot be used until PPP is stopped by the LCP termination message.
Similarly, no more AT notifications are sent over the same UART and all URC messages are lost until PPP is stopped.

.. sm_ppp_status_notif_end

PPP Link termination
--------------------

PPP link termination behaves differently in various scenarios.

When the PPP is started on the UART without CMUX, the PPP link termination always returns the channel back to AT command mode.

When the PPP is started over CMUX, the behavior depends on the termination reason and channel configuration:

.. list-table:: PPP link termination reason
   :header-rows: 1
   :widths: auto

   * - Reason
     - Description
   * - PDN connection lost when PPP is running on DLC channel 2
     - The PPP link is terminated using the LCP Terminate-Request message.
       The PPP module keeps waiting for the PDN connection to be re-established to restart PPP automatically.
       The Remote peer might wait for LCP Config-Requests to re-establish the link.
   * - PDN connection lost when PPP is running on DLC channel 1
     - The PPP link is terminated using the LCP Terminate-Request message.
       The PPP module stops without trying to restart.
       DLC channel 1 is returned to AT command mode.
       Traffic on DLC channel 2 is ignored.
   * - Peer disconnection using the LCP Terminate-Request
     - The PPP module stops PPP and releases the CMUX channel.
       It does not attempt to restart.
   * - PPP stopped by AT command
     - The PPP module stops PPP and releases the CMUX channel.
       It does not attempt to restart.

As the ```AT#XPPP=1``` command is designed to restart the PPP connection automatically when the DLC channel used for PPP is the second channel (DLC channel 2).
This is to allow the PPP connection to recover from temporary network losses without your intervention while still allowing you to stop PPP when needed.
This behavior differs from the standard ``ATD*99#`` command, which is not implemented on |SM|.

Examples
--------

The following examples assume CMUX is already established so that the AT channel is usable while PPP is running on a separate CMUX channel.

PPP with default PDN connection:

::

  // Start PPP.
  AT#XPPP=1

  OK

  AT+CFUN=1

  OK

  // PPP is started and waits for a peer to connect.
  #XPPP: 1,0,0

  // Peer connects to |SM|'s PPP.
  #XPPP: 1,1,0

  // Peer disconnects.
  #XPPP: 1,0,0

  // |SM| stops PPP when a peer disconnects.
  #XPPP: 0,0,0

  AT+CFUN=4

  OK

PPP with non-default PDN connection:

::

  // Exemplary PDN connection creation.
  // Note: APN depends on operator and additional APNs may not be supported by the operator.
  AT+CGDCONT=1,"IP","internet2"

  OK

  // Start PPP with the created PDN connection.
  AT#XPPP=1,1

  OK

  AT+CFUN=1

  OK

  // Activate the created PDN connection.
  AT+CGACT=1,1

  // PPP is automatically started when the PDN connection set for PPP has been activated.
  #XPPP: 1,0,1

  // Peer connects to |SM|'s PPP.
  #XPPP: 1,1,1

Connection recovery for network loss.
This requires the PPP on the peer side to keep retrying or waiting for LCP Config-Requests.

::

  // Simulate connection loss by activating the flight mode.
  AT+CFUN=4

  OK

  // PPP is automatically stopped when the PDN connection has been deactivated.
  #XPPP: 0,0,0

  // Reactivate the connection.
  AT+CFUN=1

  OK

  // PPP is automatically restarted when the PDN connection has been reactivated.
  #XPPP: 1,0,0

  // Peer connects to |SM|'s PPP.
  #XPPP: 1,1,0

Read command
------------

The read command allows you to get the status of PPP.

Syntax
~~~~~~

::

   AT#XPPP?

Response syntax
~~~~~~~~~~~~~~~

.. include:: at_ppp.rst
   :start-after: sm_ppp_status_notif_start
   :end-before: sm_ppp_status_notif_end

Testing on Linux
================

You can test |SM|'s PPP on Linux by using the ``pppd`` command.
This section describes a configuration without CMUX.
If you are using CMUX, see :ref:`sm_as_linux_modem` for more information on setting it up.

For the process described here, |SM|'s UARTs must be connected to the Linux host.

#. Run the following command on the Linux host:

   .. code-block:: console

      $ sudo pppd noauth <UART_dev> <baud_rate> local crtscts debug noipdefault connect "/usr/sbin/chat -v -t60 '' AT+CFUN=1 OK AT#XPPP=1 '#XPPP: 1,'" disconnect "/usr/sbin/chat -v -t10 '' AT+CFUN=0 OK" nodetach

   Replace ``<UART_dev>`` by the device file assigned to the Serial Modem's UART and ``<baud_rate>`` by the baud rate of the UART.
   Typically, the device file assigned to it is :file:`/dev/ttyACM0` for an nRF9151 DK.
   To run PPPD in backround, remove the ``nodetach`` option and observe the logs in :file:`/var/log/syslog`.

#. After the PPP link negotiation has completed successfully, you should see log messages similar to the following:

   .. code-block:: console

      sent [LCP ConfReq id=0x1 <options>]
      rcvd [LCP ConfAck id=0x1 <options>]
      ...
      local  IP address <IP_address>
      remote IP address <IP_address>

   You can now use the PPP connection for network communication.

#. Terminate the PPP connection with CTRL+C in the terminal where ``pppd`` is running or ``sudo poff`` in another terminal.

.. note::

   You might encounter a packet domain event (``+CGEV: IPV6 FAIL 0``) indicating a failure in obtaining an IPv6 address.
   This is normal and can be ignored since the modem does not require an IPv6 address when PPP is used.
   IPv6 addressing is handled by the host's IP stack.

.. note::

   You might encounter some issues with DNS resolution.
   Add ``usepeerdns`` to the ``pppd`` command line to have |SM|'s PPP provide DNS server addresses to the Linux host or edit the :file:`/etc/resolv.conf` file to work around these issues.
