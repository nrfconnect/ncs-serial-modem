.. _SM_AT_WIFI:

Wi-Fi AT commands
#################

This section describes Wi-Fi-related AT commands for devices with Wi-Fi support (e.g., Thingy:91 X with nRF7002).

Wi-Fi scan #XWIFISCAN
======================

The ``#XWIFISCAN`` command scans for nearby Wi-Fi access points and returns their MAC addresses and signal strengths.
This information can be used with the :ref:`#XNRFCLOUDPOS <SM_AT_NRFCLOUDPOS>` command for Wi-Fi-based location services.

Set command
-----------

The set command initiates a Wi-Fi scan and returns the results.

Syntax
~~~~~~

::

   AT#XWIFISCAN[=<timeout>]

The ``<timeout>`` parameter is optional and specifies the maximum time in seconds to wait for scan completion.
Default is 10 seconds. Valid range: 1-60 seconds.

Response syntax
~~~~~~~~~~~~~~~

::

   #XWIFISCAN: <count>
   #XWIFISCAN: <index>,"<MAC>",<RSSI>
   [#XWIFISCAN: <index>,"<MAC>",<RSSI>]
   ...

* The ``<count>`` value indicates the total number of access points found.
* The ``<index>`` value starts at 1 and increments for each access point.
* The ``<MAC>`` value is the MAC address of the access point in format ``aa:bb:cc:dd:ee:ff``.
* The ``<RSSI>`` value is the signal strength in dBm (negative values, e.g., -65).

If no access points are found:

::

   #XWIFISCAN: 0

Examples
~~~~~~~~

Scan with default timeout:

::

   AT#XWIFISCAN
   
   #XWIFISCAN: 3
   #XWIFISCAN: 1,"aa:bb:cc:dd:ee:ff",-65
   #XWIFISCAN: 2,"11:22:33:44:55:66",-72
   #XWIFISCAN: 3,"99:88:77:66:55:44",-80
   OK

Scan with custom timeout:

::

   AT#XWIFISCAN=15
   
   #XWIFISCAN: 2
   #XWIFISCAN: 1,"aa:bb:cc:dd:ee:ff",-65
   #XWIFISCAN: 2,"11:22:33:44:55:66",-72
   OK

Using with nRF Cloud location:

::

   AT#XWIFISCAN
   
   #XWIFISCAN: 2
   #XWIFISCAN: 1,"aa:bb:cc:dd:ee:ff",-65
   #XWIFISCAN: 2,"11:22:33:44:55:66",-72
   OK
   
   AT#XNRFCLOUDPOS=0,1,"aa:bb:cc:dd:ee:ff",-65,"11:22:33:44:55:66",-72
   OK
   
   #XNRFCLOUDPOS: 2,35.455833,139.626111,50

Read command
------------

The read command is not supported.

Test command
------------

The test command tests the existence of the AT command and provides information about its syntax.

Syntax
~~~~~~

::

   AT#XWIFISCAN=?

Response syntax
~~~~~~~~~~~~~~~

::

   #XWIFISCAN[=<timeout>]

Example
~~~~~~~

::

   AT#XWIFISCAN=?
   
   #XWIFISCAN[=<timeout>]
   OK

Notes
~~~~~

* Wi-Fi scanning requires Wi-Fi hardware (e.g., nRF7002 on Thingy:91 X).
* The Wi-Fi interface is brought up only during scanning and powered down afterwards to conserve power.
* Maximum of 20 access points will be returned per scan.
* Scan duration depends on radio environment, typically 2-5 seconds.
* The command will return an error if a scan is already in progress.
