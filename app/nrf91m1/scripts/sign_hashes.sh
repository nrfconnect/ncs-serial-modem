#!/usr/bin/env bash

set -euo pipefail

usage() {
    IFS= read -r -d '' _usage <<'EOF' || :
sign_hashes.sh - THE ONLY STEP THAT RUNS IN THE SECURE ENVIRONMENT.

Reads the signing requests from manifest-tosign.env (produced by
sign_build_unsigned.py), signs each hash with Vault, and writes the
signatures into manifest-signed.env. Bring manifest-signed.env back to
the build environment and run sign_assemble.py to produce the flashable
release images.

This script intentionally has NO toolchain dependencies: it requires only
the vault CLI and an authenticated Vault session.

Vault credentials are NOT managed here. Authenticate before running:
  export VAULT_ADDR=https://vault.example.com
  export VAULT_TRANSIT_MOUNT=<mount>   # e.g. myorg/myproduct/prod
  export VAULT_CACERT=/path/to/ca.crt  # if Vault uses a private CA
  vault login

Usage:
  sign_hashes.sh --in manifest-tosign.env --out manifest-signed.env

Pass --yes to skip the interactive confirmation prompt.
EOF
    printf '%s' "${_usage}"
}

IN=""
OUT=""
YES=0
while [[ $# -gt 0 ]]; do
    case "$1" in
        --in)      IN="${2:?}"; shift 2 ;;
        --out)     OUT="${2:?}"; shift 2 ;;
        --yes)     YES=1; shift ;;
        -h|--help) usage; exit 0 ;;
        *)         echo "sign_hashes: unknown argument: $1" >&2; exit 1 ;;
    esac
done

die() { echo "sign_hashes: error: $*" >&2; exit 1; }

# Set SIGN_HASHES_DBG_OUT to a file or /dev/stderr to capture subcommand output.
DBG_OUT="${SIGN_HASHES_DBG_OUT:-/dev/null}"

command -v vault >"${DBG_OUT}" 2>&1 || die "the 'vault' CLI is required"

[[ -n "${IN}" ]]  || die "missing --in <manifest-tosign.env>"
[[ -n "${OUT}" ]] || die "missing --out <manifest-signed.env>"
[[ -f "${IN}" ]]  || die "input not found: ${IN}"
vault token lookup >"${DBG_OUT}" 2>&1 || die \
"no authenticated Vault session. Authenticate first, e.g.:
       export VAULT_ADDR=https://vault.example.com
       export VAULT_CACERT=/path/to/ca.crt      # if Vault uses a private CA
       vault login"
[[ -n "${VAULT_TRANSIT_MOUNT:-}" ]] || die \
 "VAULT_TRANSIT_MOUNT is not set. Export it before running sign_hashes.sh, e.g.:
       export VAULT_TRANSIT_MOUNT=myorg/myproduct/debug"

# Parse the manifest as data.
# Reads the first matching KEY="VALUE" line; returns 1 if the key is absent.
get_field() {
    local key="$1" line val
    while IFS= read -r line; do
        [[ "${line}" == "${key}"=* ]] || continue
        val="${line#"${key}="}"
        val="${val#\"}"
        val="${val%\"}"
        printf '%s' "${val}"
        return 0
    done < "${IN}"
    return 1
}

SIGN_ITEMS="$(get_field SIGN_ITEMS)" \
    || die "${IN} has no SIGN_ITEMS (run sign_build_unsigned.py to regenerate the manifest)"
[[ -n "${SIGN_ITEMS}" ]] || die "${IN} has an empty SIGN_ITEMS"

# Validate item names before using them in Vault paths.
for item in ${SIGN_ITEMS}; do
    [[ "${item}" =~ ^[A-Za-z0-9_]+$ ]] \
        || die "unsafe item name in SIGN_ITEMS: '${item}' (expected alphanumeric/underscore only)"
done

