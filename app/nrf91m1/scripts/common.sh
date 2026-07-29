#!/usr/bin/env bash
#
# Shared configuration and helpers for the signing flow.
# Sourced by sign-build-unsigned.sh, sign-assemble.sh, sign-prepare-mcuboot.sh and
# sign-assemble-mcuboot.sh. Not meant to be run directly.
#

# --- Paths -------------------------------------------------------------------

SIGN_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
APP_DIR="${APP_DIR:-$(cd "${SIGN_DIR}/../.." && pwd)}"
NCS_DIR="${NCS_DIR:-$(cd "${APP_DIR}/../.." && pwd)}"

BOARD="${BOARD:-nrf9151dk/nrf9151/ns}"

BUILD_DIR="${BUILD_DIR:-${APP_DIR}/build}"
OUT_DIR="${OUT_DIR:-${APP_DIR}/signing-out}"
UNSIGNED_DIR="${UNSIGNED_DIR:-${OUT_DIR}/unsigned}"  # sign-build-unsigned.sh output bundle
RELEASE_DIR="${RELEASE_DIR:-${OUT_DIR}/release}"     # sign-assemble.sh output (signed images)
MANIFEST_FILE_NAME="manifest.env"                    # release manifest
TOSIGN_FILE_NAME="manifest-tosign.env"               # build params + Vault signing requests

# --- Signing toolchain -------------------------------------------------------
# All tools default to paths inside an NCS west workspace.

PYTHON="${PYTHON:-python3}"
IMGTOOL="${IMGTOOL:-${NCS_DIR}/bootloader/mcuboot/scripts/imgtool.py}"
MERGEHEX="${MERGEHEX:-${NCS_DIR}/zephyr/scripts/build/mergehex.py}"
HASH_PY="${HASH_PY:-${NCS_DIR}/nrf/scripts/bootloader/hash.py}"
VALIDATION_DATA_PY="${VALIDATION_DATA_PY:-${NCS_DIR}/nrf/scripts/bootloader/validation_data.py}"
PROVISION_PY="${PROVISION_PY:-${NCS_DIR}/nrf/scripts/bootloader/provision.py}"

# --- Vault key names (the actual signing happens in the secure env) ----------
NSIB_KEY_NAME="${NSIB_KEY_NAME:-B0_V0}"          # active B0 key (default; overridden from manifest)
NSIB_KEY_NAMES=("${NSIB_KEY_NAME}")  # provisioning trusted-key list (overridden from manifest)
# MCUboot signs the application image and the B0-signed MCUboot images.
MCUBOOT_KEY_NAME="${MCUBOOT_KEY_NAME:-MCUBOOT_V0}"

# B0 certificate directory — dev or prod, selected by CERTS_ENV.
# Contains B0_V0.pem, B0_V1.pem, … committed public keys whose
# private counterparts live in Vault. Filenames are the Vault key names.
# All certs found are provisioned into B0; the lowest version (V0) is
# used as the active signing key by default.
CERTS_ENV="${CERTS_ENV:-}"
B0_CERTS_DIR="${B0_CERTS_DIR:-${APP_DIR}/nrf91m1/certs/${CERTS_ENV}/b0}"
MCUBOOT_CERTS_DIR="${MCUBOOT_CERTS_DIR:-${APP_DIR}/nrf91m1/certs/${CERTS_ENV}/mcuboot}"

