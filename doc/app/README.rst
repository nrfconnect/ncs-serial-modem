.. _serial_modem:

Serial modem
############

The Serial Modem (SM) application can be used to emulate a stand-alone modem on an nRF91 Series device.
The application accepts both the modem specific AT commands and proprietary AT commands.
The AT commands are documented in the following guides:

* Modem specific AT commands - `nRF91x1 AT Commands Reference Guide`_  and `nRF9160 AT Commands Reference Guide`_
* Proprietary AT commands - :ref:`SM_AT_commands`

See the subpages for how to use the application, how to extend it, and information on the supported AT commands.

.. toctree::
   :maxdepth: 2
   :caption: Subpages:

   doc/sm_description
   doc/nRF91_as_Zephyr_modem
   doc/PPP_linux
   doc/sm_testing
   doc/sm_extending
   doc/sm_data_mode
   doc/AT_commands