# Print a signing summary for the operator to review before any vault write.
_soc_name="$(get_field SOC_NAME)" || _soc_name=""
_soc="${_soc_name:+/${_soc_name}}"
_board="$(get_field BOARD_NAME)${_soc}"
_app_ver="$(get_field APP_VERSION)"
_mcuboot_ver="$(get_field MCUBOOT_VERSION)"
_ncs_rev="$(get_field NCS_REVISION)"
echo "============================================================" >&2
echo "  RELEASE SIGNING — SECURE ENVIRONMENT" >&2
echo "  Verify ALL fields before authorising Vault to sign." >&2
echo "------------------------------------------------------------" >&2
printf   "  Board   : %s\n"  "${_board:-<unknown>}"        >&2
printf   "  App     : %s\n"  "${_app_ver:-<unknown>}"      >&2
printf   "  MCUboot : %s\n"  "${_mcuboot_ver:-<unknown>}"  >&2
printf   "  NCS rev : %s\n"  "${_ncs_rev:-<unknown>}"      >&2
echo     "  Items   :" >&2
for item in ${SIGN_ITEMS}; do
    _key="$(get_field "SIGN_${item}_KEY")"
    _b64="$(get_field "SIGN_${item}_INPUT_B64")"
    printf "    %-14s key=%s\n"   "${item}" "${_key}" >&2
    printf "    %-14s hash=%s\n"  ""        "${_b64}"  >&2
done
echo "============================================================" >&2

if [[ "${YES}" -eq 0 ]]; then
    printf "  Proceed with signing? [y/N] " >&2
    read -r _ans
    [[ "${_ans}" =~ ^[yY] ]] || die "Signing cancelled."
fi
cp "${IN}" "${OUT}"
{
    echo ""
    echo "# === signatures (sign_hashes.sh) ==="
} >> "${OUT}"

for item in ${SIGN_ITEMS}; do
    key="$(get_field "SIGN_${item}_KEY")"
    b64="$(get_field "SIGN_${item}_INPUT_B64")"
    prehashed="$(get_field "SIGN_${item}_PREHASHED")"
    halg="$(get_field "SIGN_${item}_HASH_ALGORITHM")"; halg="${halg:-sha2-256}"
    marsh="$(get_field "SIGN_${item}_MARSHALING")"; marsh="${marsh:-asn1}"
    [[ -n "${key}" && -n "${b64}" ]] || die "request '${item}' is incomplete in ${IN}"

    # Validate the key name (alphanumeric + underscore/hyphen only) before
    # constructing the Vault path. The mount is operator-controlled via
    # VAULT_TRANSIT_MOUNT.
    [[ "${key}" =~ ^[A-Za-z0-9_-]+$ ]] \
        || die "unexpected key name for '${item}': '${key}'"
    path="${VAULT_TRANSIT_MOUNT}/sign/${key}"
    # Validate algorithm fields to prevent argument injection.
    [[ "${halg}" =~ ^[a-z0-9-]+$ ]] \
        || die "unexpected hash_algorithm for '${item}': '${halg}'"
    [[ "${marsh}" =~ ^[a-z0-9-]+$ ]] \
        || die "unexpected marshaling_algorithm for '${item}': '${marsh}'"

    args=( "input=${b64}" "hash_algorithm=${halg}" "marshaling_algorithm=${marsh}" )
    [[ "${prehashed}" = "true" ]] && args+=( "prehashed=true" )

    echo "  signing ${item} with ${path}" >&2
    sig="$(vault write -field=signature "${path}" "${args[@]}")" \
        || die "vault sign failed for ${item} (${path})"
    [[ -n "${sig}" ]] || die "vault returned an empty signature for ${item}"
    echo "SIGN_${item}_SIG=\"${sig}\"" >> "${OUT}"
done

echo "sign_hashes: signed ${SIGN_ITEMS} -> ${OUT}" >&2
