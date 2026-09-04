.. _nrf91m1_signing:

nRF91M1 signing procedures
###########################

All commands below assume ``ncs-serial-modem/app`` as the working directory.

.. note::

   **Serial Modem connection**

   nRF91M1 requires the correct UART wiring — in particular the **DTR** pin wired to **GND** when using a PC host.
   See the :ref:`uart_configuration` documentation for pin mapping, signal descriptions, and host setup.

Signing pipeline
================

All build-side scripts are Python and are supposed to be run on an ordinary build machine, with **no access to a private key material**. For the secure signing step ``sign_hashes.py``  and ``sign_hashes.sh``; bash version is provided for signing on machines with no python.
``sign_hashes.py`` requires python version  ≥ 3.9.
``sign_hashes.sh`` has no external requirements except bash version ≥ 5.2.21.

Typical split:

1. **Build machine** — ``sign_build_unsigned.py`` produces ``manifest-tosign.env``
   (opaque base64 hashes, no secrets).
2. **Secure environment** — carries in ``manifest-tosign.env`` and
   ``sign_hashes.py`` (or ``sign_hashes.sh``), authenticates to Vault, produces
   ``manifest-signed.env`` (signatures, no key material).
3. **Build machine** — ``sign_assemble.py`` applies the signatures and produces
   the flashable images.

To authenticate in the secure environment:

.. code-block:: sh

   export VAULT_ADDR=https://vault.example.com
   export VAULT_TRANSIT_MOUNT=vault/location
   vault login

When a procedure involves two ``sign_hashes.py`` calls (MCUboot update), the
Vault session must still be valid for the second call, or re-authenticate.

.. warning::

   **Trust boundaries of a build machine**

   A compromised build environment could inject malicious code into the firmware
   before hashing, or substitute the hashes sent to the secure environment.
   Private keys never leave Vault, but the signing operator cannot verify firmware
   content from hashes alone. Mitigate by building from a pinned, audited source
   tree and cross-checking ``manifest-tosign.env`` against an independent build.

Signing the application
=======================

Produces the initial full image flashed to production devices. All B0 public
keys found in ``nrf91m1/certs/{env}/b0/`` are provisioned, enabling future
B0 key rotation.

.. code-block:: sh

   ./nrf91m1/scripts/sign_build_unsigned.py --dev

   # In secure environment:
   ./nrf91m1/scripts/sign_hashes.py \
       --in  ./signing-out/unsigned/manifest-tosign.env \
       --out ./signing-out/release/manifest-signed.env

   ./nrf91m1/scripts/sign_assemble.py \
       --signed       ./signing-out/release/manifest-signed.env \
       --unsigned-dir ./signing-out/unsigned

   nrfutil device program --firmware signing-out/release/merged_nrf91m1.hex

   nrfutil device reset

Updating the application
========================

Increment the version in ``VERSION`` before building. MCUboot enforces downgrade
prevention. Use ``--override-app-version <X.Y.Z+BUILD>`` to override the VERSION
file (debugging only).

.. code-block:: sh

   ./nrf91m1/scripts/sign_build_unsigned.py --dev --app-update-only

   # In secure environment:
   ./nrf91m1/scripts/sign_hashes.py \
       --in  ./signing-out/unsigned/manifest-tosign.env \
       --out ./signing-out/release/manifest-signed.env

   ./nrf91m1/scripts/sign_assemble.py \
       --signed       ./signing-out/release/manifest-signed.env \
       --unsigned-dir ./signing-out/unsigned \
       --app-update-only

   ./scripts/sm_dfu_host.py --port /dev/ttyACM0 --baudrate 115200 \
       --type application \
       --file ./signing-out/release/app_signed.bin

   nrfutil device reset

Updating MCUboot
================

MCUboot update images are signed by both B0 and the MCUboot key.

.. note::

   In production:

   1. Instead of using the ``--override-mcuboot-version`` option, update the MCUboot ``CONFIG_FW_INFO_FIRMWARE_VERSION`` Kconfig value to a higher version.

