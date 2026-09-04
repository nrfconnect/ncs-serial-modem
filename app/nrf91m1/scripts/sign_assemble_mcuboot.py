#!/usr/bin/env python3
"""
sign_assemble_mcuboot.py - apply Vault MCUboot signatures to the B0-signed
MCUboot images to produce the dual-signed images needed for MCUboot updates.

Only needed for MCUboot update releases. Run after the second sign_hashes.py
trip has produced manifest-mcuboot-signed.env.

Inputs (from release_dir):
  signed_by_b0_mcuboot.bin/.hex            produced by sign_assemble.py
  signed_by_b0_mcuboot_s1_variant.bin/.hex produced by sign_assemble.py
  manifest.env                             produced by sign_assemble.py
  manifest-mcuboot-signed.env              produced by sign_hashes.py (second trip)

Emits (under release_dir):
  signed_by_mcuboot_and_b0_mcuboot.hex/.bin
  signed_by_mcuboot_and_b0_mcuboot_s1_variant.hex/.bin
"""

from __future__ import annotations

import argparse
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import common as C


def _parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(
        description="Apply Vault MCUboot signatures to produce dual-signed MCUboot images.",
        allow_abbrev=False,
    )
    p.add_argument("--mcuboot-signed", type=Path,
                   default=C.RELEASE_DIR / "manifest-mcuboot-signed.env", metavar="FILE",
                   help="manifest-mcuboot-signed.env from sign_hashes.py (second trip).")
    p.add_argument("--release-dir", type=Path, default=C.RELEASE_DIR, metavar="DIR",
                   help="Input/output directory.")
    return p.parse_args()


def main() -> None:
    C.set_script_name("sign-assemble-mcuboot")
    args = _parse_args()

    mcuboot_signed: Path = args.mcuboot_signed
    release_dir: Path = args.release_dir

    # --- Preconditions -------------------------------------------------------

    C.require_python_imgtool()
    C.require_file(mcuboot_signed,
                   "MCUboot signed manifest (run sign_hashes.py second trip)")

    b0_s0_bin = release_dir / "signed_by_b0_mcuboot.bin"
    b0_s0_hex = release_dir / "signed_by_b0_mcuboot.hex"
    b0_s1_bin = release_dir / "signed_by_b0_mcuboot_s1_variant.bin"
    b0_s1_hex = release_dir / "signed_by_b0_mcuboot_s1_variant.hex"
    for f in (b0_s0_bin, b0_s0_hex, b0_s1_bin, b0_s1_hex):
        C.require_file(f, "B0-signed MCUboot artifact (run sign_assemble.py first)")

    release_manifest = release_dir / C.MANIFEST_FILE_NAME
    C.require_file(release_manifest, "release manifest (run sign_assemble.py first)")

    # --- Read signing parameters from the release manifest -------------------

    certs_env = C.get_required_field("CERTS_ENV", release_manifest)
    C.require_safe_name(certs_env, "CERTS_ENV")
    prov_s0_addr = C.get_required_field("PROV_S0_ADDR", release_manifest)
    prov_s1_addr = C.get_required_field("PROV_S1_ADDR", release_manifest)
    mcuboot_version = C.get_required_field("MCUBOOT_VERSION", release_manifest)
    mcuboot_slot_size = C.get_required_field("MCUBOOT_SLOT_SIZE", release_manifest)
    mcuboot_header_size = C.get_required_field("MCUBOOT_HEADER_SIZE", release_manifest)
    mcuboot_align = C.get_required_field("MCUBOOT_ALIGN", release_manifest)

    mcuboot_certs_dir = C.APP_DIR / "nrf91m1/certs" / certs_env / "mcuboot"
    app_key = (C.get_field("SIGN_app_KEY", release_manifest)
               or C.discover_mcuboot_key(mcuboot_certs_dir))
    app_pub = C.mcuboot_pub_pem(app_key, mcuboot_certs_dir)  # validates key name
    C.require_file(app_pub,
                   f"committed MCUboot public key for {app_key} ({mcuboot_certs_dir}/)")

    # --- Apply MCUboot signatures --------------------------------------------

    with tempfile.TemporaryDirectory() as work:
        work_dir = Path(work)

        def apply_mcuboot_slot(
            rom_fixed: str, item: str,
            b0_bin: Path, b0_hex: Path, out_name: str,
        ) -> None:
            C.log(f"  {out_name}: apply MCUBOOT signature (rom-fixed={rom_fixed})")
            sig = work_dir / f"{out_name}.sig.b64"
            sig.write_text(
                C.strip_vault_prefix(C.get_required_field(f"SIGN_{item}_SIG", mcuboot_signed))
            )
            for src, out in ((b0_bin, release_dir / f"{out_name}.bin"),
                             (b0_hex, release_dir / f"{out_name}.hex")):
                C.mcuboot_fixsig(rom_fixed, sig, app_pub, src, out,
                                 mcuboot_version, mcuboot_slot_size,
                                 mcuboot_header_size, mcuboot_align)

        C.log("Assembling MCUboot images (MCUBOOT key)")
        apply_mcuboot_slot(prov_s0_addr, "mcuboot_s0",
                           b0_s0_bin, b0_s0_hex, "signed_by_mcuboot_and_b0_mcuboot")
        apply_mcuboot_slot(prov_s1_addr, "mcuboot_s1",
                           b0_s1_bin, b0_s1_hex, "signed_by_mcuboot_and_b0_mcuboot_s1_variant")

    C.ok("MCUboot images assembled.")
    C.log(f"  {release_dir}/signed_by_mcuboot_and_b0_mcuboot.hex/.bin")
    C.log(f"  {release_dir}/signed_by_mcuboot_and_b0_mcuboot_s1_variant.hex/.bin")


if __name__ == "__main__":
    main()
