.. _nrf91m1_intro:

Introduction
############

This document describes the AT commands used to control the nRF91M1 module.
This allows you to run your application on a separate host MCU.
The module accepts both the modem specific AT commands and proprietary application level AT commands.
The AT commands are documented in the following guides:

* nRF91x1 modem specific AT commands

  * ``mfw_nrf91x1`` - `nRF91x1 AT Commands Reference Guide`_
  * ``mfw_nrf9151-ntn`` - `nRF91x1 NTN AT Commands Reference Guide`_

* Proprietary application level AT commands are documented in this document.

The nRF91M1 module supports the following modem firmware:

* ``mfw_nrf91x1`` v2.0.4 (pre-programmed on the module)
* ``mfw_nrf9151-ntn`` v1.0.1 (can be loaded using the :ref:`DFU_AT_commands`)