.. code-block:: sh

   ./nrf91m1/scripts/sign_build_unsigned.py --dev --override-mcuboot-version 2

   # In secure environment:
   ./nrf91m1/scripts/sign_hashes.py \
       --in  ./signing-out/unsigned/manifest-tosign.env \
       --out ./signing-out/release/manifest-signed.env

   ./nrf91m1/scripts/sign_assemble.py \
       --signed       ./signing-out/release/manifest-signed.env \
       --unsigned-dir ./signing-out/unsigned

   ./nrf91m1/scripts/sign_prepare_mcuboot.py \
       --release-dir ./signing-out/release \
       --release-manifest ./signing-out/release/manifest.env \
       --output ./signing-out/release/manifest-mcuboot-tosign.env

   # Another round in secure environment:
   # Re-authenticate to Vault if the session has expired
   ./nrf91m1/scripts/sign_hashes.py \
       --in  ./signing-out/release/manifest-mcuboot-tosign.env \
       --out ./signing-out/release/manifest-mcuboot-signed.env

   ./nrf91m1/scripts/sign_assemble_mcuboot.py \
       --release-dir ./signing-out/release \
       --mcuboot-signed ./signing-out/release/manifest-mcuboot-signed.env

   ./scripts/sm_dfu_host.py --port /dev/ttyACM0 --baudrate 115200 \
       --type mcuboot-bootloader \
       --file ./signing-out/release/signed_by_mcuboot_and_b0_mcuboot_s1_variant.bin

   nrfutil device reset

Expected: MCUboot version counter using ``AT#XBOOTINFO=0`` returns 2.

B0 key revocation
=================

B0 provisions all public keys found in ``nrf91m1/certs/{env}/b0/`` at initial
flash. The active signing key advances monotonically via the hardware counter;
earlier keys are permanently revoked if a later key passes verification.

Step 1 — Rotate to B0_V1 (revokes B0_V0)
----------------------------------------

.. code-block:: sh

   ./nrf91m1/scripts/sign_build_unsigned.py --override-mcuboot-version 3 --b0-key-name B0_V1 --dev

   # In secure environment:
   ./nrf91m1/scripts/sign_hashes.py \
       --in  signing-out/unsigned/manifest-tosign.env \
       --out signing-out/release/manifest-signed.env

   ./nrf91m1/scripts/sign_assemble.py

   ./nrf91m1/scripts/sign_prepare_mcuboot.py

   # Another round in secure environment:
   # Re-authenticate to Vault if the session has expired
   ./nrf91m1/scripts/sign_hashes.py \
       --in  signing-out/release/manifest-mcuboot-tosign.env \
       --out signing-out/release/manifest-mcuboot-signed.env

   ./nrf91m1/scripts/sign_assemble_mcuboot.py

   # Note: Use signed_by_mcuboot_and_b0_mcuboot_s1_variant.bin if you did not update MCUboot.
   # AT#XBOOTINFO=1 can be used to check the slot that is currently active.
   ./scripts/sm_dfu_host.py --port /dev/ttyACM0 --baudrate 115200 \
       --type mcuboot-bootloader \
       --file ./signing-out/release/signed_by_mcuboot_and_b0_mcuboot.bin

   nrfutil device reset

Expected: MCUboot version counter using ``AT#XBOOTINFO=0`` returns 3.
B0_V0 is now permanently revoked; only B0_V1, B0_V2, and B0_V3 are accepted for future updates.

Step 2 — Verify revocation: sign with B0_V0 (expected to fail)
--------------------------------------------------------------

.. code-block:: sh

   ./nrf91m1/scripts/sign_build_unsigned.py --override-mcuboot-version 4 --b0-key-name B0_V0 --dev

   # In secure environment:
   ./nrf91m1/scripts/sign_hashes.py \
       --in  signing-out/unsigned/manifest-tosign.env \
       --out signing-out/release/manifest-signed.env

   ./nrf91m1/scripts/sign_assemble.py

   ./nrf91m1/scripts/sign_prepare_mcuboot.py

   # Another round in secure environment:
   # Re-authenticate to Vault if the session has expired
   ./nrf91m1/scripts/sign_hashes.py \
       --in  signing-out/release/manifest-mcuboot-tosign.env \
       --out signing-out/release/manifest-mcuboot-signed.env

   ./nrf91m1/scripts/sign_assemble_mcuboot.py

   ./scripts/sm_dfu_host.py --port /dev/ttyACM0 --baudrate 115200 \
       --type mcuboot-bootloader \
       --file ./signing-out/release/signed_by_mcuboot_and_b0_mcuboot_s1_variant.bin

   nrfutil device reset

