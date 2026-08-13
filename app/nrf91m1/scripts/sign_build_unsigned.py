#!/usr/bin/env python3
"""
sign_build_unsigned.py - build stage (no private keys)

Builds b0 + MCUboot + app, exports the unsigned artifacts, and writes the
Vault signing requests (manifest-tosign.env) in a single step. No production
private key and no Vault are involved.
The real MCUboot app PUBLIC key (committed in nrf91m1/certs/{env}/mcuboot/) is
baked in so MCUboot will later verify the Vault-signed app.

Produces (under signing-out/unsigned/):
  b0.hex                  immutable bootloader               (full build only)
  mcuboot_s0.hex          unsigned MCUboot, S0 variant       (full build only)
  mcuboot_s1.hex          unsigned MCUboot, S1 variant       (full build only)
  app_unsigned.hex        unsigned TF-M + app merge
  manifest-tosign.env     signing parameters + Vault signing requests
"""

from __future__ import annotations

import argparse
import base64
import re
import shutil
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import common as C


# ---------------------------------------------------------------------------
# Argument parsing
# ---------------------------------------------------------------------------


def _parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(
        description="Build stage: produce unsigned artifacts and signing manifest.",
        allow_abbrev=False,
    )
    p.add_argument("--override-mcuboot-version", dest="mcuboot_version", metavar="N",
                   help="Override MCUboot monotonic firmware version.")
    p.add_argument("--override-app-version", dest="app_version", metavar="X",
                   help="Override app image version.")
    p.add_argument("--build-dir", type=Path, default=C.BUILD_DIR, metavar="DIR",
                   help=f"Build directory (default: {C.BUILD_DIR}).")
    env_grp = p.add_mutually_exclusive_group(required=True)
    env_grp.add_argument("--dev", dest="certs_env", action="store_const", const="dev",
                         help="Use development certificates.")
    env_grp.add_argument("--production", dest="certs_env", action="store_const", const="prod",
                         help="Use production certificates.")
    p.add_argument("--use-existing", action="store_true",
                   help="Harvest from an existing build dir (skip west build).")
    p.add_argument("--app-update-only", action="store_true",
                   help="Update app only; skip bootloader artifacts and NSIB signing.")
    p.add_argument("--b0-key-name", metavar="NAME",
                   help="B0 Vault key for NSIB signing (default: lowest version).")
    p.add_argument("--next-mcuboot-key", metavar="NAME",
                   help="Key rotation: also bake NAME into MCUboot; sign app with current key.")
    p.add_argument("--mcuboot-key-name", metavar="NAME",
                   help="Key rotation: override active key (bakes NAME only, signs with NAME).")
    p.add_argument("--output", type=Path, metavar="FILE",
                   help="Output manifest-tosign.env path.")
    return p.parse_args()


# ---------------------------------------------------------------------------
# Build.ninja parsing helpers
# ---------------------------------------------------------------------------


def _ninja_field(ninja_text: str, cmd_pattern: str, flag: str) -> str:
    """Extract --flag VALUE from the first ninja segment matching cmd_pattern.

    Mirrors: grep -rhoE "$cmd_pattern[^&]*" | grep -oE "--$flag [^ ]+" | head -1 | awk '{print $2}'
    """
    m = re.search(rf'({cmd_pattern}[^&]*)', ninja_text)
    if not m:
        return ""
    segment = m.group(1)
    fm = re.search(rf'--{re.escape(flag)}\s+(\S+)', segment)
    return fm.group(1) if fm else ""


def _cmd_segment(ninja_text: str, pattern: str) -> str:
    """Return the first ninja command segment matching pattern (up to the next &)."""
    m = re.search(rf'({pattern}[^&]*)', ninja_text)
    return m.group(1) if m else ""


def _flag_from(cmd_text: str, flag: str) -> str:
    """Extract --flag VALUE from a command text string."""
    m = re.search(rf'--{re.escape(flag)}\s+(\S+)', cmd_text)
    return m.group(1) if m else ""


# ---------------------------------------------------------------------------
# Self-check: verify MCUboot baked the expected public key(s)
# ---------------------------------------------------------------------------


