.. _sm_testing:

Testing scenarios
#################

.. contents::
   :local:
   :depth: 2

The following testing scenarios give detailed instructions for testing specific use cases.
They list the required AT commands and the expected responses.

Some scenarios are generic and should work out of the box, while others require you to set up a server that you can test against.

See :ref:`sm_building` for instructions on how to build and run the |SM| application.
:ref:`sm_testing_section` describes how to turn on the modem and conduct the tests.

Generic AT commands
*******************

Complete the following steps to test the functionality provided by the :ref:`SM_AT_gen`:

1. Retrieve the version of the |SM| application.

   .. parsed-literal::
      :class: highlight

      **AT#XSMVER**
      #XSMVER: "v0.1.0","3.1.99"
      OK

#. Retrieve a list of all supported proprietary AT commands.

   .. parsed-literal::
      :class: highlight

      **AT#XCLAC**
      AT#XSMVER
      AT#XSLEEP
      AT#XCLAC
      AT#XSOCKET
      AT#XSOCKETOPT
      AT#XBIND
      *[...]*
      OK

#. Check the supported values for the sleep command, then put the kit in sleep mode.

   .. parsed-literal::
      :class: highlight

      **AT#XSLEEP=?**
      #XSLEEP: (1,2)
      OK

TCP/UDP AT commands
*******************

The following sections show how to test the functionalities provided by the :ref:`SM_AT_SOCKET`.

Network connection is required for these tests, so insert a SIM card and connect to the network:

   .. parsed-literal::
      :class: highlight

      **AT+CFUN=1**

      OK

TCP connection
==============

The following steps assume that you have a TCP echo server available.
You can connect to public HTTP servers (port 80), but you cannot send and receive data.

1. Test the TCP connection with a TCP socket:

   a. Check the available values for the AT#XSOCKET command.

      .. parsed-literal::
         :class: highlight

         **AT#XSOCKET=?**

         #XSOCKET: <handle>,(1,2),(1,2,3),(0,1),<cid>

         OK

   #. Open a TCP socket, read the information (handle, family, role, type and cid) about the open socket, and set the receive timeout of the open socket to 30 seconds.

      .. parsed-literal::
         :class: highlight

         **AT#XSOCKET=1,1,0**

         #XSOCKET: 0,1,6

         OK

         **AT#XSOCKET?**

         #XSOCKET: 0,1,0,1,0

         OK

         **AT#XSOCKETOPT=0,1,20,30**

         OK

   #. Replace *example.com* with the hostname or IPv4 address of the TCP echo server, and *1234* with the corresponding port.
      ``1`` indicates that the connection is established.

      .. parsed-literal::
        :class: highlight

         **AT#XCONNECT=0,"**\ *example.com*\ **",**\ *1234*

         #XCONNECT: 0,1

         OK

   #. Send plaintext data to the TCP server and retrieve the response.

      .. parsed-literal::
         :class: highlight

         **AT#XSEND=0,0,0,"Test TCP"**

         #XSEND: 0,0,8

         OK

         **AT#XRECV=0,0,0,0**

         #XRECV: 0,0,8
         Test TCP
         OK

   #. Close the socket and confirm its state.

      .. parsed-literal::
         :class: highlight

         **AT#XCLOSE=0**

         #XCLOSE: 0,0

         OK

         **AT#XSOCKET?**

         OK

UDP connection
==============

The following steps assume that you have a UDP echo server available.

1. Test the UDP connection with a UDP socket using ``AT#XSENDTO``:

   a. Open a UDP socket and read the information (handle, family, role, type and cid) about the open socket.

      .. parsed-literal::
         :class: highlight

         **AT#XSOCKET=1,2,0**

         #XSOCKET: 0,2,17

         OK

         **AT#XSOCKET?**

         #XSOCKET: 0,1,0,2,0

         OK

   #. Send plaintext data to a UDP echo server on a specified port.
      Replace *example.com* with the hostname or IPv4 address of a UDP server, and *1234* with the corresponding port.
      Then retrieve the response.

      .. parsed-literal::
         :class: highlight

         **AT#XSENDTO=0,0,0,"**\ *example.com*\ **",**\ *1234*\ **,"Test UDP"**

         #XSENDTO: 0,0,8

         OK

         **AT#XRECVFROM=0,0,0,0**

         #XRECVFROM: 0,0,8,"<*IP address*>",<*port*>
         Test UDP
         OK

   #. Close the socket.

      .. parsed-literal::
         :class: highlight

         **AT#XCLOSE=0**

         #XCLOSE: 0,0

         OK

