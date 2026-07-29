#!/usr/bin/env bash
#
# sign-assemble.sh - apply the Vault signatures (manifest-signed.env, produced by
# sign-hashes.sh in the secure env) to the unsigned bundle and build the final
# flashable images.
#
# Emits (under signing-out/release/):
#   full build:        app_signed.hex/.bin, full.hex, manifest.env
#   --app-update-only: app_signed.hex/.bin, manifest.env
#
# Options:
#   --signed FILE         manifest-signed.env. Default: ${OUT_DIR}/manifest-signed.env
#   --unsigned-dir DIR    Input bundle dir.    Default: ${UNSIGNED_DIR}
#   --app-update-only     Sign app only; skip bootloader assembly
#                         (pair with sign-build-unsigned.sh --app-update-only).

set -euo pipefail
SCRIPT_NAME="sign-assemble"
. "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/common.sh"

SIGNED="${OUT_DIR}/manifest-signed.env"
APP_UPDATE_ONLY=0
while [ $# -gt 0 ]; do
    case "$1" in
        --signed)          SIGNED="${2:?}"; shift 2 ;;
        --unsigned-dir)    UNSIGNED_DIR="${2:?}"; shift 2 ;;
        --app-update-only) APP_UPDATE_ONLY=1; shift ;;
        -h|--help)         sed -n '2,28p' "$0"; exit 0 ;;
        *)                 err "unknown argument: $1 (see --help)" ;;
    esac
done

# --- Preconditions: toolchain, committed public keys, Vault signatures -------

require_python_imgtool
require_file "${MERGEHEX}" "mergehex.py"
require_file "${SIGNED}" "signed manifest (run sign-hashes.sh in the secure env)"

# Read all required parameters from the signed manifest.
SIGN_ITEMS="$(get_field SIGN_ITEMS "${SIGNED}")" \
    || err "${SIGNED} has no SIGN_ITEMS / signatures"
[ -n "${SIGN_ITEMS}" ] || err "${SIGNED} has an empty SIGN_ITEMS"

CERTS_ENV="$(get_field CERTS_ENV "${SIGNED}")"             || err "manifest missing CERTS_ENV"
APP_VERSION="$(get_field APP_VERSION "${SIGNED}")"         || err "manifest missing APP_VERSION"
APP_SLOT_SIZE="$(get_field APP_SLOT_SIZE "${SIGNED}")"     || err "manifest missing APP_SLOT_SIZE"
APP_HEADER_SIZE="$(get_field APP_HEADER_SIZE "${SIGNED}")" || err "manifest missing APP_HEADER_SIZE"
APP_ALIGN="$(get_field APP_ALIGN "${SIGNED}")"             || err "manifest missing APP_ALIGN"
APP_LOAD_ADDR="$(get_field APP_LOAD_ADDR "${SIGNED}")"     || err "manifest missing APP_LOAD_ADDR"

if [ "${APP_UPDATE_ONLY}" -eq 0 ]; then
    MAGIC_VALUE="$(get_field MAGIC_VALUE "${SIGNED}")"   || err "manifest missing MAGIC_VALUE"
    VAL_SKIP="$(get_field VAL_SKIP "${SIGNED}")"         || err "manifest missing VAL_SKIP"
    VAL_OFFSET="$(get_field VAL_OFFSET "${SIGNED}")"     || VAL_OFFSET="0"
    B0_KEY_NAMES="$(get_field B0_KEY_NAMES "${SIGNED}")" || err "manifest missing B0_KEY_NAMES"
    PROV_S0_ADDR="$(get_field PROV_S0_ADDR "${SIGNED}")" || err "manifest missing PROV_S0_ADDR"
    PROV_S1_ADDR="$(get_field PROV_S1_ADDR "${SIGNED}")" || err "manifest missing PROV_S1_ADDR"
    PROV_ADDR="$(get_field PROV_ADDR "${SIGNED}")"       || err "manifest missing PROV_ADDR"
    PROV_MAX_SIZE="$(get_field PROV_MAX_SIZE "${SIGNED}")" || err "manifest missing PROV_MAX_SIZE"
    PROV_COUNTER_SLOTS="$(get_field PROV_COUNTER_SLOTS "${SIGNED}")" \
        || err "manifest missing PROV_COUNTER_SLOTS"
    PROV_OTP_WIDTH="$(get_field PROV_OTP_WIDTH "${SIGNED}")" \
        || err "manifest missing PROV_OTP_WIDTH"
fi

# Re-derive cert dirs from the manifest's CERTS_ENV.
B0_CERTS_DIR="${APP_DIR}/nrf91m1/certs/${CERTS_ENV}/b0"
MCUBOOT_CERTS_DIR="${APP_DIR}/nrf91m1/certs/${CERTS_ENV}/mcuboot"

_app_key="$(get_field "SIGN_app_KEY" "${SIGNED}")" || _app_key="${MCUBOOT_KEY_NAME}"
[[ "${_app_key}" =~ ^[A-Za-z0-9_-]+$ ]] \
    || err "unexpected key name in SIGN_app_KEY: '${_app_key}'"
APP_PUB="$(mcuboot_pub_pem "${_app_key}")"
require_file "${APP_PUB}" "committed MCUboot public key for ${_app_key} (${MCUBOOT_CERTS_DIR}/)"
if [ "${APP_UPDATE_ONLY}" -eq 0 ]; then
    require_file "${VALIDATION_DATA_PY}" "validation_data.py"
    require_file "${PROVISION_PY}" "provision.py"
    NSIB_PUB="$(b0_pub_pem "${NSIB_KEY_NAME}")"
    require_file "${NSIB_PUB}" "committed B0 public key (${B0_CERTS_DIR}/)"
fi