def _verify_mcuboot_pubkey_baked(
    build_dir: Path, mcuboot_pub: str, next_pub: str = "",
) -> None:
    for img in ("mcuboot", "mcuboot_s1_variant"):
        cfg = build_dir / img / "zephyr" / ".config"
        if not cfg.is_file():
            continue
        content = cfg.read_text()
        if (f'CONFIG_BOOT_SIGNATURE_KEY_FILE="{mcuboot_pub}"' not in content
                and f'CONFIG_BOOT_SIGNATURE_KEY_FILE="{mcuboot_pub},' not in content):
            found = re.search(r'CONFIG_BOOT_SIGNATURE_KEY_FILE=.*', content)
            C.err(
                f"MCUboot image '{img}' did NOT bake {mcuboot_pub}\n"
                f"       (found: {found.group(0) if found else '<unset>'}).\n"
                f"       SB_CONFIG_BOOT_SIGNATURE_KEY_FILE did not propagate. Aborting."
            )
        if next_pub and f",{next_pub}" not in content:
            found = re.search(r'CONFIG_BOOT_SIGNATURE_KEY_FILE=.*', content)
            C.err(
                f"MCUboot image '{img}' did NOT bake second key {next_pub}\n"
                f"       (found: {found.group(0) if found else '<unset>'}).\n"
                f"       Key rotation second key did not propagate. Aborting."
            )


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------


