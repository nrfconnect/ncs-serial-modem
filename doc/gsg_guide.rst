.. _serial_modem_gsg:

Getting started
###############

.. contents::
   :local:
   :depth: 2

Before getting started, make sure you have a proper |NCS| development environment.
Follow the official `Getting started guide`_.

There are two options for setting up the project, depending on your preferred development environment:

* Using `nRF Connect for VS Code`_ (recommended)
* Using the command line

Initialization
**************

This section represents alternative approaches for initializing the workspace.

.. tabs::

   .. group-tab:: nRF Connect for VS Code

      #. To install the |NCS| and its toolchain using nRF Connect for VS Code, follow `extension documentation <nRF Connect for VS Code install_>`_ or `Installing nRF Connect SDK and VS Code exercise <Devacademy VS Code exercise_>`_  on Nordic Developer Academy.
      #. Open the nRF Connect extension in |VSC| by clicking its icon in the :guilabel:`Activity Bar`.
      #. Click :guilabel:`Create New Application`.
      #. Select the **Browse nRF Connect SDK add-on Index** option.
      #. Search for the **Serial Modem** application and create the project.

   .. group-tab:: Command line

      .. tabs::

         .. group-tab:: Add |SM| into existing |NCS| workspace

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

         .. group-tab:: Initialize workspace and add |SM| from scratch

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

Before building and running the application, you need to connect the development kit to your PC using a USB cable and power on the development kit.
Complete the following steps for building and running:

.. tabs::

   .. group-tab:: nRF Connect for VS Code

      #. Complete the steps in the `How to build an application`_ section in the extension documentation to build the application.

	   You can also specify the additional configuration variables (Kconfig, DTC overlays, and extra Kconfig options) while setting up a build configuration during building the application.
	   See `Providing CMake options <cmake_options_>`_ for more information on setting the additional configuration variables.

      #. Open the nRF Connect extension in |VSC| by clicking its icon in the :guilabel:`Activity Bar`.
      #. In the extension's `Actions View`_, click on :guilabel:`Flash`.

      The application is now built and flashed to the device.
      You can open a serial terminal to use the application.
      The default baud rate is 115200.

   .. group-tab:: Command line

      #. From the workspace folder, navigate to the :file:`ncs-serial-modem` folder::

            cd ncs-serial-modem/app

      #. To build the application, run the following command::

            west build -p -b nrf9151dk/nrf9151/ns

         To build the application with Kconfig, DTC overlays, and extra Kconfig options, the following command gives an example of how they are passed as build arguments::

            west build -p -b nrf9151dk/nrf9151/ns -- -DEXTRA_CONF_FILE="overlay-ppp.conf;overlay-cmux.conf" -DEXTRA_DTC_OVERLAY_FILE="overlay-external-mcu.overlay" -DCONFIG_SM_LOG_LEVEL_DBG=y

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
