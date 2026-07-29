#!/usr/bin/env python3
"""
sign_hashes.py - THE ONLY STEP THAT RUNS IN THE SECURE ENVIRONMENT.

Reads the signing requests from manifest-tosign.env (produced by
sign_build_unsigned.py), signs each hash with Vault, and writes the
signatures into manifest-signed.env. Bring manifest-signed.env back to
the build environment and run sign_assemble.py to produce the flashable
release images.

This script intentionally has NO toolchain dependencies: it requires only
the vault CLI and an authenticated Vault session. It is the sole script
that must be present in the secure signing environment.

Vault credentials are NOT managed here. Authenticate before running:
  export VAULT_ADDR=https://vault.example.com
  export VAULT_TRANSIT_MOUNT=<mount>   # e.g. myorg/myproduct/prod
  export VAULT_CACERT=/path/to/ca.crt  # if Vault uses a private CA
  vault login

Usage:
  sign_hashes.py --in manifest-tosign.env --out manifest-signed.env

Pass --yes to skip the interactive confirmation prompt.
"""

from __future__ import annotations

import argparse
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path
from typing import NoReturn


def die(*args: str) -> NoReturn:
    print(f"sign_hashes: error: {' '.join(args)}", file=sys.stderr)
    sys.exit(1)


def _load_manifest(filepath: Path) -> dict[str, str]:
    """Parse all KEY=\"VALUE\" pairs from a manifest env file into a dict."""
    data: dict[str, str] = {}
    with open(filepath) as f:
        for line in f:
            line = line.rstrip()
            if not line or line.startswith('#') or '=' not in line:
                continue
            k, _, v = line.partition('=')
            data[k] = v.strip('"')
    return data


def _parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(
        description="Secure-environment signing step: sign manifest hashes with Vault.",
        allow_abbrev=False,
    )
    p.add_argument("--in", dest="infile", required=True, metavar="FILE",
                   help="Input manifest-tosign.env.")
    p.add_argument("--out", dest="outfile", required=True, metavar="FILE",
                   help="Output manifest-signed.env.")
    p.add_argument("--yes", action="store_true",
                   help="Skip the interactive confirmation prompt.")
    return p.parse_args()


_ITEM_NAME_RE = re.compile(r'^[A-Za-z0-9_]+$')
_KEY_NAME_RE = re.compile(r'^[A-Za-z0-9_-]+$')
_ALG_RE = re.compile(r'^[a-z0-9-]+$')


