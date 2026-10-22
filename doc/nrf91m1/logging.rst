.. raw:: latex

   \nordicStartAppendices

.. _nrf91m1_logging:

Logging and tracing
###################

.. contents::
   :local:
   :depth: 2

The |SM| outputs application logs and modem traces on ``UART1``.
See :ref:`nrf91m1_uart_pin_assignment` for the ``UART1`` pins.

.. include:: ../app/sm_logging.rst
   :start-after: application_logging_start
   :end-before: application_logging_end

.. include:: ../app/sm_logging.rst
   :start-after: modem_traces_start
   :end-before: modem_traces_end

.. include:: ../app/sm_logging.rst
   :start-after: shared_uart_backend_start
   :end-before: shared_uart_backend_end

.. include:: ../app/sm_logging.rst
   :start-after: analyzing_traces_start
   :end-before: analyzing_traces_end