def main() -> None:
    C.set_script_name("build-unsigned")
    args = _parse_args()

    build_dir: Path = args.build_dir
    certs_env: str = args.certs_env
    mcuboot_version: str = args.mcuboot_version or ""
    app_version: str = args.app_version or ""
    use_existing: bool = args.use_existing
    app_update_only: bool = args.app_update_only
    b0_key_name_arg: str = args.b0_key_name or ""
    next_mcuboot_key: str = args.next_mcuboot_key or ""
    mcuboot_key_name_explicit: str = args.mcuboot_key_name or ""

    unsigned_dir = C.UNSIGNED_DIR
    tosign: Path = args.output or (unsigned_dir / C.TOSIGN_FILE_NAME)

    # Validate constraints
    if use_existing and (mcuboot_version or app_version or certs_env == "prod"):
        C.err("--override-mcuboot-version / --override-app-version / --production"
              " require a rebuild; cannot combine with --use-existing")
    if use_existing and next_mcuboot_key:
        C.err("--next-mcuboot-key requires a fresh build; cannot combine with --use-existing")

    # Compute cert dirs now that certs_env is resolved
    b0_certs_dir = C.APP_DIR / "nrf91m1/certs" / certs_env / "b0"
    mcuboot_certs_dir = C.APP_DIR / "nrf91m1/certs" / certs_env / "mcuboot"

    # Resolve active MCUboot key
    if mcuboot_key_name_explicit:
        mcuboot_key_name = mcuboot_key_name_explicit
    else:
        mcuboot_key_name = C.discover_mcuboot_key(mcuboot_certs_dir)
        if not mcuboot_key_name:
            C.err(f"no MCUboot cert found in {mcuboot_certs_dir};"
                  " add a cert or use --mcuboot-key-name")

    # Precondition checks
    C.require_python_imgtool()
    if not app_update_only:
        C.require_file(C.HASH_PY, "hash.py")

    mcuboot_pub = str(C.mcuboot_pub_pem(mcuboot_key_name, mcuboot_certs_dir))
    C.require_file(
        mcuboot_pub,
        f"MCUboot app public key ({mcuboot_pub})."
        f" Committed in nrf91m1/certs/{certs_env}/mcuboot/.",
    )

    next_pub = ""
    if next_mcuboot_key:
        next_pub = str(C.mcuboot_pub_pem(next_mcuboot_key, mcuboot_certs_dir))
        C.require_file(next_pub, f"next MCUboot key cert ({next_pub}).")

    # --- Build (unless harvesting an existing build) -------------------------

    if not use_existing:
        C.require_cmd("west", "Activate your NCS environment first.")

        west_cmd = [
            "west", "build", "--pristine", "-T", "./serial_modem.nrf91m1",
            "--board", C.BOARD,
            "--build-dir", str(build_dir),
            "--sysbuild", str(C.APP_DIR),
            "--",
            f"-DMCUBOOT_BAKE_PUBKEY={mcuboot_pub}",
        ]
        if next_mcuboot_key:
            west_cmd.append(f"-DMCUBOOT_BAKE_PUBKEY_2={next_pub}")
            C.log(f"Key rotation: baking {mcuboot_key_name} (current)"
                  f" + {next_mcuboot_key} (next) into MCUboot")
        if mcuboot_version:
            west_cmd.append(f"-Dmcuboot_CONFIG_FW_INFO_FIRMWARE_VERSION={mcuboot_version}")
        if app_version:
            west_cmd.append(f"-Dapp_CONFIG_MCUBOOT_IMGTOOL_SIGN_VERSION={app_version}")

        C.log("Building unsigned artifacts"
              " (real app pubkey baked into MCUboot; debug keys discarded)...")
        print(" ".join(str(x) for x in west_cmd), file=sys.stderr)
        subprocess.run(west_cmd, check=True)

        _verify_mcuboot_pubkey_baked(build_dir, mcuboot_pub, next_pub)
        C.ok("Verified: MCUboot S0/S1 bake the real app pubkey"
             " (build's own signatures are discarded).")
    else:
        C.log(f"Harvesting from existing build dir: {build_dir}")

    # --- Harvest unsigned binaries -------------------------------------------

    unsigned_dir.mkdir(parents=True, exist_ok=True)

    def harvest(src: Path, dst_name: str) -> None:
        C.require_file(src, f"unsigned build artifact for {dst_name}")
        shutil.copy2(src, unsigned_dir / dst_name)

    harvest(build_dir / "app/zephyr/tfm_merged.hex", "app_unsigned.hex")
    if not app_update_only:
        harvest(build_dir / "b0/zephyr/zephyr.hex", "b0.hex")
        harvest(build_dir / "mcuboot/zephyr/zephyr.hex", "mcuboot_s0.hex")
        harvest(build_dir / "mcuboot_s1_variant/zephyr/zephyr.hex", "mcuboot_s1.hex")

    # --- Parse signing parameters from build.ninja ---------------------------

    ninja_path = build_dir / "build.ninja"
    C.require_file(ninja_path, "build.ninja")
    ninja = ninja_path.read_text()

    val_magic_value = _ninja_field(ninja, r'validation_data\.py', "magic-value")
    val_skip = _ninja_field(ninja, r'validation_data\.py', "skip")
    val_offset = _ninja_field(ninja, r'validation_data\.py', "offset")
    prov_s0_addr = _ninja_field(ninja, r'provision\.py', "s0-addr")
    prov_s1_addr = _ninja_field(ninja, r'provision\.py', "s1-addr")
    prov_addr = _ninja_field(ninja, r'provision\.py', "provision-addr")
    prov_max_size = _ninja_field(ninja, r'provision\.py', "max-size")
    prov_counter_slots = _ninja_field(ninja, r'provision\.py', "num-counter-slots-version")
    prov_otp_width = _ninja_field(ninja, r'provision\.py', "otp-write-width")

    # MCUboot imgtool flags — S0 slot identified by rom-fixed 0x8000
    mb_cmd = _cmd_segment(ninja, r'imgtool\.py sign[^&]*rom-fixed 0x8000')
    mcuboot_slot_size = _flag_from(mb_cmd, "slot-size")
    mcuboot_header_size = _flag_from(mb_cmd, "header-size")
    mcuboot_align = _flag_from(mb_cmd, "align")

    # App imgtool flags — identified by tfm_merged in app/build.ninja
    app_ninja_path = build_dir / "app" / "build.ninja"
    C.require_file(app_ninja_path, "app/build.ninja")
    app_ninja = app_ninja_path.read_text()
    app_cmd = _cmd_segment(app_ninja, r'imgtool\.py sign[^&]*tfm_merged')
    app_slot_size = _flag_from(app_cmd, "slot-size")
    app_header_size = _flag_from(app_cmd, "header-size")
    app_align = _flag_from(app_cmd, "align")
    app_version_from_ninja = _flag_from(app_cmd, "version")

    # App load address
    load_m = re.search(r'app\.signed\.binload_address=(0x[0-9a-fA-F]+)', ninja)
    app_load_addr = load_m.group(1) if load_m else ""

    # Version precedence: CLI override > build.ninja > mcuboot/.config
    if not app_version:
        app_version = app_version_from_ninja

    if not mcuboot_version:
        mcuboot_cfg = build_dir / "mcuboot" / "zephyr" / ".config"
        if mcuboot_cfg.is_file():
            mv = re.search(r'CONFIG_FW_INFO_FIRMWARE_VERSION=(\d+)',
                           mcuboot_cfg.read_text())
            mcuboot_version = mv.group(1) if mv else ""

    # Validate all required fields before writing the manifest
    _required = {
        "MAGIC_VALUE (--magic-value from build.ninja)": val_magic_value,
        "VAL_SKIP (--skip from build.ninja)": val_skip,
        "MCUBOOT_SLOT_SIZE (MCUboot --slot-size from build.ninja)": mcuboot_slot_size,
        "MCUBOOT_VERSION (CONFIG_FW_INFO_FIRMWARE_VERSION)": mcuboot_version,
        "PROV_S0_ADDR (--s0-addr from build.ninja)": prov_s0_addr,
        "PROV_S1_ADDR (--s1-addr from build.ninja)": prov_s1_addr,
        "PROV_ADDR (--provision-addr from build.ninja)": prov_addr,
        "APP_SLOT_SIZE (app --slot-size from build.ninja)": app_slot_size,
        "APP_LOAD_ADDR (app load_address from build.ninja)": app_load_addr,
        "APP_VERSION (app --version from build.ninja)": app_version,
    }
    for desc, val in _required.items():
        if not val:
            C.err(f"could not capture {desc}")

    # --- Write manifest-tosign.env header ------------------------------------

    board_parts = C.BOARD.split("/")
    board_name = board_parts[0]
    soc_name = board_parts[1] if len(board_parts) > 1 else ""

    r = subprocess.run(
        ["git", "-C", str(C.NCS_DIR / "nrf"), "rev-parse", "HEAD"],
        capture_output=True, text=True,
    )
    ncs_revision = r.stdout.strip() if r.returncode == 0 else ""

    b0_key_names_list = C.discover_b0_keys(b0_certs_dir)
    b0_key_names = " ".join(b0_key_names_list)
    b0_key_name = b0_key_name_arg or (b0_key_names_list[0] if b0_key_names_list else "")

    tosign.parent.mkdir(parents=True, exist_ok=True)
    tosign.write_text("\n".join([
        "# Signing parameters + Vault requests produced by sign_build_unsigned.py.",
        "# Non-secret, board/config-derived values. Safe to publish alongside artifacts.",
        f'BOARD_NAME="{board_name}"',
        f'SOC_NAME="{soc_name}"',
        f'NCS_REVISION="{ncs_revision}"',
        f'CERTS_ENV="{certs_env}"',
        f'B0_KEY_NAMES="{b0_key_names}"',
        "",
        "# NSIB (B0 signs MCUboot)",
        f'MAGIC_VALUE="{val_magic_value}"',
        f'VAL_SKIP="{val_skip}"',
        f'VAL_OFFSET="{val_offset}"',
        f'MCUBOOT_VERSION="{mcuboot_version}"',
        f'MCUBOOT_SLOT_SIZE="{mcuboot_slot_size}"',
        f'MCUBOOT_HEADER_SIZE="{mcuboot_header_size}"',
        f'MCUBOOT_ALIGN="{mcuboot_align}"',
        "",
        "# B0 provisioning (public key hash list)",
        f'PROV_S0_ADDR="{prov_s0_addr}"',
        f'PROV_S1_ADDR="{prov_s1_addr}"',
        f'PROV_ADDR="{prov_addr}"',
        f'PROV_MAX_SIZE="{prov_max_size}"',
        f'PROV_COUNTER_SLOTS="{prov_counter_slots}"',
        f'PROV_OTP_WIDTH="{prov_otp_width}"',
        "",
        "# Application (MCUboot signs the app); load address captured from build.",
        f'APP_VERSION="{app_version}"',
        f'APP_SLOT_SIZE="{app_slot_size}"',
        f'APP_HEADER_SIZE="{app_header_size}"',
        f'APP_ALIGN="{app_align}"',
        f'APP_LOAD_ADDR="{app_load_addr}"',
    ]) + "\n")

    # Verify captured parameters against the committed reference
    ref_file = C.SIGN_DIR / "expected-signing-params.env"
    if ref_file.is_file():
        params = {
            "MAGIC_VALUE": val_magic_value,
            "VAL_SKIP": val_skip,
            "VAL_OFFSET": val_offset,
            "MCUBOOT_SLOT_SIZE": mcuboot_slot_size,
            "MCUBOOT_HEADER_SIZE": mcuboot_header_size,
            "MCUBOOT_ALIGN": mcuboot_align,
            "PROV_S0_ADDR": prov_s0_addr,
            "PROV_S1_ADDR": prov_s1_addr,
            "PROV_ADDR": prov_addr,
            "PROV_MAX_SIZE": prov_max_size,
            "PROV_COUNTER_SLOTS": prov_counter_slots,
            "PROV_OTP_WIDTH": prov_otp_width,
            "APP_SLOT_SIZE": app_slot_size,
            "APP_HEADER_SIZE": app_header_size,
            "APP_ALIGN": app_align,
            "APP_LOAD_ADDR": app_load_addr,
        }
        mismatch = False
        with open(ref_file) as f:
            for line in f:
                line = line.strip()
                if not line or line.startswith("#"):
                    continue
                ref_key, _, ref_val = line.partition("=")
                actual = params.get(ref_key, "")
                if actual != ref_val:
                    C.warn(f"signing param mismatch: {ref_key}={actual} (expected {ref_val})")
                    mismatch = True
        if mismatch:
            C.err("signing parameters do not match reference;",
                  f"update {ref_file} if the change is intentional")
        C.ok("Signing parameters match reference.")

    # --- Compute signing requests and append to manifest ---------------------

    items: list[str] = []
    request_lines: list[str] = ["", "# === Vault signing requests (see sign_hashes.py)"]

    def add_request(name: str, key: str, prehashed: bool, b64: str) -> None:
        items.append(name)
        ph = "true" if prehashed else "false"
        request_lines.extend([
            f'SIGN_{name}_KEY="{key}"',
            f'SIGN_{name}_PREHASHED="{ph}"',
            f'SIGN_{name}_HASH_ALGORITHM="sha2-256"',
            f'SIGN_{name}_MARSHALING="asn1"',
            f'SIGN_{name}_INPUT_B64="{b64}"',
        ])

    if not app_update_only:
        C.log(f"Hashing MCUboot slots (hash.py) for B0 to sign (key: {b0_key_name})")
        add_request("nsib_s0", b0_key_name, False,
                    base64.b64encode(C.nsib_hash(unsigned_dir / "mcuboot_s0.hex", val_skip)).decode())
        add_request("nsib_s1", b0_key_name, False,
                    base64.b64encode(C.nsib_hash(unsigned_dir / "mcuboot_s1.hex", val_skip)).decode())

    C.log(f"Computing app image digest (imgtool) for MCUBOOT to sign"
          f" (key: {mcuboot_key_name})")
    add_request("app", mcuboot_key_name, True,
                base64.b64encode(C.app_digest(
                    Path(mcuboot_pub), unsigned_dir / "app_unsigned.hex",
                    app_version, app_slot_size, app_header_size, app_align, app_load_addr,
                )).decode())

    request_lines.append(f'SIGN_ITEMS="{" ".join(items)}"')

    with open(tosign, "a") as mf:
        mf.write("\n".join(request_lines) + "\n")

    C.ok(f"Unsigned artifact bundle -> {unsigned_dir}")
    C.log(f"  {' '.join(p.name for p in sorted(unsigned_dir.iterdir()))}")
    C.log(f"Signing requests ({len(items)}): {' '.join(items)} -> {tosign}")
    C.log(f"Next (in the secure env):"
          f" ./sign_hashes.py --in {C.TOSIGN_FILE_NAME} --out manifest-signed.env")


if __name__ == "__main__":
    main()
