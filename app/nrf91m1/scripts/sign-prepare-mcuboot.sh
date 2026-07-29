#!/usr/bin/env bash
#
# sign-prepare-mcuboot.sh - compute MCUboot image signing digests from the B0-signed
# MCUboot images and write them as Vault signing requests into manifest-mcuboot-tosign.env.
# Runs after sign-assemble.sh has produced the B0-signed MCUboot images.
#
# Only needed for MCUboot update releases. App-update-only releases do not require this
# step; the single build-unsigned / sign-hashes / sign-assemble trip is sufficient.
#
# The secure environment then signs the requests with Vault (sign-hashes.sh),
# and sign-assemble-mcuboot.sh applies the returned signatures to produce the
# dual-signed signed_by_mcuboot_and_b0_mcuboot images.
#
# Options:
#   --release-dir DIR   Dir with B0-signed MCUboot images. Default: ${RELEASE_DIR}
#   --output FILE       Output manifest. Default: ${OUT_DIR}/manifest-mcuboot-tosign.env

set -euo pipefail
SCRIPT_NAME="sign-prepare-mcuboot"
. "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/common.sh"

TOSIGN=""
while [ $# -gt 0 ]; do
    case "$1" in
        --release-dir) RELEASE_DIR="${2:?}"; shift 2 ;;
        --output)      TOSIGN="${2:?}"; shift 2 ;;
        -h|--help)     sed -n '2,18p' "$0"; exit 0 ;;
        *)             err "unknown argument: $1 (see --help)" ;;
    esac
done
TOSIGN="${TOSIGN:-${OUT_DIR}/manifest-mcuboot-tosign.env}"

# --- Preconditions -----------------------------------------------------------

require_python_imgtool

B0_S0="${RELEASE_DIR}/signed_by_b0_mcuboot.bin"
B0_S1="${RELEASE_DIR}/signed_by_b0_mcuboot_s1_variant.bin"
require_file "${B0_S0}" "B0-signed MCUboot S0 (run sign-assemble.sh first)"
require_file "${B0_S1}" "B0-signed MCUboot S1 variant (run sign-assemble.sh first)"

# Load the release manifest to get signing parameters and the cert environment.
MANIFEST="${RELEASE_DIR}/${MANIFEST_FILE_NAME}"
require_file "${MANIFEST}" "release manifest (run sign-assemble.sh first)"
CERTS_ENV="$(get_field CERTS_ENV "${MANIFEST}")"             || err "manifest missing CERTS_ENV"
PROV_S0_ADDR="$(get_field PROV_S0_ADDR "${MANIFEST}")"       || err "manifest missing PROV_S0_ADDR"
PROV_S1_ADDR="$(get_field PROV_S1_ADDR "${MANIFEST}")"       || err "manifest missing PROV_S1_ADDR"
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
# This ensures the MCUboot update package is signed with the correct key even during
# key rotation, when the lowest cert in MCUBOOT_CERTS_DIR may still be the old key.
MCUBOOT_KEY_NAME="$(get_field SIGN_app_KEY "${MANIFEST}")" \
    || err "manifest missing SIGN_app_KEY (cannot determine MCUboot signing key)"
[[ "${MCUBOOT_KEY_NAME}" =~ ^[A-Za-z0-9_-]+$ ]] \
    || err "unsafe key name in SIGN_app_KEY: '${MCUBOOT_KEY_NAME}'"

APP_PUB="$(mcuboot_pub_pem)"
require_file "${APP_PUB}" "committed MCUboot public key (${MCUBOOT_CERTS_DIR}/)"

WORK="$(mktemp -d)"; trap 'rm -rf "${WORK}"' EXIT
mkdir -p "$(dirname "${TOSIGN}")"

# Build a clean mcuboot manifest: strip all SIGN_* lines from the release manifest
# so that neither the round-1 SIGN_ITEMS nor the round-1 signatures are carried
# through.
grep -v '^SIGN_' "${MANIFEST}" > "${TOSIGN}"
{
    echo ""
    echo "# === MCUboot signing requests (sign-prepare-mcuboot.sh) - sign with Vault"
} >> "${TOSIGN}"

ITEMS=()
add_mcuboot_request() {  # <name> <rom_fixed_addr> <b0_signed_bin>
    local name="$1" rom_fixed="$2" b0_bin="$3"
    ITEMS+=("${name}")
    log "  ${name}: imgtool digest (rom-fixed=${rom_fixed})"
    mcuboot_digest "${rom_fixed}" "${APP_PUB}" "${b0_bin}" "${WORK}/${name}.digest"
    cat >> "${TOSIGN}" <<EOF
SIGN_${name}_KEY="${MCUBOOT_KEY_NAME}"
SIGN_${name}_PREHASHED="true"
SIGN_${name}_HASH_ALGORITHM="sha2-256"
SIGN_${name}_MARSHALING="asn1"
SIGN_${name}_INPUT_B64="$(base64 < "${WORK}/${name}.digest" | tr -d '\n')"
EOF
}

log "Computing MCUboot image signing digests for MCUBOOT key"
add_mcuboot_request mcuboot_s0 "${PROV_S0_ADDR}" "${B0_S0}"
add_mcuboot_request mcuboot_s1 "${PROV_S1_ADDR}" "${B0_S1}"

echo "SIGN_ITEMS=\"${ITEMS[*]}\"" >> "${TOSIGN}"

ok "Prepared ${#ITEMS[@]} MCUboot signing request(s) -> ${TOSIGN}"
log "Requests: ${ITEMS[*]}"
log "Next (in the secure env):" \
    "./sign-hashes.sh --in manifest-mcuboot-tosign.env --out manifest-mcuboot-signed.env"