def main() -> None:
    args = _parse_args()
    in_file = Path(args.infile)
    out_file = Path(args.outfile)

    if not in_file.is_file():
        die(f"input not found: {in_file}")

    if not shutil.which("vault"):
        die("the 'vault' CLI is required")

    r = subprocess.run(["vault", "token", "lookup"], capture_output=True)
    if r.returncode != 0:
        die(
            "no authenticated Vault session. Authenticate first, e.g.:\n"
            "       export VAULT_ADDR=https://vault.example.com\n"
            "       export VAULT_CACERT=/path/to/ca.crt      # if Vault uses a private CA\n"
            "       vault login"
        )

    vault_mount = os.environ.get("VAULT_TRANSIT_MOUNT", "")
    if not vault_mount:
        die(
            "VAULT_TRANSIT_MOUNT is not set. Export it before running sign_hashes.py, e.g.:\n"
            "       export VAULT_TRANSIT_MOUNT=myorg/myproduct/debug"
        )
    if ".." in vault_mount:
        die(f"VAULT_TRANSIT_MOUNT contains '..': '{vault_mount}'")

    manifest = _load_manifest(in_file)

    sign_items_raw = manifest.get("SIGN_ITEMS", "")
    if not sign_items_raw:
        die(f"{in_file} has no or empty SIGN_ITEMS"
            " (run sign_build_unsigned.py to regenerate the manifest)")

    sign_items = sign_items_raw.split()
    for item in sign_items:
        if not _ITEM_NAME_RE.match(item):
            die(f"unsafe item name in SIGN_ITEMS: '{item}'"
                " (expected alphanumeric/underscore only)")

    # Print a signing summary for the operator to review before any vault write.
    board_name = manifest.get("BOARD_NAME", "<unknown>")
    soc_name = manifest.get("SOC_NAME", "")
    board = f"{board_name}/{soc_name}" if soc_name else board_name
    app_ver = manifest.get("APP_VERSION", "<unknown>")
    mcuboot_ver = manifest.get("MCUBOOT_VERSION", "<unknown>")
    ncs_rev = manifest.get("NCS_REVISION", "<unknown>")

    print("============================================================", file=sys.stderr)
    print("  RELEASE SIGNING — SECURE ENVIRONMENT", file=sys.stderr)
    print("  Verify ALL fields before authorising Vault to sign.", file=sys.stderr)
    print("------------------------------------------------------------", file=sys.stderr)
    print(f"  Board   : {board}", file=sys.stderr)
    print(f"  App     : {app_ver}", file=sys.stderr)
    print(f"  MCUboot : {mcuboot_ver}", file=sys.stderr)
    print(f"  NCS rev : {ncs_rev}", file=sys.stderr)
    print("  Items   :", file=sys.stderr)
    for item in sign_items:
        key = manifest.get(f"SIGN_{item}_KEY", "")
        b64 = manifest.get(f"SIGN_{item}_INPUT_B64", "")
        print(f"    {item:<14s} key={key}", file=sys.stderr)
        print(f"    {'': <14s} hash={b64}", file=sys.stderr)
    print("============================================================", file=sys.stderr)

    if not args.yes:
        print("  Proceed with signing? [y/N] ", end="", flush=True, file=sys.stderr)
        answer = sys.stdin.readline().strip()
        if not answer.lower().startswith("y"):
            die("Signing cancelled.")

    # Write to a temp file; atomically rename to out_file only when all items succeed.
    tmp_out = out_file.with_suffix(".tmp")
    try:
        shutil.copy2(in_file, tmp_out)
        with open(tmp_out, "a") as mf:
            mf.write("\n# === signatures (sign_hashes.py) ===\n")
            for item in sign_items:
                key = manifest.get(f"SIGN_{item}_KEY", "")
                b64 = manifest.get(f"SIGN_{item}_INPUT_B64", "")
                prehashed = manifest.get(f"SIGN_{item}_PREHASHED", "false")
                halg = manifest.get(f"SIGN_{item}_HASH_ALGORITHM") or "sha2-256"
                marsh = manifest.get(f"SIGN_{item}_MARSHALING") or "asn1"

                if not key or not b64:
                    die(f"request '{item}' is incomplete in {in_file}")
                if not _KEY_NAME_RE.match(key):
                    die(f"unexpected key name for '{item}': '{key}'")
                if not _ALG_RE.match(halg):
                    die(f"unexpected hash_algorithm for '{item}': '{halg}'")
                if not _ALG_RE.match(marsh):
                    die(f"unexpected marshaling_algorithm for '{item}': '{marsh}'")

                path = f"{vault_mount}/sign/{key}"
                vault_args = [
                    f"input={b64}",
                    f"hash_algorithm={halg}",
                    f"marshaling_algorithm={marsh}",
                ]
                if prehashed == "true":
                    vault_args.append("prehashed=true")

                print(f"  signing {item} with {path}", file=sys.stderr)
                r = subprocess.run(
                    ["vault", "write", "-field=signature", path, *vault_args],
                    capture_output=True, text=True,
                )
                if r.returncode != 0:
                    detail = r.stderr.strip()
                    die(f"vault sign failed for {item} ({path})"
                        + (f":\n       {detail}" if detail else ""))
                sig = r.stdout.strip()
                if not sig:
                    die(f"vault returned an empty signature for {item}")

                mf.write(f'SIGN_{item}_SIG="{sig}"\n')
    except BaseException:
        tmp_out.unlink(missing_ok=True)
        raise
    else:
        tmp_out.replace(out_file)

    print(f"sign_hashes: signed {' '.join(sign_items)} -> {out_file}", file=sys.stderr)


if __name__ == "__main__":
    main()