# Populate NSIB_KEY_NAMES from B0_KEY_NAMES recorded in the manifest.
NSIB_KEY_NAMES=()
for _k in ${B0_KEY_NAMES:-${NSIB_KEY_NAME}}; do NSIB_KEY_NAMES+=("${_k}"); done

# Derive which B0 public key was used for signing from the signed manifest.
if [ "${APP_UPDATE_ONLY}" -eq 0 ]; then
    _nsib_key="$(get_field "SIGN_nsib_s0_KEY" "${SIGNED}")" || _nsib_key="${NSIB_KEY_NAME}"
    [[ "${_nsib_key}" =~ ^[A-Za-z0-9_-]+$ ]] \
        || err "unexpected key name in SIGN_nsib_s0_KEY: '${_nsib_key}'"
    NSIB_PUB="$(b0_pub_pem "${_nsib_key}")"
    require_file "${NSIB_PUB}" "committed B0 public key for ${_nsib_key} (${B0_CERTS_DIR}/)"
fi

U_APP="${UNSIGNED_DIR}/app_unsigned.hex"
require_file "${U_APP}" "unsigned artifact"

mkdir -p "${RELEASE_DIR}"
WORK="$(mktemp -d)"; trap 'rm -rf "${WORK}"' EXIT

# Extract a Vault signature for an item, stripping the vault:vN: version prefix.
sig_b64der() {  # <item-name> -> stdout: base64 DER
    local item="$1" raw
    raw="$(get_field "SIGN_${item}_SIG" "${SIGNED}")" \
        || err "no signature for '${item}' in ${SIGNED}"
    [ -n "${raw}" ] || err "no signature for '${item}' in ${SIGNED}"
    printf '%s' "${raw#vault:v*:}"
}

if [ "${APP_UPDATE_ONLY}" -eq 0 ]; then
    log "Assembling full release (NSIB key: ${NSIB_KEY_NAME}, MCUboot app key: ${MCUBOOT_KEY_NAME})"

    U_B0="${UNSIGNED_DIR}/b0.hex"
    U_S0="${UNSIGNED_DIR}/mcuboot_s0.hex"
    U_S1="${UNSIGNED_DIR}/mcuboot_s1.hex"
    for f in "${U_B0}" "${U_S0}" "${U_S1}"; do require_file "$f" "unsigned artifact"; done

    # --- 1. Generate B0 provisioning data -----------------------------------
    PROV_PUBS=""
    for k in "${NSIB_KEY_NAMES[@]}"; do
        PROV_PUBS="${PROV_PUBS:+${PROV_PUBS},}$(b0_pub_pem "$k")"
    done
    PROVISION_HEX="${RELEASE_DIR}/provision.hex"
    log "Generating B0 provisioning (provision.py) with ${#NSIB_KEY_NAMES[@]} public key(s)"
    gen_provision "${PROV_PUBS}" "${PROVISION_HEX}"

    # --- 2. Apply B0 NSIB signatures to MCUboot S0/S1 ----------------------
    apply_mcuboot_slot() {  # <unsigned-hex> <item> <out-name>
        local in_hex="$1" item="$2" out_name="$3"
        local sigraw="${WORK}/${out_name}.sigraw"
        local out_hex="${RELEASE_DIR}/${out_name}.hex" out_bin="${RELEASE_DIR}/${out_name}.bin"
        log "  ${out_name}: apply B0 signature -> validation_data"
        der_b64_to_raw "$(sig_b64der "${item}")" "${sigraw}"
        nsib_validation "${in_hex}" "${sigraw}" "${NSIB_PUB}" "${out_hex}" "${out_bin}"
    }
    apply_mcuboot_slot "${U_S0}" nsib_s0 "signed_by_b0_mcuboot"
    apply_mcuboot_slot "${U_S1}" nsib_s1 "signed_by_b0_mcuboot_s1_variant"
fi

# --- Apply the MCUboot signature to the application (both modes) ------------
APP_SIG="${WORK}/app.sig.b64"
APP_HEX="${RELEASE_DIR}/app_signed.hex"
APP_BIN="${RELEASE_DIR}/app_signed.bin"
log "  app: apply MCUBOOT signature -> fix-sig"
sig_b64der app > "${APP_SIG}"
app_fixsig "${APP_SIG}" "${APP_PUB}" "${U_APP}" "${APP_HEX}"
app_fixsig "${APP_SIG}" "${APP_PUB}" "${U_APP}" "${APP_BIN}"
"${PYTHON}" "${IMGTOOL}" verify -k "${APP_PUB}" "${APP_HEX}" >/dev/null \
    || err "app signature verification FAILED"


if [ "${APP_UPDATE_ONLY}" -eq 1 ]; then
    cp "${SIGNED}" "${RELEASE_DIR}/${MANIFEST_FILE_NAME}"
    ok "App update assembled."
    log "Signed app : ${APP_HEX} / ${APP_BIN}"
    log "${APP_BIN} is ready for the FOTA service."
else
    # --- Merge full flashable image ------------------------------------------
    FULL_HEX="${RELEASE_DIR}/full.hex"
    log "Merging full flashable image -> ${FULL_HEX}"
    "${PYTHON}" "${MERGEHEX}" -o "${FULL_HEX}" \
        "${U_B0}" "${PROVISION_HEX}" \
        "${RELEASE_DIR}/signed_by_b0_mcuboot.hex" \
        "${RELEASE_DIR}/signed_by_b0_mcuboot_s1_variant.hex" \
        "${APP_HEX}"

    cp "${SIGNED}" "${RELEASE_DIR}/${MANIFEST_FILE_NAME}"

    ok "Release assembled."
    log "Signed app          : ${APP_HEX} / ${APP_BIN}"
    log "Full flashable image: ${FULL_HEX}"
fi
