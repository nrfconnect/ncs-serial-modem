#!/usr/bin/env python3
"""
common - shared configuration and helpers for the signing flow.

Imported by sign_build_unsigned.py, sign_assemble.py,
sign_prepare_mcuboot.py, and sign_assemble_mcuboot.py.
Not meant to be run directly.
"""

from __future__ import annotations

import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import NoReturn

# ---------------------------------------------------------------------------
# Paths
# ---------------------------------------------------------------------------

SIGN_DIR: Path = Path(__file__).resolve().parent
APP_DIR: Path = Path(os.environ.get("APP_DIR", str(SIGN_DIR / "../.."))).resolve()
NCS_DIR: Path = Path(os.environ.get("NCS_DIR", str(APP_DIR / "../.."))).resolve()

BOARD: str = os.environ.get("BOARD", "nrf9151dk/nrf9151/ns")

BUILD_DIR: Path = Path(os.environ.get("BUILD_DIR", str(APP_DIR / "build")))
OUT_DIR: Path = Path(os.environ.get("OUT_DIR", str(APP_DIR / "signing-out")))
UNSIGNED_DIR: Path = Path(os.environ.get("UNSIGNED_DIR", str(OUT_DIR / "unsigned")))
RELEASE_DIR: Path = Path(os.environ.get("RELEASE_DIR", str(OUT_DIR / "release")))
MANIFEST_FILE_NAME = "manifest.env"
TOSIGN_FILE_NAME = "manifest-tosign.env"

# ---------------------------------------------------------------------------
# Signing toolchain (all default to paths inside an NCS west workspace)
# ---------------------------------------------------------------------------

PYTHON: str = os.environ.get("PYTHON", "python3")
IMGTOOL: Path = Path(os.environ.get(
    "IMGTOOL", str(NCS_DIR / "bootloader/mcuboot/scripts/imgtool.py")))
MERGEHEX: Path = Path(os.environ.get(
    "MERGEHEX", str(NCS_DIR / "zephyr/scripts/build/mergehex.py")))
HASH_PY: Path = Path(os.environ.get(
    "HASH_PY", str(NCS_DIR / "nrf/scripts/bootloader/hash.py")))
VALIDATION_DATA_PY: Path = Path(os.environ.get(
    "VALIDATION_DATA_PY",
    str(NCS_DIR / "nrf/scripts/bootloader/validation_data.py")))
PROVISION_PY: Path = Path(os.environ.get(
    "PROVISION_PY", str(NCS_DIR / "nrf/scripts/bootloader/provision.py")))

# ---------------------------------------------------------------------------
# Output formatting
# ---------------------------------------------------------------------------

_SCRIPT_NAME = "signing"


def set_script_name(name: str) -> None:
    global _SCRIPT_NAME
    _SCRIPT_NAME = name


def log(*args: str) -> None:
    print(f"\033[36m[{_SCRIPT_NAME}]\033[0m {' '.join(args)}", file=sys.stderr)


def ok(*args: str) -> None:
    print(f"\033[32m[{_SCRIPT_NAME}] {' '.join(args)}\033[0m", file=sys.stderr)


def warn(*args: str) -> None:
    print(f"\033[33m[{_SCRIPT_NAME}] warning:\033[0m {' '.join(args)}", file=sys.stderr)


def err(*args: str) -> NoReturn:
    print(f"\033[31m[{_SCRIPT_NAME}] error:\033[0m {' '.join(args)}", file=sys.stderr)
    sys.exit(1)

# ---------------------------------------------------------------------------
# Precondition checks
# ---------------------------------------------------------------------------


def require_cmd(cmd: str, hint: str = "") -> None:
    if not shutil.which(cmd):
        err(f"required command '{cmd}' not found in PATH.", hint)


def require_file(path: Path | str, description: str = "") -> None:
    if not Path(path).is_file():
        err(f"{description or 'required file'} not found: {path}")


def require_python_imgtool() -> None:
    require_file(IMGTOOL, "imgtool (set IMGTOOL=...)")
    try:
        r = subprocess.run(
            [PYTHON, "-c",
             "from cryptography.hazmat.primitives.asymmetric.utils"
             " import decode_dss_signature"],
            capture_output=True,
        )
    except FileNotFoundError:
        err(f"python interpreter not found: {PYTHON} (set PYTHON=...)")
    if r.returncode != 0:
        err("Python 'cryptography' package not found (pip install cryptography)")

# ---------------------------------------------------------------------------
# Key validation and discovery
# ---------------------------------------------------------------------------

_KEY_NAME_RE = re.compile(r'^[A-Za-z0-9_-]+$')


