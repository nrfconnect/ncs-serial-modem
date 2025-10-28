.. _serial_modem_gsg:

Getting started
###############

.. contents::
   :local:
   :depth: 2

Before getting started, make sure you have a proper nRF Connect SDK development environment.
Follow the official `Getting started guide`_.

Initialization
**************

Complete the following steps for initialization:

#. To initialize the workspace:

   .. code-block::

      west init -m https://github.com/nrfconnect/ncs-serial-modem.git --mr main serial_modem

#. Navigate to the project folder::

    cd serial_modem

#. Update the |NCS| modules.

   .. code-block::

      west update

   This might take a while.

Building and running
********************

Complete the following steps for building and running:

#. Navigate to the project folder:

   .. code-block::

      cd project/app

#. To build the application, run the following command:

   .. code-block::

      west build -p -b nrf9151dk/nrf9151/ns

#. To program the application, run the following command:

   .. code-block::

      west flash

The application is now built and flashed to the device.
You can open a serial terminal to use the application.
The default baud rate is 115200.
