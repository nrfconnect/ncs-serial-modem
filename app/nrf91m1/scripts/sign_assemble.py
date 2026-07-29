#!/usr/bin/env python3
"""
sign_assemble.py - apply the Vault signatures (manifest-signed.env, produced by
sign_hashes.py in the secure env) to the unsigned bundle and build the final
flashable images.

Emits (under signing-out/release/):
  full build:        app_signed.hex/.bin, merged_nrf91m1.hex, manifest.env
  --app-update-only: app_signed.hex/.bin, manifest.env
"""

from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import common as C


def _parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(
        description="Apply Vault signatures to unsigned bundle and build flashable images.",
        allow_abbrev=False,
    )
    p.add_argument("--signed", type=Path, default=C.RELEASE_DIR / "manifest-signed.env",
                   metavar="FILE",
                   help="manifest-signed.env produced by sign_hashes.py.")
    p.add_argument("--unsigned-dir", type=Path, default=C.UNSIGNED_DIR, metavar="DIR",
                   help="Input unsigned bundle directory.")
    p.add_argument("--app-update-only", action="store_true",
                   help="Sign app only; skip bootloader assembly.")
    return p.parse_args()




def main() -> None:
    C.set_script_name("sign-assemble")
    args = _parse_args()

    signed: Path = args.signed
    unsigned_dir: Path = args.unsigned_dir
    app_update_only: bool = args.app_update_only

    # --- Preconditions -------------------------------------------------------

    C.require_python_imgtool()
    C.require_file(signed, "signed manifest (run sign_hashes.py in the secure env)")

    # --- Read parameters from the signed manifest ----------------------------

    if not C.get_required_field("SIGN_ITEMS", signed):
        C.err(f"{signed} has an empty SIGN_ITEMS")

    certs_env = C.get_required_field("CERTS_ENV", signed)
    C.require_safe_name(certs_env, "CERTS_ENV")
    app_version = C.get_required_field("APP_VERSION", signed)
    app_slot_size = C.get_required_field("APP_SLOT_SIZE", signed)
    app_header_size = C.get_required_field("APP_HEADER_SIZE", signed)
    app_align = C.get_required_field("APP_ALIGN", signed)
    app_load_addr = C.get_required_field("APP_LOAD_ADDR", signed)

    magic_value = val_skip = val_offset = ""
    b0_key_names_str = ""
    prov_s0_addr = prov_s1_addr = prov_addr = prov_max_size = ""
    prov_counter_slots = prov_otp_width = ""

    if not app_update_only:
        magic_value = C.get_required_field("MAGIC_VALUE", signed)
        val_skip = C.get_required_field("VAL_SKIP", signed)
        val_offset = C.get_field("VAL_OFFSET", signed) or "0"
        b0_key_names_str = C.get_required_field("B0_KEY_NAMES", signed)
        prov_s0_addr = C.get_required_field("PROV_S0_ADDR", signed)
        prov_s1_addr = C.get_required_field("PROV_S1_ADDR", signed)
        prov_addr = C.get_required_field("PROV_ADDR", signed)
        prov_max_size = C.get_required_field("PROV_MAX_SIZE", signed)
        prov_counter_slots = C.get_required_field("PROV_COUNTER_SLOTS", signed)
        prov_otp_width = C.get_required_field("PROV_OTP_WIDTH", signed)

    # Re-derive cert dirs from the manifest's CERTS_ENV.
    b0_certs_dir = C.APP_DIR / "nrf91m1/certs" / certs_env / "b0"
    mcuboot_certs_dir = C.APP_DIR / "nrf91m1/certs" / certs_env / "mcuboot"

    # Resolve the MCUboot public key used to sign the app.
    # Falls back to the lowest cert in mcuboot_certs_dir if not recorded in the manifest.
    app_key = (C.get_field("SIGN_app_KEY", signed)
               or C.discover_mcuboot_key(mcuboot_certs_dir))
    app_pub = C.mcuboot_pub_pem(app_key, mcuboot_certs_dir)  # validates key name
    C.require_file(app_pub,
                   f"committed MCUboot public key for {app_key} ({mcuboot_certs_dir}/)")

    if not app_update_only:
        C.require_file(C.VALIDATION_DATA_PY, "validation_data.py")
        C.require_file(C.PROVISION_PY, "provision.py")

        nsib_key = C.get_required_field("SIGN_nsib_s0_KEY", signed)
        nsib_pub = C.b0_pub_pem(nsib_key, b0_certs_dir)  # validates key name
        C.require_file(nsib_pub,
                       f"committed B0 public key for {nsib_key} ({b0_certs_dir}/)")

    u_app = unsigned_dir / "app_unsigned.hex"
    C.require_file(u_app, "unsigned artifact")

    release_dir = C.RELEASE_DIR
    release_dir.mkdir(parents=True, exist_ok=True)

    with tempfile.TemporaryDirectory() as work:
        work_dir = Path(work)

        # --- Full build: provision + apply NSIB signatures to MCUboot --------

        if not app_update_only:
            nsib_key_names = b0_key_names_str.split()
            prov_pubs = ",".join(
                str(C.b0_pub_pem(k, b0_certs_dir)) for k in nsib_key_names
            )
            provision_hex = release_dir / "provision.hex"
            C.log(f"Generating B0 provisioning (provision.py)"
                  f" with {len(nsib_key_names)} public key(s)")
            C.gen_provision(prov_pubs, provision_hex,
                            prov_s0_addr, prov_s1_addr, prov_addr,
                            prov_max_size, prov_counter_slots, prov_otp_width)

            u_b0 = unsigned_dir / "b0.hex"
            u_s0 = unsigned_dir / "mcuboot_s0.hex"
            u_s1 = unsigned_dir / "mcuboot_s1.hex"
            for f in (u_b0, u_s0, u_s1):
                C.require_file(f, "unsigned artifact")
            shutil.copy2(u_b0, release_dir / "b0.hex")

            def apply_mcuboot_slot(in_hex: Path, item: str, out_name: str) -> None:
                C.log(f"  {out_name}: apply B0 signature -> validation_data")
                sig_raw = work_dir / f"{out_name}.sigraw"
                C.der_b64_to_raw(
                    C.strip_vault_prefix(C.get_required_field(f"SIGN_{item}_SIG", signed)),
                    sig_raw)
                C.nsib_validation(
                    in_hex, sig_raw, nsib_pub,
                    release_dir / f"{out_name}.hex",
                    release_dir / f"{out_name}.bin",
                    val_skip, val_offset, magic_value,
                )

            apply_mcuboot_slot(u_s0, "nsib_s0", "signed_by_b0_mcuboot")
            apply_mcuboot_slot(u_s1, "nsib_s1", "signed_by_b0_mcuboot_s1_variant")

        # --- Apply MCUboot signature to the app (both modes) -----------------

        app_sig = work_dir / "app.sig.b64"
        app_hex = release_dir / "app_signed.hex"
        app_bin = release_dir / "app_signed.bin"
        C.log("  app: apply MCUBOOT signature -> fix-sig")
        app_sig.write_text(
            C.strip_vault_prefix(C.get_required_field("SIGN_app_SIG", signed)))

        C.app_fixsig(app_sig, app_pub, u_app, app_hex,
                     app_version, app_slot_size, app_header_size, app_align, app_load_addr)
        C.app_fixsig(app_sig, app_pub, u_app, app_bin,
                     app_version, app_slot_size, app_header_size, app_align, app_load_addr)

        r = subprocess.run(
            [C.PYTHON, str(C.IMGTOOL), "verify", "-k", str(app_pub), str(app_hex)],
            capture_output=True,
        )
        if r.returncode != 0:
            C.err("app signature verification FAILED")

    # --- Finalise ------------------------------------------------------------

    manifest_dst = release_dir / C.MANIFEST_FILE_NAME

    if app_update_only:
        shutil.copy2(signed, manifest_dst)
        C.ok("App update assembled.")
        C.log(f"Signed app : {app_hex} / {app_bin}")
        C.log(f"{app_bin} is ready for the FOTA service.")
    else:
        full_hex = release_dir / "merged_nrf91m1.hex"
        C.log(f"Merging full flashable image -> {full_hex}")
        C._run(["west", "ncs-mergehex",
                str(C.APP_DIR / "nrf91m1/mergehex.yaml"),
                "--no-rebuild",
                "--build-dir", str(release_dir),
                "--fail-on-missing"])

        shutil.copy2(signed, manifest_dst)
        C.ok("Release assembled.")
        C.log(f"Signed app          : {app_hex} / {app_bin}")
        C.log(f"Full flashable image: {full_hex}")


if __name__ == "__main__":
    main()