def _check_key_name(key: str, context: str) -> None:
    if not _KEY_NAME_RE.match(key):
        err(f"{context}: unsafe key name '{key}'"
            " (expected alphanumeric/underscore/hyphen)")


def require_safe_name(value: str, field: str) -> None:
    """Require that value is safe for use as a path component or Vault route segment."""
    if not _KEY_NAME_RE.match(value):
        err(f"{field} value is unsafe for path construction: '{value}'"
            " (expected alphanumeric/underscore/hyphen)")


def _version_sort_key(p: Path) -> tuple[int, str]:
    """Sort key that orders v0, v1, …, v9, v10 correctly (version-aware)."""
    m = re.search(r'(\d+)', p.stem)
    return (int(m.group(1)) if m else 0, p.stem)


def discover_b0_keys(b0_certs_dir: Path) -> list[str]:
    """Return Vault key names for all B0 certs in b0_certs_dir, ascending by version."""
    return [p.stem for p in sorted(b0_certs_dir.glob("*.pem"), key=_version_sort_key)]


def discover_mcuboot_key(mcuboot_certs_dir: Path) -> str:
    """Return the Vault key name for the lowest-versioned MCUboot cert."""
    pems = sorted(mcuboot_certs_dir.glob("*.pem"), key=_version_sort_key)
    return pems[0].stem if pems else ""


def b0_pub_pem(key: str, b0_certs_dir: Path) -> Path:
    _check_key_name(key, "b0_pub_pem")
    return b0_certs_dir / f"{key}.pem"


def mcuboot_pub_pem(key: str, mcuboot_certs_dir: Path) -> Path:
    _check_key_name(key, "mcuboot_pub_pem")
    return mcuboot_certs_dir / f"{key}.pem"

# ---------------------------------------------------------------------------
# Manifest parsing
# ---------------------------------------------------------------------------


def get_field(key: str, filepath: Path | str) -> str | None:
    """Read KEY="VALUE" from a manifest env file. Returns None if absent."""
    try:
        with open(filepath) as f:
            for line in f:
                if line.startswith(f"{key}="):
                    val = line[len(key) + 1:].rstrip()
                    val = val.strip('"')
                    return val
    except FileNotFoundError:
        pass
    return None

def get_required_field(key: str, filepath: Path | str) -> str:
    """Like get_field but calls err() if the key is absent."""
    val = get_field(key, filepath)
    if val is None:
        err(f"manifest missing {key}")
    return val


_VAULT_PREFIX_RE = re.compile(r'^vault:v[^:]*:')


def strip_vault_prefix(sig: str) -> str:
    """Strip the vault:vN: version prefix from a Vault signature."""
    return _VAULT_PREFIX_RE.sub("", sig)
# ---------------------------------------------------------------------------
# Internal subprocess helper
# ---------------------------------------------------------------------------


def _run(cmd: list[str | Path], **kwargs) -> subprocess.CompletedProcess:
    return subprocess.run([str(c) for c in cmd], check=True, **kwargs)

# ---------------------------------------------------------------------------
# Sign argument builders
# ---------------------------------------------------------------------------


def app_sign_args(
    version: str, slot_size: str, header_size: str, align: str, load_addr: str,
) -> list[str]:
    return [
        "sign",
        "--version", version,
        "--slot-size", slot_size,
        "--header-size", header_size,
        "--pad-header",
        "--align", align,
        "--rom-fixed", load_addr,
    ]


def mcuboot_sign_args(
    version: str, slot_size: str, header_size: str, align: str, rom_fixed: str,
) -> list[str]:
    # --pad-header not needed: MCUboot binaries already reserve header space.
    return [
        "sign",
        "--version", version,
        "--slot-size", slot_size,
        "--header-size", header_size,
        "--align", align,
        "--rom-fixed", rom_fixed,
    ]

# ---------------------------------------------------------------------------
# Round 1: before Vault
# ---------------------------------------------------------------------------


def nsib_hash(in_hex: Path, val_skip: str) -> bytes:
    """Hash an unsigned MCUboot slot (B0 Vault sign input)."""
    result = _run(
        [PYTHON, HASH_PY, "--in", in_hex, "--skip", val_skip],
        capture_output=True,
    )
    if not result.stdout:
        err(f"hash.py produced empty output for {in_hex.name}")
    return result.stdout


