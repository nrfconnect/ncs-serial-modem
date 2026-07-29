#!/usr/bin/env bash
#
# sign-assemble-mcuboot.sh - apply Vault MCUBOOT signatures to the B0-signed MCUboot
# images to produce the dual-signed signed_by_mcuboot_and_b0_mcuboot images
# needed for MCUboot updates. Run in build environment after the second sign-hashes.sh trip.
#
# This is the second assemble step; it is ONLY needed for MCUboot update releases.
# App-update-only releases use sign-assemble.sh alone (single secure-env trip).
#
# Inputs (from ${RELEASE_DIR} unless overridden):
#   signed_by_b0_mcuboot.bin/.hex            produced by sign-assemble.sh
#   signed_by_b0_mcuboot_s1_variant.bin/.hex produced by sign-assemble.sh
#   manifest.env                             produced by sign-assemble.sh
#   manifest-mcuboot-signed.env              produced by sign-hashes.sh (second trip)
#
# Emits (under ${RELEASE_DIR}):
#   signed_by_mcuboot_and_b0_mcuboot.hex/.bin
#   signed_by_mcuboot_and_b0_mcuboot_s1_variant.hex/.bin
#
# Options:
#   --mcuboot-signed FILE  manifest-mcuboot-signed.env.
#                          Default: ${OUT_DIR}/manifest-mcuboot-signed.env
#   --release-dir DIR      Input/output directory.        Default: ${RELEASE_DIR}

set -euo pipefail
SCRIPT_NAME="sign-assemble-mcuboot"
. "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/common.sh"

MCUBOOT_SIGNED="${OUT_DIR}/manifest-mcuboot-signed.env"
while [ $# -gt 0 ]; do
    case "$1" in
        --mcuboot-signed) MCUBOOT_SIGNED="${2:?}"; shift 2 ;;
        --release-dir)    RELEASE_DIR="${2:?}"; shift 2 ;;
        -h|--help)        sed -n '2,24p' "$0"; exit 0 ;;
        *)                err "unknown argument: $1 (see --help)" ;;
    esac
done

# --- Preconditions -----------------------------------------------------------

require_python_imgtool
require_file "${MCUBOOT_SIGNED}" "MCUboot signed manifest (run sign-hashes.sh second trip)"

B0_S0_BIN="${RELEASE_DIR}/signed_by_b0_mcuboot.bin"
B0_S0_HEX="${RELEASE_DIR}/signed_by_b0_mcuboot.hex"
B0_S1_BIN="${RELEASE_DIR}/signed_by_b0_mcuboot_s1_variant.bin"
B0_S1_HEX="${RELEASE_DIR}/signed_by_b0_mcuboot_s1_variant.hex"
for f in "${B0_S0_BIN}" "${B0_S0_HEX}" "${B0_S1_BIN}" "${B0_S1_HEX}"; do
    require_file "$f" "B0-signed MCUboot artifact (run sign-assemble.sh first)"
done

# Load the release manifest to get signing parameters and the cert environment.
MANIFEST="${RELEASE_DIR}/${MANIFEST_FILE_NAME}"
require_file "${MANIFEST}" "release manifest (run sign-assemble.sh first)"

# Read signing parameters from the release manifest.
CERTS_ENV="$(get_field CERTS_ENV "${MANIFEST}")"       || err "manifest missing CERTS_ENV"
PROV_S0_ADDR="$(get_field PROV_S0_ADDR "${MANIFEST}")" || err "manifest missing PROV_S0_ADDR"
PROV_S1_ADDR="$(get_field PROV_S1_ADDR "${MANIFEST}")" || err "manifest missing PROV_S1_ADDR"
MCUBOOT_VERSION="$(get_field MCUBOOT_VERSION "${MANIFEST}")" \
    || err "manifest missing MCUBOOT_VERSION"
MCUBOOT_SLOT_SIZE="$(get_field MCUBOOT_SLOT_SIZE "${MANIFEST}")" \
    || err "manifest missing MCUBOOT_SLOT_SIZE"
