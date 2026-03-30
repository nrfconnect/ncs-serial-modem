Release artifacts
#################

The |SM| repository contains the release artifacts that you can download from the Assets section of the release on the `release page <github_release_>`_.
The following release artifacts are available:

.. list-table::
   :widths: auto
   :header-rows: 1

   * - Release artifact
     - Description
   * - ``serial_modem_{VERSION}_nrf9151dk_nrf91m1``
     - nRF91M1 content for nRF9151 DK.
   * - ``serial_modem_{VERSION}_nrf9151dk_normal``
     - Normal build for nRF9151 DK, including PPP and CMUX support to operate with PC.
   * - ``serial_modem_{VERSION}_nrf9151dk_normal_mtrace``
     - Normal build with modem traces and application debug logging enabled for nRF9151 DK to operate with PC.
   * - ``serial_modem_{VERSION}_nrf9151dk_extmcu``
     - External MCU build for nRF9151 DK, including PPP and CMUX support.
   * - ``serial_modem_{VERSION}_nrf9151dk_extmcu_mtrace``
     - External MCU build with modem traces and application debug logging enabled for nRF9151 DK.
   * - ``sm_at_client_shell_{VERSION}_nrf54l15dk``
     - AT client shell build for nRF54L15 DK host that can be used as an external MCU with ``serial_modem_{VERSION}_nrf9151dk_extmcu``.
       See :ref:`uart_configuration` for pin wiring.
   * - ``sm_ppp_shell_{VERSION}_nrf54l15dk``
     - PPP shell build for nRF54L15 DK host that can be used as an external MCU with ``serial_modem_{VERSION}_nrf9151dk_extmcu``.
       See :ref:`uart_configuration` for pin wiring.

The artifacts are zipped and contain build files such as:

.. list-table::
   :widths: auto
   :header-rows: 1

   * - Release artifact
     - Description
   * - ``.hex``
     - The unsigned application binary (MCUboot + application) to be flashed with Programmer or nRF Util.
   * - ``.signed.bin``
     - Signed application only binary to be used in FOTA/DFU.
   * - ``_dfu.zip``
     - Signed application only binary zipped with metadata used by nRF Cloud for FOTA/DFU.
   * - ``.elf``
     - Application image symbols for debugging purposes.
   * - ``.dts``
     - Devicetree.
   * - ``.config``
     - Application image Kconfig options.

For more details about the build artifacts, see the `build output files`_ documentation.

.. note::

   Do not use ``_mtrace`` variants for power measurements.
   The trace UART with HW flow control enabled due to RTT application logging has approximately 700 uA overhead on the power consumption.