Expected: B0 rejects the update because B0_V0 is revoked. ``AT#XBOOTINFO=0``
still returns 3.

MCUboot key rotation
====================

MCUboot key rotation replaces the key MCUboot uses to verify the application and MCUboot
update image. The process is a three-phase FOTA campaign; all phases keep devices
functional throughout.

Phase 1 — Transition MCUboot (bakes MCUBOOT_V0 + MCUBOOT_V1, signs app with MCUBOOT_V0)
---------------------------------------------------------------------------------------

Devices currently have MCUboot with only MCUBOOT_V0 baked. This build produces a
MCUboot update that bakes both MCUBOOT_V0 and MCUBOOT_V1. The app is still signed with MCUBOOT_V0 so
existing devices can verify it before and after the MCUboot update.

.. note::

   In production:

   1. Instead of using the ``--override-mcuboot-version`` option, update the MCUboot ``CONFIG_FW_INFO_FIRMWARE_VERSION`` Kconfig value to a higher version.

.. code-block:: sh

   ./nrf91m1/scripts/sign_build_unsigned.py --dev --override-mcuboot-version 5 --next-mcuboot-key MCUBOOT_V1

   # In secure environment:
   ./nrf91m1/scripts/sign_hashes.py \
       --in  signing-out/unsigned/manifest-tosign.env \
       --out signing-out/release/manifest-signed.env

   ./nrf91m1/scripts/sign_assemble.py

   ./nrf91m1/scripts/sign_prepare_mcuboot.py

   # Another round in secure environment:
   ./nrf91m1/scripts/sign_hashes.py \
       --in  signing-out/release/manifest-mcuboot-tosign.env \
       --out signing-out/release/manifest-mcuboot-signed.env

   ./nrf91m1/scripts/sign_assemble_mcuboot.py

   ./scripts/sm_dfu_host.py --port /dev/ttyACM0 --baudrate 115200 \
       --type mcuboot-bootloader \
       --file ./signing-out/release/signed_by_mcuboot_and_b0_mcuboot_s1_variant.bin

   nrfutil device reset

Expected: devices now accept images signed by either MCUBOOT_V0 or MCUBOOT_V1.
Verify with ``AT#XBOOTINFO=0`` that the MCUboot version counter is updated to 5.

Phase 2 — Build MCUBOOT_V1-only artifacts and push app update
-------------------------------------------------------------

Build the final MCUboot (MCUBOOT_V1 only) and app signed with MCUBOOT_V1. Push the app update
first; the MCUboot update is used in Phase 3.

.. important::

   The app update with MCUBOOT_V1 (Phase 2)  must be pushed and succeed before the MCUboot update with MCUBOOT_V1 (Phase 3).
   If the MCUboot update is pushed first, devices will reject to boot the application signed with MCUBOOT_V0, and the device will be bricked.

.. note::

   In production:

   1. Update the ``VERSION`` file to a higher version.
   2. Instead of using the ``--override-mcuboot-version`` option, update the MCUboot ``CONFIG_FW_INFO_FIRMWARE_VERSION`` Kconfig value to a higher version.

.. code-block:: sh

   ./nrf91m1/scripts/sign_build_unsigned.py --dev --override-mcuboot-version 6 --mcuboot-key-name MCUBOOT_V1

   # In secure environment:
   ./nrf91m1/scripts/sign_hashes.py \
       --in  signing-out/unsigned/manifest-tosign.env \
       --out signing-out/release/manifest-signed.env

   ./nrf91m1/scripts/sign_assemble.py

   ./nrf91m1/scripts/sign_prepare_mcuboot.py

   # Another round in secure environment:
   ./nrf91m1/scripts/sign_hashes.py \
       --in  signing-out/release/manifest-mcuboot-tosign.env \
       --out signing-out/release/manifest-mcuboot-signed.env

   ./nrf91m1/scripts/sign_assemble_mcuboot.py

   ./scripts/sm_dfu_host.py --port /dev/ttyACM0 --baudrate 115200 \
       --type application \
       --file ./signing-out/release/app_signed.bin

   nrfutil device reset