# discover_b0_keys: emit Vault key names for all B0 certs in B0_CERTS_DIR,
# ascending by version. Filename = key name (B0_V0.pem -> B0_V0).
discover_b0_keys() {
    local dir="${1:-${B0_CERTS_DIR}}"
    for f in $(ls "${dir}"/*.pem 2>/dev/null | sort -V); do
        basename "${f}" .pem
    done
}

# discover_mcuboot_key: emit the Vault key name for the lowest-versioned MCUboot
# cert in MCUBOOT_CERTS_DIR. Filename = key name (MCUBOOT_V0.pem -> MCUBOOT_V0).
discover_mcuboot_key() {
    local dir="${1:-${MCUBOOT_CERTS_DIR}}"
    local f
    f="$(ls "${dir}"/*.pem 2>/dev/null | sort -V | head -1)" || return 1
    [ -n "${f}" ] || return 1
    basename "${f}" .pem
}

# --- Public verification keys -----------------------------------------------
# b0_pub_pem:      B0 key   from B0_CERTS_DIR      (nrf91m1/certs/{env}/b0/B0_V0.pem)
b0_pub_pem() {
    local key="$1"
    [[ "${key}" =~ ^[A-Za-z0-9_-]+$ ]] \
        || err "b0_pub_pem: unsafe key name '${key}' (expected alphanumeric/underscore/hyphen)"
    echo "${B0_CERTS_DIR}/${key}.pem"
}
# mcuboot_pub_pem: MCUboot key from MCUBOOT_CERTS_DIR (nrf91m1/certs/{env}/mcuboot/<KEY>.pem)
# Optional arg: Vault key name (defaults to MCUBOOT_KEY_NAME).
mcuboot_pub_pem() {
    local key="${1:-${MCUBOOT_KEY_NAME}}"
    [[ "${key}" =~ ^[A-Za-z0-9_-]+$ ]] \
        || err "mcuboot_pub_pem: unsafe key name '${key}' (expected alphanumeric/underscore/hyphen)"
    echo "${MCUBOOT_CERTS_DIR}/${key}.pem"
}

# --- Output formatting -------------------------------------------------------

_c_red='\033[31m'; _c_grn='\033[32m'; _c_yel='\033[33m'; _c_cya='\033[36m'; _c_rst='\033[0m'
log()  { printf "${_c_cya}[%s]${_c_rst} %s\n" "${SCRIPT_NAME:-signing}" "$*" >&2; }
ok()   { printf "${_c_grn}[%s] %s${_c_rst}\n" "${SCRIPT_NAME:-signing}" "$*" >&2; }
warn() { printf "${_c_yel}[%s] warning:${_c_rst} %s\n" "${SCRIPT_NAME:-signing}" "$*" >&2; }
err()  { printf "${_c_red}[%s] error:${_c_rst} %s\n" "${SCRIPT_NAME:-signing}" "$*" >&2; exit 1; }

# --- Precondition checks -----------------------------------------------------

require_cmd() {
    command -v "$1" >/dev/null 2>&1 || err "required command '$1' not found in PATH. $2"
}

require_file() {
    [ -f "$1" ] || err "${2:-required file} not found: $1"
}

require_python_imgtool() {
    { [ -x "${PYTHON}" ] || command -v "${PYTHON}" >/dev/null 2>&1; } \
        || err "python interpreter not found: ${PYTHON} (set PYTHON=...)"
    require_file "${IMGTOOL}" "imgtool (set IMGTOOL=...)"
    "${PYTHON}" -c \
        'from cryptography.hazmat.primitives.asymmetric.utils import decode_dss_signature' \
        2>/dev/null \
        || err "Python 'cryptography' package not found (pip install cryptography)"
}

# get_field: read one KEY="VALUE" field from a manifest file as data.
# Returns 1 (and prints nothing) if the key is absent.
get_field() {  # <key> <file> -> stdout
    local key="$1" file="$2" line val
    line="$(grep -m1 "^${key}=" "${file}" 2>/dev/null)" || return 1
    val="${line#"${key}="}"
    val="${val#\"}"
    val="${val%\"}"
    printf '%s' "${val}"
}

# --- Signing operations ------------------------------------------------------
# Helpers used on both sides of each Vault signing trip.
#
# Round 1 (full release or app-update-only):
#   Before  (sign-build-unsigned.sh)      nsib_hash, app_digest
#   [Vault trip — sign-hashes.sh]
#   After   (sign-assemble.sh)           gen_provision, der_b64_to_raw,
#                                        nsib_validation, app_fixsig
#
# Round 2 (MCUboot update releases only):
#   Before  (sign-prepare-mcuboot.sh)    mcuboot_digest
#   [Vault trip — sign-hashes.sh]
#   After   (sign-assemble-mcuboot.sh)   mcuboot_fixsig
#
# All functions read parameters (VAL_SKIP, MAGIC_VALUE, APP_*, PROV_*, …)
# from the sourced manifest.

# -- Argument builder (internal) ---

# app_sign_args: populate APP_ARGS with imgtool sign flags for the app slot.
app_sign_args() {
    APP_ARGS=( sign --version "${APP_VERSION}" --slot-size "${APP_SLOT_SIZE}"
               --header-size "${APP_HEADER_SIZE}" --pad-header --align "${APP_ALIGN}"
               --rom-fixed "${APP_LOAD_ADDR}" )
}

# -- Round 1: before Vault ---

# nsib_hash: hash an unsigned MCUboot slot → hash file (B0 Vault sign input).
nsib_hash() {  # <in_hex> <out_hashfile>
    local in_hex="$1" out_hashfile="$2"
    "${PYTHON}" "${HASH_PY}" --in "${in_hex}" --skip "${VAL_SKIP}" > "${out_hashfile}"
}

# app_digest: compute the imgtool signing digest for the app → MCUBOOT Vault sign input.
app_digest() {  # <pubkey> <unsigned_app> <out_digest>
    local pubkey="$1" unsigned_app="$2" out_digest="$3"
    app_sign_args
    "${PYTHON}" "${IMGTOOL}" "${APP_ARGS[@]}" \
        -k "${pubkey}" --vector-to-sign digest "${unsigned_app}" "${out_digest}"
}

# -- Round 1: after Vault ---

# gen_provision: generate B0 provision data from public key PEMs → provision.hex.
gen_provision() {  # <pubs_csv> <out_hex>
    local pubs_csv="$1" out_hex="$2"
    "${PYTHON}" "${PROVISION_PY}" \
        --s0-addr "${PROV_S0_ADDR}" --s1-addr "${PROV_S1_ADDR}" \
        --provision-addr "${PROV_ADDR}" \
        --public-key-files "${pubs_csv}" --output "${out_hex}" --max-size "${PROV_MAX_SIZE}" \
        --num-counter-slots-version "${PROV_COUNTER_SLOTS}" \
        --otp-write-width "${PROV_OTP_WIDTH}"
}

# der_b64_to_raw: convert a Vault DER signature (base64) → raw 64-byte r||s file.
der_b64_to_raw() {  # <der_b64> <out_rawfile>
    local der_b64="$1" out_rawfile="$2"
    printf '%s' "${der_b64}" | base64 -d | "${PYTHON}" -c '
import sys
from cryptography.hazmat.primitives.asymmetric.utils import decode_dss_signature
r, s = decode_dss_signature(sys.stdin.buffer.read())
sys.stdout.buffer.write(r.to_bytes(32, "big") + s.to_bytes(32, "big"))' > "${out_rawfile}"
    [ "$(wc -c < "${out_rawfile}" | tr -d ' ')" = "64" ] \
        || err "expected 64-byte raw NSIB signature (${out_rawfile})"
}

# nsib_validation: embed a raw B0 signature into MCUboot slot validation data
# → B0-verifiable image.
nsib_validation() {  # <in_hex> <raw_sigfile> <pubkey> <out_hex> <out_bin>
    local in_hex="$1" raw_sigfile="$2" pubkey="$3" out_hex="$4" out_bin="$5"
    "${PYTHON}" "${VALIDATION_DATA_PY}" \
        --input "${in_hex}" --skip "${VAL_SKIP}" --offset "${VAL_OFFSET}" \
        --signature "${raw_sigfile}" --public-key "${pubkey}" --magic-value "${MAGIC_VALUE}" \
        --output-hex "${out_hex}" --output-bin "${out_bin}"
}

# app_fixsig: apply a Vault MCUBOOT signature to the unsigned app → signed app.
app_fixsig() {  # <sig_b64der_file> <pubkey> <unsigned_app> <out>
    local sig_b64der_file="$1" pubkey="$2" unsigned_app="$3" out="$4"
    app_sign_args
    "${PYTHON}" "${IMGTOOL}" "${APP_ARGS[@]}" \
        --fix-sig "${sig_b64der_file}" --fix-sig-pubkey "${pubkey}" "${unsigned_app}" "${out}"
}

# -- Round 2: before Vault (MCUboot update) ---

# mcuboot_sign_args: populate MCUBOOT_SIGN_ARGS with imgtool sign flags for a MCUboot
# slot. --pad-header not needed as MCUboot binaries already reserve header space.
mcuboot_sign_args() {  # <rom_fixed_addr>
    MCUBOOT_SIGN_ARGS=( sign --version "${MCUBOOT_VERSION}"
                        --slot-size "${MCUBOOT_SLOT_SIZE}"
                        --header-size "${MCUBOOT_HEADER_SIZE}"
                        --align "${MCUBOOT_ALIGN}"
                        --rom-fixed "$1" )
}

# mcuboot_digest: compute the imgtool signing digest for a B0-signed MCUboot slot
# → MCUBOOT Vault sign input.
mcuboot_digest() {  # <rom_fixed_addr> <pubkey> <b0_signed_bin> <out_digest>
    local rom_fixed="$1" pubkey="$2" b0_signed_bin="$3" out_digest="$4"
    mcuboot_sign_args "${rom_fixed}"
    "${PYTHON}" "${IMGTOOL}" "${MCUBOOT_SIGN_ARGS[@]}" \
        -k "${pubkey}" --vector-to-sign digest "${b0_signed_bin}" "${out_digest}"
}

# -- Round 2: after Vault (MCUboot update) ---

# mcuboot_fixsig: apply a Vault MCUBOOT signature to a B0-signed MCUboot slot
# → dual-signed MCUboot image.
mcuboot_fixsig() {  # <rom_fixed_addr> <sig_b64der_file> <pubkey> <b0_signed_in> <out>
    local rom_fixed="$1" sig_b64der_file="$2" pubkey="$3" b0_signed_in="$4" out="$5"
    mcuboot_sign_args "${rom_fixed}"
    "${PYTHON}" "${IMGTOOL}" "${MCUBOOT_SIGN_ARGS[@]}" \
        --fix-sig "${sig_b64der_file}" --fix-sig-pubkey "${pubkey}" "${b0_signed_in}" "${out}"
}
