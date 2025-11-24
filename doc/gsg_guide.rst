.. _serial_modem_gsg:

Getting started
###############

.. contents::
   :local:
   :depth: 2

Before getting started, make sure you have a proper |NCS| development environment.
Follow the official `Getting started guide`_.

Initialization
**************

This section represents alternative approaches for initializing the workspace.

Add |SM| into existing |NCS| workspace
======================================

Assume you have an existing |NCS| workspace in the :file:`ncs` folder.

#. Navigate to the workspace folder::

      cd ncs

#. Clone |SM| repository::

      git clone https://github.com/nrfconnect/ncs-serial-modem.git

#. Set manifest path to the |SM|::

      west config manifest.path ncs-serial-modem

#. Update the |NCS| modules::

      west update

   Depending on the current state of your |NCS| modules, this may take several minutes as it fetches all |NCS| modules according to the requirements of the |SM|.

Initialize workspace and add |SM| from scratch
==============================================

Complete the following steps for initialization:

#. To initialize the workspace::

      west init -m https://github.com/nrfconnect/ncs-serial-modem --mr main ncs

#. Navigate to the workspace folder::

      cd ncs

#. Update the |NCS| modules::

      west update

   This may take several minutes as it fetches all |NCS| modules according to the requirements of the |SM|.

Building and running
********************

Complete the following steps for building and running:

#. From the workspace folder, navigate to the :file:`ncs-serial-modem` folder::

      cd ncs-serial-modem/app

#. To build the application, run the following command::

      west build -p -b nrf9151dk/nrf9151/ns

#. To program the application, run the following command::

      west flash

The application is now built and flashed to the device.
You can open a serial terminal to use the application.
The default baud rate is 115200.

.. note::

   When building the |SM| application, the manifest path must point to the :file:`ncs-serial-modem` folder.
   Otherwise linking will fail as drivers and libraries from |SM| will not be found.

   To check your current manifest path, run the following command anywhere in your workspace::

      west config manifest.path

   To set the manifest path for |SM|, run the following command::

      west config manifest.path ncs-serial-modem