#. Test the UDP connection with a UDP socket, using ``AT#XCONNECT``:

   a. Open a UDP socket and set connection to UDP server.
      Replace *example.com* with the hostname or IPv4 address of a UDP server, and *1234* with the corresponding port.

      .. parsed-literal::
         :class: highlight

         **AT#XSOCKET=1,2,0**

         #XSOCKET: 0,2,17

         OK

         **AT#XCONNECT=0,"**\ *example.com*\ **",**\ *1234*

         #XCONNECT: 0,1

         OK

   #. Send plaintext data to the UDP server and retrieve the response.

      .. parsed-literal::
         :class: highlight

         **AT#XSEND=0,0,0,"Test UDP"**

         #XSEND: 0,0,8

         OK

         **AT#XRECV=0,0,0,0**

         #XRECV: 0,0,8
         Test UDP
         OK

   #. Close the socket.

      .. parsed-literal::
         :class: highlight

         **AT#XCLOSE=0**

         #XCLOSE: 0,0

         OK

TLS connection
==============

The following steps assume that you have a TLS echo server available.
You can connect to public HTTPS servers (port 443), but you cannot send and receive the data.

A TLS connection requires a valid certificate for the TLS server.

Update your TLS (root) certificate in PEM format with your selected security tag (in this example, 1000), and start the modem:

   .. note::
      Sending multi-line text to |SM| requires the terminal to be configured to use ``<CR><LF>`` as the line ending.

   .. parsed-literal::
      :class: highlight

      **AT+CFUN=0**

      OK

      **AT%CMNG=0,1000,0,"**-----BEGIN CERTIFICATE-----
      MIIFazCCA1OgAwIBAgIRAIIQz7DSQONZRGPgu2OCiwAwDQYJKoZIhvcNAQELBQAw
      TzELMAkGA1UEBhMCVVMxKTAnBgNVBAoTIEludGVybmV0IFNlY3VyaXR5IFJlc2Vh
      cmNoIEdyb3VwMRUwEwYDVQQDEwxJU1JHIFJvb3QgWDEwHhcNMTUwNjA0MTEwNDM4
      WhcNMzUwNjA0MTEwNDM4WjBPMQswCQYDVQQGEwJVUzEpMCcGA1UEChMgSW50ZXJu
      ZXQgU2VjdXJpdHkgUmVzZWFyY2ggR3JvdXAxFTATBgNVBAMTDElTUkcgUm9vdCBY
      MTCCAiIwDQYJKoZIhvcNAQEBBQADggIPADCCAgoCggIBAK3oJHP0FDfzm54rVygc
      h77ct984kIxuPOZXoHj3dcKi/vVqbvYATyjb3miGbESTtrFj/RQSa78f0uoxmyF+
      0TM8ukj13Xnfs7j/EvEhmkvBioZxaUpmZmyPfjxwv60pIgbz5MDmgK7iS4+3mX6U
      A5/TR5d8mUgjU+g4rk8Kb4Mu0UlXjIB0ttov0DiNewNwIRt18jA8+o+u3dpjq+sW
      T8KOEUt+zwvo/7V3LvSye0rgTBIlDHCNAymg4VMk7BPZ7hm/ELNKjD+Jo2FR3qyH
      B5T0Y3HsLuJvW5iB4YlcNHlsdu87kGJ55tukmi8mxdAQ4Q7e2RCOFvu396j3x+UC
      B5iPNgiV5+I3lg02dZ77DnKxHZu8A/lJBdiB3QW0KtZB6awBdpUKD9jf1b0SHzUv
      KBds0pjBqAlkd25HN7rOrFleaJ1/ctaJxQZBKT5ZPt0m9STJEadao0xAH0ahmbWn
      OlFuhjuefXKnEgV4We0+UXgVCwOPjdAvBbI+e0ocS3MFEvzG6uBQE3xDk3SzynTn
      jh8BCNAw1FtxNrQHusEwMFxIt4I7mKZ9YIqioymCzLq9gwQbooMDQaHWBfEbwrbw
      qHyGO0aoSCqI3Haadr8faqU9GY/rOPNk3sgrDQoo//fb4hVC1CLQJ13hef4Y53CI
      rU7m2Ys6xt0nUW7/vGT1M0NPAgMBAAGjQjBAMA4GA1UdDwEB/wQEAwIBBjAPBgNV
      HRMBAf8EBTADAQH/MB0GA1UdDgQWBBR5tFnme7bl5AFzgAiIyBpY9umbbjANBgkq
      hkiG9w0BAQsFAAOCAgEAVR9YqbyyqFDQDLHYGmkgJykIrGF1XIpu+ILlaS/V9lZL
      ubhzEFnTIZd+50xx+7LSYK05qAvqFyFWhfFQDlnrzuBZ6brJFe+GnY+EgPbk6ZGQ
      3BebYhtF8GaV0nxvwuo77x/Py9auJ/GpsMiu/X1+mvoiBOv/2X/qkSsisRcOj/KK
      NFtY2PwByVS5uCbMiogziUwthDyC3+6WVwW6LLv3xLfHTjuCvjHIInNzktHCgKQ5
      ORAzI4JMPJ+GslWYHb4phowim57iaztXOoJwTdwJx4nLCgdNbOhdjsnvzqvHu7Ur
      TkXWStAmzOVyyghqpZXjFaH3pO3JLF+l+/+sKAIuvtd7u+Nxe5AW0wdeRlN8NwdC
      jNPElpzVmbUq4JUagEiuTDkHzsxHpFKVK7q4+63SM1N95R1NbdWhscdCb+ZAJzVc
      oyi3B43njTOQ5yOf+1CceWxG1bQVs5ZufpsMljq4Ui0/1lvh+wjChP4kqKOJ2qxq
      4RgqsahDYVvTH9w7jXbyLeiNdd8XM2w9U/t7y0Ff/9yi0GE44Za4rF2LN9d11TPA
      mRGunUHBcnWEvgJBQl9nJEiU0Zsnvgc/ubhPgXRR4Xq37Z0j4r7g1SgEEzwxA57d
      emyPxgcYxn/eR44/KJ4EBs+lVDR3veyJm+kXQ99b21/+jh5Xos1AnX5iItreGCc=
      -----END CERTIFICATE-----**"**

      OK

      **AT+CFUN=1**

      OK

