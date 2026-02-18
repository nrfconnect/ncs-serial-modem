.. _serial_modem_release_notes:

Release notes
#############

.. contents::
   :local:
   :depth: 2

For all the notable changes to the |SM|, see the `release on GitHub <github_release_>`_.

Release artifacts
*****************

The |SM| repository contains the following release artifacts:

.. list-table::
   :widths: auto
   :header-rows: 1

   * - Release artifact
     - Description
   * - ``serial_modem_{VERSION}_nrf9151dk_normal.hex``
     - Normal build for nRF9151 DK, including PPP and CMUX support to operate with PC.
   * - ``serial_modem_{VERSION}_nrf9151dk_normal_mtrace.hex``
     - Normal build with modem traces and application debug logging enabled for nRF9151 DK to operate with PC.
   * - ``serial_modem_{VERSION}_nrf9151dk_extmcu.hex``
     - External MCU build for nRF9151 DK, including PPP and CMUX support.
   * - ``serial_modem_{VERSION}_nrf9151dk_extmcu_mtrace.hex``
     - External MCU build with modem traces and application debug logging enabled for nRF9151 DK.
   * - ``sm_at_client_shell_{VERSION}_nrf54l15dk.hex``
     - AT client shell build for nRF54L15 DK host that can be used as an external MCU with ``serial_modem_{VERSION}_nrf9151dk_extmcu*.hex``.
       See :ref:`uart_configuration` for pin wiring.
   * - ``sm_ppp_shell_{VERSION}_nrf54l15dk.hex``
     - PPP shell build for nRF54L15 DK host that can be used as an external MCU with ``serial_modem_{VERSION}_nrf9151dk_extmcu*.hex``.
       See :ref:`uart_configuration` for pin wiring.

You can download these artifacts from the Assets section of the release on the `release page <github_release_>`_.

.. note::

   Do not use ``_mtrace`` variants for power measurements.
   The trace UART with HW flow control enabled due to RTT application logging has approximately 700 uA overhead on the power consumption.