Expected: app on all devices is now signed with MCUBOOT_V1.
If the VERSION file was updated, ``AT#XSMVER`` reflects the new version.

Phase 3 — Push final MCUboot with MCUBOOT_V1
--------------------------------------------

Use the MCUboot artifacts produced by the Phase 2 build. No new build needed.

.. code-block:: sh

   ./scripts/sm_dfu_host.py --port /dev/ttyACM0 --baudrate 115200 \
       --type mcuboot-bootloader \
       --file ./signing-out/release/signed_by_mcuboot_and_b0_mcuboot_s1_variant.bin

   nrfutil device reset

Expected: devices now accept images signed only by MCUBOOT_V1. MCUBOOT_V0 is permanently revoked.
Verify with ``AT#XBOOTINFO=0`` that the MCUboot version counter is updated to 6.

Phase 4 — Verify revocation (testing only)
------------------------------------------

.. note::

   Only required during testing to confirm revocation is effective. Devices in field do not perform this step.

.. note::

   When testing production deployments:

   1. Update the ``VERSION`` file **temporarily** to a higher version to test revocation.
      The device will reject the application update and ``AT#XSMVER`` will still return the previous version.
   2. Instead of using the ``--override-mcuboot-version`` option, **temporarily** update the MCUboot ``CONFIG_FW_INFO_FIRMWARE_VERSION`` Kconfig value to a higher version to test revocation.
      The device will reject the MCUboot update and ``AT#XBOOTINFO=0`` will still return the previous version.

Build a MCUboot update and an app update both signed with the old ``MCUBOOT_V0``
key and attempt to install them. Both must be rejected by the device.

.. code-block:: sh

   # MCUboot update signed with revoked MCUBOOT_V0 — expected to fail
   ./nrf91m1/scripts/sign_build_unsigned.py --dev \
       --override-mcuboot-version 7 \
       --mcuboot-key-name MCUBOOT_V0

   # In secure environment:
   ./nrf91m1/scripts/sign_hashes.py \
       --in  signing-out/unsigned/manifest-tosign.env \
       --out signing-out/release/manifest-signed.env

   ./nrf91m1/scripts/sign_assemble.py

   ./nrf91m1/scripts/sign_prepare_mcuboot.py

   # Another round in secure environment:
   ./nrf91m1/scripts/sign_hashes.py \
       --in  signing-out/release/manifest-mcuboot-tosign.env \
       --out signing-out/release/manifest-mcuboot-signed.env

   ./nrf91m1/scripts/sign_assemble_mcuboot.py

   ./scripts/sm_dfu_host.py --port /dev/ttyACM0 --baudrate 115200 \
       --type mcuboot-bootloader \
       --file ./signing-out/release/signed_by_mcuboot_and_b0_mcuboot_s1_variant.bin

   nrfutil device reset

Expected: MCUboot rejects the update; version counter unchanged. ``AT#XBOOTINFO=0``
still returns the previous version.

.. code-block:: sh

   # App update signed with revoked MCUBOOT_V0 — expected to fail
   ./scripts/sm_dfu_host.py --port /dev/ttyACM0 --baudrate 115200 \
       --type application \
       --file ./signing-out/release/app_signed.bin

   nrfutil device reset

Expected: MCUboot rejects the app image; device continues running the previous app. Failed update is only verifiable from boot logs.
If VERSION file was updated, the updated version would not be reflected in the device's AT#XSMVER output.

Phase 5 — Cleanup (production only)
-----------------------------------

After the packages have been created:

- Remove ``nrf91m1/certs/{env}/mcuboot/MCUBOOT_V0.pem`` from the repository so
  it can no longer be accidentally used for new builds.
- Retain ``MCUBOOT_V0`` in Vault so that existing signed update packages can
  still be re-assembled or verified if needed.