1. Test the TLS connection with a TLS socket:

   a. Open a TLS socket that uses the security tag 1000 and connect to a TLS server on a specified port.
      Replace *example.com* with the hostname or IPv4 address of a TLS server and *1234* with the corresponding port.

      .. parsed-literal::
         :class: highlight

         **AT#XSSOCKET=1,1,0,1000**

         #XSSOCKET: 0,1,258

         OK

         **AT#XCONNECT=0,"**\ *example.com*\ **",**\ *1234*

         #XCONNECT: 0,1

         OK

   #. Send plaintext data to the TLS server and retrieve the response.

      .. parsed-literal::
         :class: highlight

         **AT#XSEND=0,0,0,"Test TLS"**

         #XSEND: 0,0,8

         OK

         **AT#XRECV=0,0,0,0**

         #XRECV: 0,0,8
         Test TLS
         OK

   #. Close the socket.

      .. parsed-literal::
         :class: highlight

         **AT#XCLOSE=0**

         #XCLOSE: 0,0

         OK

DTLS connection
===============

The following steps assume that you have a DTLS echo server available with pre-shared key (PSK) authentication.

Update your hex-encoded PSK and the PSK identity to be used for the DTLS connection in the modem, with your selected security tag (in this example, 1001):

   .. parsed-literal::
      :class: highlight

      **AT+CFUN=0**

      OK

      **AT%CMNG=0,1001,3,"6e7266393174657374"**

      OK

      **AT%CMNG=0,1001,4,"nrf91test"**

      OK

      **AT+CFUN=1**

      OK

