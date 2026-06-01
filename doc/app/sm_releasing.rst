.. _sm_releasing:

Releasing |SM|
##############

Before each official release or pre-release, update the version metadata below.
Each published application image must carry a higher MCUboot sign version than the previous one on the same product line.
Increment ``CONFIG_FW_INFO_FIRMWARE_VERSION`` only when the MCUboot bootloader image changes.

Application — :file:`app/VERSION`
=================================

:file:`app/VERSION` is the single source of truth for:

* The ``major.minor.patchlevel`` prefix in ``AT#XSMVER`` ``<sm_version>`` (Git-describe style: ``v`` + that triplet + optional ``-N-g<hash>`` from Git).
* The MCUboot application sign version (``CONFIG_MCUBOOT_IMGTOOL_SIGN_VERSION``, ``major.minor.patchlevel+VERSION_TWEAK``), used for application downgrade prevention during DFU.

See `Zephyr application VERSION`_.

DFU and ``AT#XSMVER``
---------------------

For application firmware update, a host can read ``AT#XSMVER`` before and after DFU and compare the ``major.minor.patchlevel`` prefix of ``<sm_version>`` (the part from :file:`app/VERSION`, before any ``-N-g<hash>`` Git suffix).
That value must increase when moving to a newer published image; MCUboot enforces the same ordering via the sign version derived from :file:`app/VERSION`.
The Git suffix is informational only and is not used for downgrade checks.

Pre-release numbering
---------------------

Before General Availability (GA) for a target ``X.Y.Z``, use a staging line below ``X.Y.Z`` and increment ``PATCHLEVEL`` for each published pre-release.
Keep ``VERSION_TWEAK = 0`` unless you have a separate reason to use it after GA.

v2.0.0 GA
~~~~~~~~~

The first v2.0.0 pre-release uses ``1.99.0``, the second ``1.99.1``, and so on.
At GA, set the product version to ``2.0.0``.

.. code-block:: none

   # First v2.0.0 pre-release
   VERSION_MAJOR = 1
   VERSION_MINOR = 99
   PATCHLEVEL = 0
   VERSION_TWEAK = 0

.. code-block:: none

   # Second v2.0.0 pre-release
   VERSION_MAJOR = 1
   VERSION_MINOR = 99
   PATCHLEVEL = 1
   VERSION_TWEAK = 0

.. code-block:: none

   # v2.0.0 release
   VERSION_MAJOR = 2
   VERSION_MINOR = 0
   PATCHLEVEL = 0
   VERSION_TWEAK = 0

``2.0.0+0`` must be greater than every shipped ``1.99.N+0`` pre-release.

Future v2.1.0 GA
~~~~~~~~~~~~~~~~

Pre-releases before v2.1.0 GA use the ``2.0.99`` line (for example ``2.0.99``, then ``2.0.100``), incrementing ``PATCHLEVEL`` for each published pre-release—the same pattern as ``1.99.0``, ``1.99.1`` before v2.0.0 GA.
At v2.1.0 GA, set ``VERSION_MAJOR``, ``VERSION_MINOR``, and ``PATCHLEVEL`` to ``2``, ``1``, and ``0``.

General Availability and later
------------------------------

Set the product version and ``VERSION_TWEAK = 0`` at GA.
After GA, increment ``PATCHLEVEL`` for each new published application build on that line.

Do not bump version fields for unpublished local or CI builds.
Record Git tag ↔ :file:`app/VERSION` in release notes.
Confirm the version with :ref:`SM_AT_XSMVER`.

MCUboot — ``CONFIG_FW_INFO_FIRMWARE_VERSION``
=============================================

:file:`app/sysbuild/mcuboot/prj.conf`

Increment only when the MCUboot bootloader image changes.
Used by NSIB (B0) for MCUboot slot downgrade prevention — independent of :file:`app/VERSION`.

After an MCUboot update, ``AT#XBOOTINFO=0`` reports the active slot value.

See :ref:`serial_modem_release_notes`.