def app_digest(
    pubkey: Path,
    unsigned_app: Path,
    version: str,
    slot_size: str,
    header_size: str,
    align: str,
    load_addr: str,
) -> bytes:
    """Compute the imgtool signing digest for the app (MCUboot Vault sign input)."""
    args = app_sign_args(version, slot_size, header_size, align, load_addr)
    with tempfile.TemporaryDirectory() as tmp:
        out = Path(tmp) / "app.digest"
        _run([PYTHON, IMGTOOL, *args, "-k", pubkey,
              "--vector-to-sign", "digest", unsigned_app, out])
        return out.read_bytes()

# ---------------------------------------------------------------------------
# Round 1: after Vault
# ---------------------------------------------------------------------------


def gen_provision(
    pubs_csv: str,
    out_hex: Path,
    s0_addr: str,
    s1_addr: str,
    prov_addr: str,
    max_size: str,
    counter_slots: str,
    otp_width: str,
) -> None:
    """Generate B0 provisioning data from public key PEMs → provision.hex."""
    _run([PYTHON, PROVISION_PY,
          "--s0-addr", s0_addr,
          "--s1-addr", s1_addr,
          "--provision-addr", prov_addr,
          "--public-key-files", pubs_csv,
          "--output", out_hex,
          "--max-size", max_size,
          "--num-counter-slots-version", counter_slots,
          "--otp-write-width", otp_width])


def der_b64_to_raw(der_b64: str, out_rawfile: Path) -> None:
    """Convert a Vault DER signature (base64) → raw 64-byte r||s file."""
    import base64
    from cryptography.hazmat.primitives.asymmetric.utils import decode_dss_signature

    try:
        der = base64.b64decode(der_b64, validate=True)
    except Exception:
        err(f"invalid base64 in signature ({out_rawfile.name})")
    try:
        r, s = decode_dss_signature(der)
    except Exception:
        err(f"invalid DER encoding in signature ({out_rawfile.name})")
    out_rawfile.write_bytes(r.to_bytes(32, "big") + s.to_bytes(32, "big"))


def nsib_validation(
    in_hex: Path,
    raw_sigfile: Path,
    pubkey: Path,
    out_hex: Path,
    out_bin: Path,
    val_skip: str,
    val_offset: str,
    magic_value: str,
) -> None:
    """Embed a raw B0 signature into MCUboot slot validation data → B0-verifiable image."""
    _run([PYTHON, VALIDATION_DATA_PY,
          "--input", in_hex,
          "--skip", val_skip,
          "--offset", val_offset,
          "--signature", raw_sigfile,
          "--public-key", pubkey,
          "--magic-value", magic_value,
          "--output-hex", out_hex,
          "--output-bin", out_bin])


def app_fixsig(
    sig_b64der_file: Path,
    pubkey: Path,
    unsigned_app: Path,
    out: Path,
    version: str,
    slot_size: str,
    header_size: str,
    align: str,
    load_addr: str,
) -> None:
    """Apply a Vault MCUboot signature to the unsigned app → signed app."""
    args = app_sign_args(version, slot_size, header_size, align, load_addr)
    _run([PYTHON, IMGTOOL, *args,
          "--fix-sig", sig_b64der_file,
          "--fix-sig-pubkey", pubkey,
          unsigned_app, out])

# ---------------------------------------------------------------------------
# Round 2: before Vault (MCUboot update)
# ---------------------------------------------------------------------------


def mcuboot_digest(
    rom_fixed: str,
    pubkey: Path,
    b0_signed_bin: Path,
    version: str,
    slot_size: str,
    header_size: str,
    align: str,
) -> bytes:
    """Compute the imgtool signing digest for a B0-signed MCUboot slot (MCUboot Vault input)."""
    args = mcuboot_sign_args(version, slot_size, header_size, align, rom_fixed)
    with tempfile.TemporaryDirectory() as tmp:
        out = Path(tmp) / "mcuboot.digest"
        _run([PYTHON, IMGTOOL, *args, "-k", pubkey,
              "--vector-to-sign", "digest", b0_signed_bin, out])
        return out.read_bytes()

# ---------------------------------------------------------------------------
# Round 2: after Vault (MCUboot update)
# ---------------------------------------------------------------------------


def mcuboot_fixsig(
    rom_fixed: str,
    sig_b64der_file: Path,
    pubkey: Path,
    b0_signed_in: Path,
    out: Path,
    version: str,
    slot_size: str,
    header_size: str,
    align: str,
) -> None:
    """Apply a Vault MCUboot signature to a B0-signed MCUboot slot → dual-signed image."""
    args = mcuboot_sign_args(version, slot_size, header_size, align, rom_fixed)
    _run([PYTHON, IMGTOOL, *args,
          "--fix-sig", sig_b64der_file,
          "--fix-sig-pubkey", pubkey,
          b0_signed_in, out])