1. Test the DTLS connection with a DTLS socket:

   a. Open a DTLS socket that uses the security tag 1001 and connect to a DTLS server on a specified port.
      Replace *example.com* with the hostname or IPv4 address of a DTLS server and *1234* with the corresponding port.

      .. parsed-literal::
         :class: highlight

         **AT#XSSOCKET=1,2,0,1001**

         #XSSOCKET: 0,2,273

         OK

         **AT#XCONNECT=0,"**\ *example.com*\ **",**\ *1234*

         #XCONNECT: 0,1

         OK

      #. Send plaintext data to the DTLS server and retrieve the returned data.

      .. parsed-literal::
         :class: highlight

         **AT#XSEND=0,0,0,"Test DTLS"**

         #XSEND: 0,0,9

         OK

         **AT#XRECV=0,0,0,0**

         #XRECV: 0,0,9
         Test DTLS
         OK

   #. Close the socket.

      .. parsed-literal::
         :class: highlight

         **AT#XCLOSE=0**

         #XCLOSE: 0,0

         OK

UDP server
==========

To act as a UDP server, |public_ip_address_req|

|public_ip_address_check|

To test the UDP server functionality, complete the following steps:

1. Create a Python script :file:`client_udp.py` that acts as a UDP client.
   See the following sample code (make sure to use the correct IP addresses and port):

   .. code-block:: python

      import socket
      import time

      host_addr = '000.000.000.00'
      host_port = 1234
      host = (host_addr, host_port)
      local_addr = '9.999.999.99'
      local_port = 1234
      local = (local_addr, local_port)
      s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
      s.bind(local)
      print("Sending: 'Hello, UDP#1!")
      s.sendto(b"Hello, UDP#1!", host)
      time.sleep(1)
      print("Sending: 'Hello, UDP#2!")
      s.sendto(b"Hello, UDP#2!", host)
      data, address = s.recvfrom(1024)
      print(data)
      print(address)

      print("Sending: 'Hello, UDP#3!")
      s.sendto(b"Hello, UDP#3!", host)
      time.sleep(1)
      print("Sending: 'Hello, UDP#4!")
      s.sendto(b"Hello, UDP#4!", host)
      time.sleep(1)
      print("Sending: 'Hello, UDP#5!")
      s.sendto(b"Hello, UDP#5!", host)
      data, address = s.recvfrom(1024)
      print(data)
      print(address)

      print("Closing connection")
      s.close()

