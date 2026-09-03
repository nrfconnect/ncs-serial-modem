#!/usr/bin/env python3
"""
sign_prepare_mcuboot.py - compute MCUboot image signing digests and write
Vault signing requests into manifest-mcuboot-tosign.env.

Only needed for MCUboot update releases. Runs after sign_assemble.py has
produced the B0-signed MCUboot images. The secure environment then signs
the requests with Vault (sign_hashes.py), and sign_assemble_mcuboot.py
applies the returned signatures.
"""

from __future__ import annotations

import argparse
import base64
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import common as C


def _parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(
        description="Compute MCUboot signing digests and write Vault signing requests.",
        allow_abbrev=False,
    )
    p.add_argument("--release-dir", type=Path, default=C.RELEASE_DIR, metavar="DIR",
                   help="Directory with B0-signed MCUboot images.")
    p.add_argument("--release-manifest", type=Path, default=C.RELEASE_DIR / C.MANIFEST_FILE_NAME, metavar="FILE",
                   help="Release manifest.")
    p.add_argument("--output", type=Path, default=C.RELEASE_DIR / "manifest-mcuboot-tosign.env", metavar="FILE",
                   help="Output manifest-mcuboot-tosign.env path.")
    return p.parse_args()


def main() -> None:
    C.set_script_name("sign-prepare-mcuboot")
    args = _parse_args()

    release_dir: Path = args.release_dir
    tosign: Path = args.output

    # --- Preconditions -------------------------------------------------------

    C.require_python_imgtool()

    b0_s0 = release_dir / "signed_by_b0_mcuboot.bin"
    b0_s1 = release_dir / "signed_by_b0_mcuboot_s1_variant.bin"
    C.require_file(b0_s0, "B0-signed MCUboot S0 (run sign_assemble.py first)")
    C.require_file(b0_s1, "B0-signed MCUboot S1 variant (run sign_assemble.py first)")

    release_manifest = args.release_manifest
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

    # Use the same key that signed the app in round 1. During key rotation the
    # lowest cert in mcuboot_certs_dir may already be the next key, so we must
    # read the active key from the manifest rather than re-discovering it.
    mcuboot_key = C.get_required_field("SIGN_app_KEY", release_manifest)

    mcuboot_certs_dir = C.APP_DIR / "nrf91m1/certs" / certs_env / "mcuboot"
    app_pub = C.mcuboot_pub_pem(mcuboot_key, mcuboot_certs_dir)  # validates key name
    C.require_file(app_pub, f"committed MCUboot public key ({mcuboot_certs_dir}/)")

    # --- Build clean output manifest: strip all SIGN_* from release manifest -

    tosign.parent.mkdir(parents=True, exist_ok=True)
    with open(release_manifest) as f:
        tosign.write_text("".join(l for l in f if not l.startswith("SIGN_")))

    # --- Compute digests and append signing requests -------------------------

    items: list[str] = []
    request_lines: list[str] = [
        "",
        "# === MCUboot signing requests (sign_prepare_mcuboot.py) - sign with Vault",
    ]

    C.log("Computing MCUboot image signing digests for MCUBOOT key")
    for name, rom_fixed, b0_bin in (
        ("mcuboot_s0", prov_s0_addr, b0_s0),
        ("mcuboot_s1", prov_s1_addr, b0_s1),
    ):
        C.log(f"  {name}: imgtool digest (rom-fixed={rom_fixed})")
        items.append(name)
        request_lines.extend([
            f'SIGN_{name}_KEY="{mcuboot_key}"',
            f'SIGN_{name}_PREHASHED="true"',
            f'SIGN_{name}_HASH_ALGORITHM="sha2-256"',
            f'SIGN_{name}_MARSHALING="asn1"',
            f'SIGN_{name}_INPUT_B64="{base64.b64encode(C.mcuboot_digest(rom_fixed, app_pub, b0_bin, mcuboot_version, mcuboot_slot_size, mcuboot_header_size, mcuboot_align)).decode()}"',
        ])

    request_lines.append(f'SIGN_ITEMS="{" ".join(items)}"')
    with open(tosign, "a") as mf:
        mf.write("\n".join(request_lines) + "\n")

    C.ok(f"Prepared {len(items)} MCUboot signing request(s) -> {tosign}")
    C.log(f"Requests: {' '.join(items)}")
    C.log("Next (in the secure env):"
          " ./sign_hashes.py --in manifest-mcuboot-tosign.env"
          " --out manifest-mcuboot-signed.env")


if __name__ == "__main__":
    main()