MCUBOOT_HEADER_SIZE="$(get_field MCUBOOT_HEADER_SIZE "${MANIFEST}")" \
    || err "manifest missing MCUBOOT_HEADER_SIZE"
MCUBOOT_ALIGN="$(get_field MCUBOOT_ALIGN "${MANIFEST}")" \
    || err "manifest missing MCUBOOT_ALIGN"

# Re-derive cert dirs from the manifest's CERTS_ENV.
B0_CERTS_DIR="${APP_DIR}/nrf91m1/certs/${CERTS_ENV}/b0"
MCUBOOT_CERTS_DIR="${APP_DIR}/nrf91m1/certs/${CERTS_ENV}/mcuboot"

# Use the same MCUboot key that signed the app in round 1 (recorded in the manifest).
_app_key="$(get_field SIGN_app_KEY "${MANIFEST}")" || _app_key="${MCUBOOT_KEY_NAME}"
[[ "${_app_key}" =~ ^[A-Za-z0-9_-]+$ ]] \
    || err "unsafe key name in SIGN_app_KEY: '${_app_key}'"
APP_PUB="$(mcuboot_pub_pem "${_app_key}")"
require_file "${APP_PUB}" "committed MCUboot public key for ${_app_key} (${MCUBOOT_CERTS_DIR}/)"

# Read signatures from the mcuboot signed manifest.
SIGN_ITEMS="$(get_field SIGN_ITEMS "${MCUBOOT_SIGNED}")" \
    || err "${MCUBOOT_SIGNED} has no SIGN_ITEMS"
[ -n "${SIGN_ITEMS}" ] || err "${MCUBOOT_SIGNED} has an empty SIGN_ITEMS"

WORK="$(mktemp -d)"; trap 'rm -rf "${WORK}"' EXIT

# Extract a Vault signature for an item, stripping the vault:vN: version prefix.
sig_b64der() {  # <item-name> -> stdout: base64 DER
    local item="$1" raw
    raw="$(get_field "SIGN_${item}_SIG" "${MCUBOOT_SIGNED}")" \
        || err "no signature for '${item}' in ${MCUBOOT_SIGNED}"
    [ -n "${raw}" ] || err "no signature for '${item}' in ${MCUBOOT_SIGNED}"
    printf '%s' "${raw#vault:v*:}"
}

apply_mcuboot_slot() {  # <rom_fixed_addr> <item> <b0_bin> <b0_hex> <out_name>
    local rom_fixed="$1" item="$2" b0_bin="$3" b0_hex="$4" out_name="$5"
    local sig="${WORK}/${out_name}.sig.b64"
    local out_bin="${RELEASE_DIR}/${out_name}.bin"
    local out_hex="${RELEASE_DIR}/${out_name}.hex"

    log "  ${out_name}: apply MCUBOOT signature (rom-fixed=${rom_fixed})"
    sig_b64der "${item}" > "${sig}"
    mcuboot_fixsig "${rom_fixed}" "${sig}" "${APP_PUB}" "${b0_bin}" "${out_bin}"
    mcuboot_fixsig "${rom_fixed}" "${sig}" "${APP_PUB}" "${b0_hex}" "${out_hex}"
}

log "Assembling MCUboot images (MCUBOOT key)"
apply_mcuboot_slot "${PROV_S0_ADDR}" mcuboot_s0 \
    "${B0_S0_BIN}" "${B0_S0_HEX}" "signed_by_mcuboot_and_b0_mcuboot"
apply_mcuboot_slot "${PROV_S1_ADDR}" mcuboot_s1 \
    "${B0_S1_BIN}" "${B0_S1_HEX}" "signed_by_mcuboot_and_b0_mcuboot_s1_variant"

ok "MCUboot images assembled."
log "  ${RELEASE_DIR}/signed_by_mcuboot_and_b0_mcuboot.hex/.bin"
log "  ${RELEASE_DIR}/signed_by_mcuboot_and_b0_mcuboot_s1_variant.hex/.bin"