#. Establish and test a UDP connection:

   a. Open a UDP socket and bind it to the UDP port that you want to use.
      Replace *1234* with the correct port number.

      .. parsed-literal::
         :class: highlight

         **AT#XSOCKET=1,2,1**
         #XSOCKET: 0,2,17
         OK

         **AT#XBIND=0,**\ *1234*
         OK

   #. Run the :file:`client_udp.py` script to start sending data to the server.

   #. Start receiving and acknowledging the data.
      Replace *example.com* with the hostname or IPv4 address of the UDP client and *1234* with the corresponding port.
      A more complete example would use ``AT#XAPOLL`` to see when data has arrived and then read it with ``AT#XRECVFROM``.

      .. parsed-literal::
         :class: highlight

         **AT#XRECVFROM=0,0,0,0**
         #XRECVFROM: 0,0,13,"<*IP address*>",<*port*>
         Hello, UDP#1!
         OK

         **AT#XRECVFROM=0,0,0,0**
         #XRECVFROM: 0,0,13,"<*IP address*>",<*port*>
         Hello, UDP#2!
         OK

         **AT#XSENDTO=0,0,0,"**\ *example.com*\ **",**\ *1234*\ **,"UDP1/2 received"**
         #XSENDTO: 0,0,15
         OK

         **AT#XRECVFROM=0,0,0,0**
         #XRECVFROM: 0,0,13,"<*IP address*>",<*port*>
         Hello, UDP#3!
         OK

         **AT#XRECVFROM=0,0,0,0**
         #XRECVFROM: 0,0,13,"<*IP address*>",<*port*>
         Hello, UDP#4!
         OK

         **AT#XRECVFROM=0,0,0,0**
         #XRECVFROM: 0,0,13,"<*IP address*>",<*port*>
         Hello, UDP#5!
         OK

         **AT#XSENDTO=0,0,0,"**\ *example.com*\ **",**\ *1234*\ **,"UDP3/4/5 received"**
         #XSENDTO: 0,0,17
         OK

   #. Observe the output of the Python script::

         $ python client_udp.py

         Sending: 'Hello, UDP#1!
         Sending: 'Hello, UDP#2!
         b'UDP1/2 received'
         ('000.000.000.00', 1234, 0, 0)
         Sending: 'Hello, UDP#3!
         Sending: 'Hello, UDP#4!
         Sending: 'Hello, UDP#5!
         b'UDP3/4/5 received'
         ('000.000.000.00', 1234, 0, 0)
         Closing connection

   #. Close the socket.

      .. parsed-literal::
         :class: highlight

         **AT#XCLOSE=0**
         #XCLOSE: 0,0
         OK

DNS lookup
==========

1. Look up the IP address for a hostname.

   .. parsed-literal::
      :class: highlight

      **AT#XGETADDRINFO="www.google.com"**
      #XGETADDRINFO: "172.217.174.100"
      OK

      **AT#XGETADDRINFO="ipv6.google.com"**
      #XGETADDRINFO: "2404:6800:4006:80e::200e"
      OK

      **AT#XGETADDRINFO="172.217.174.100"**
      #XGETADDRINFO: "172.217.174.100"
      OK

      **AT#XGETADDRINFO="2404:6800:4006:80e::200e"**
      #XGETADDRINFO: "2404:6800:4006:80e::200e"
      OK

Socket options
==============

After opening a client-role socket, you can configure various options.

1. Check the available values for the XSOCKETOPT command.

   .. parsed-literal::
      :class: highlight

      **AT#XSOCKETOPT=?**
      #XSOCKETOPT: <handle>,(0,1),<name>,<value>
      OK

#. Open a client socket.

   .. parsed-literal::
      :class: highlight

      **AT#XSOCKET=1,1,0**
      #XSOCKET: 0,1,6
      OK

#. Test to set and get socket options.
   Note that not all options are supported.

   .. parsed-literal::
      :class: highlight

      **AT#XSOCKETOPT=0,1,20,30**
      OK

ICMP AT commands
****************

Complete the following steps to test the functionality provided by the :ref:`SM_AT_ICMP`:

1. Ping a remote host, for example, *www.google.com*.

   .. parsed-literal::
      :class: highlight

      **AT#XPING="www.google.com",45,5000,5,1000**
      OK
      #XPING: 0.637 seconds
      #XPING: 0.585 seconds
      #XPING: 0.598 seconds
      #XPING: 0.598 seconds
      #XPING: 0.599 seconds
      #XPING: average 0.603 seconds

      **AT#XPING="ipv6.google.com",45,5000,5,1000**
      OK
      #XPING: 0.140 seconds
      #XPING: 0.109 seconds
      #XPING: 0.113 seconds
      #XPING: 0.118 seconds
      #XPING: 0.112 seconds
      #XPING: average 0.118 seconds

#. Ping a remote IP address, for example, 172.217.174.100.

   .. parsed-literal::
      :class: highlight

      **AT#XPING="172.217.174.100",45,5000,5,1000**
      OK
      #XPING: 0.873 seconds
      #XPING: 0.576 seconds
      #XPING: 0.599 seconds
      #XPING: 0.623 seconds
      #XPING: 0.577 seconds
      #XPING: average 0.650 seconds
