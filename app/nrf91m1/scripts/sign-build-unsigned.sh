#!/usr/bin/env bash
#
# sign-build-unsigned.sh - build stage (no private keys)
#
# Builds b0 + MCUboot + app, exports the unsigned artifacts, and writes the
# Vault signing requests (manifest-tosign.env) in a single step. No production
# private key and no Vault are involved.
# The real MCUboot app PUBLIC key (committed in nrf91m1/certs/{env}/mcuboot/) is
# baked in so MCUboot will later verify the Vault-signed app.
#
# Produces (under signing-out/unsigned/):
#   b0.hex                  immutable bootloader               (full build only)
#   mcuboot_s0.hex          unsigned MCUboot, S0 variant       (full build only)
#   mcuboot_s1.hex          unsigned MCUboot, S1 variant       (full build only)
#   app_unsigned.hex        unsigned TF-M + app merge
#   manifest-tosign.env     signing parameters + Vault signing requests
#
# Options:
#   --override-mcuboot-version N  override MCUboot monotonic firmware version
#   --override-app-version X      override app image version
#   --build-dir DIR               build directory              (default: ${BUILD_DIR})
#   --dev                         use development certificates
#   --production                  use production certificates
#   --use-existing                harvest from an existing build dir (skip west build)
#   --app-update-only             update app only; skip bootloader artifacts and NSIB signing
#   --b0-key-name NAME            B0 Vault key to request for NSIB signing
#                                 (default: lowest version)
#   --next-mcuboot-key NAME       key rotation: also bake NAME into MCUboot; sign app with current key
#   --mcuboot-key-name NAME       key rotation: override active key (bakes NAME only, signs with NAME)


set -euo pipefail
SCRIPT_NAME="build-unsigned"
. "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/common.sh"

MCUBOOT_VERSION=""
APP_VERSION=""
USE_EXISTING=0
APP_UPDATE_ONLY=0
B0_KEY_NAME=""
NEXT_MCUBOOT_KEY=""
_mcuboot_key_explicit=0
TOSIGN=""

while [ $# -gt 0 ]; do
    case "$1" in
        --override-mcuboot-version) MCUBOOT_VERSION="${2:?}"; shift 2 ;;
        --override-app-version) APP_VERSION="${2:?}"; shift 2 ;;
        --build-dir)        BUILD_DIR="${2:?}"; shift 2 ;;
        --dev)              CERTS_ENV=dev; shift ;;
        --production)       CERTS_ENV=prod; shift ;;
        --use-existing)     USE_EXISTING=1; shift ;;
        --app-update-only)  APP_UPDATE_ONLY=1; shift ;;
        --b0-key-name)               B0_KEY_NAME="${2:?}"; shift 2 ;;
        --next-mcuboot-key)          NEXT_MCUBOOT_KEY="${2:?}"; shift 2 ;;
        --mcuboot-key-name)          MCUBOOT_KEY_NAME="${2:?}"; _mcuboot_key_explicit=1; shift 2 ;;
        --output)                    TOSIGN="${2:?}"; shift 2 ;;
        -h|--help)                   sed -n '2,32p' "$0"; exit 0 ;;
        *)                           err "unknown argument: $1 (see --help)" ;;
    esac
done
TOSIGN="${TOSIGN:-${UNSIGNED_DIR}/${TOSIGN_FILE_NAME}}"

[ -n "${CERTS_ENV:-}" ] \
    || err "certificate environment required: pass --dev or --production"

[ "${USE_EXISTING}" -eq 1 ] && \
    [ -n "${MCUBOOT_VERSION}${APP_VERSION}" -o "${CERTS_ENV}" = "prod" ] && \
    err "--override-mcuboot-version / --override-app-version / --production" \
        "require a rebuild; cannot combine with --use-existing"

# Re-derive cert dirs from CERTS_ENV now that argument parsing is complete.
B0_CERTS_DIR="${APP_DIR}/nrf91m1/certs/${CERTS_ENV}/b0"
MCUBOOT_CERTS_DIR="${APP_DIR}/nrf91m1/certs/${CERTS_ENV}/mcuboot"

# Auto-discover the active MCUboot key from the lowest cert in MCUBOOT_CERTS_DIR
# unless --mcuboot-key-name was given explicitly.
if [ "${_mcuboot_key_explicit}" -eq 0 ]; then
    MCUBOOT_KEY_NAME="$(discover_mcuboot_key)" \
        || err "no MCUboot cert found in ${MCUBOOT_CERTS_DIR}; add a cert or use --mcuboot-key-name"
fi

[ "${USE_EXISTING}" -eq 1 ] && [ -n "${NEXT_MCUBOOT_KEY}" ] \
    && err "--next-mcuboot-key requires a fresh build; cannot combine with --use-existing"

# The MCUboot app PUBLIC key must be present so it gets baked into MCUboot.
require_python_imgtool
[ "${APP_UPDATE_ONLY}" -eq 0 ] && require_file "${HASH_PY}" "hash.py"
require_file "$(mcuboot_pub_pem)" \
    "MCUboot app public key ($(mcuboot_pub_pem)). Committed in nrf91m1/certs/${CERTS_ENV}/mcuboot/."
[ -n "${NEXT_MCUBOOT_KEY}" ] \
    && require_file "$(mcuboot_pub_pem "${NEXT_MCUBOOT_KEY}")" \
       "next MCUboot key cert ($(mcuboot_pub_pem "${NEXT_MCUBOOT_KEY}"))."

# --- Build (unless harvesting an existing build) -----------------------------

if [ "${USE_EXISTING}" -eq 0 ]; then
    require_cmd west "Activate your NCS environment first."
    # MCUBOOT_BAKE_PUBKEY (handled by APP_DIR/sysbuild.cmake) makes BOTH MCUboot
    # variants bake the real app-verification PUBLIC key, WITHOUT touching
    # SB_CONFIG_BOOT_SIGNATURE_KEY_FILE. So every in-build signing step (app,
    # MCUboot update) still uses the default debug PRIVATE key and succeeds -
    # those signed outputs are discarded; we harvest the unsigned payloads. NSIB
    # also uses the debug key here (discarded); the unsigned payloads are signed
    # later in a secure environment.
    MCUBOOT_PUB="$(mcuboot_pub_pem)"
    WEST_CMD=(
        west build --pristine -T ./serial_modem.nrf91m1 \
        --board "${BOARD}" \
        --build-dir "${BUILD_DIR}" \
        --sysbuild "${APP_DIR}" \
        -- \
        "-DMCUBOOT_BAKE_PUBKEY=${MCUBOOT_PUB}"
    )
    if [ -n "${NEXT_MCUBOOT_KEY}" ]; then
        WEST_CMD+=("-DMCUBOOT_BAKE_PUBKEY_2=$(mcuboot_pub_pem "${NEXT_MCUBOOT_KEY}")")
        log "Key rotation: baking ${MCUBOOT_KEY_NAME} (current) + ${NEXT_MCUBOOT_KEY} (next) into MCUboot"
    fi
    [ -n "${MCUBOOT_VERSION}" ] && \
        WEST_CMD+=("-Dmcuboot_CONFIG_FW_INFO_FIRMWARE_VERSION=${MCUBOOT_VERSION}")
    [ -n "${APP_VERSION}" ] && \
        WEST_CMD+=("-Dapp_CONFIG_MCUBOOT_IMGTOOL_SIGN_VERSION=${APP_VERSION}")
    log "Building unsigned artifacts (real app pubkey baked into MCUboot; debug keys discarded)..."
    printf '%s ' "${WEST_CMD[@]}" >&2; echo >&2
    "${WEST_CMD[@]}"

    # Self-check: the real app public key was baked into BOTH MCUboot variants.
    for img in mcuboot mcuboot_s1_variant; do
        cfg="${BUILD_DIR}/${img}/zephyr/.config"
        if [ -f "${cfg}" ] && \
                ! grep -qF "CONFIG_BOOT_SIGNATURE_KEY_FILE=\"${MCUBOOT_PUB}\"" "${cfg}" && \
                ! grep -qF "CONFIG_BOOT_SIGNATURE_KEY_FILE=\"${MCUBOOT_PUB}," "${cfg}"; then
            err "MCUboot image '${img}' did NOT bake ${MCUBOOT_PUB}
       (found: $(grep -E '^CONFIG_BOOT_SIGNATURE_KEY_FILE=' "${cfg}" || echo '<unset>')).
       SB_CONFIG_BOOT_SIGNATURE_KEY_FILE did not propagate. Aborting."
        fi
        if [ -n "${NEXT_MCUBOOT_KEY}" ]; then
            _next_pub="$(mcuboot_pub_pem "${NEXT_MCUBOOT_KEY}")"
            if [ -f "${cfg}" ] && ! grep -qF ",${_next_pub}" "${cfg}"; then
                err "MCUboot image '${img}' did NOT bake second key ${_next_pub}
       (found: $(grep -E '^CONFIG_BOOT_SIGNATURE_KEY_FILE=' "${cfg}" || echo '<unset>')).
       Key rotation second key did not propagate. Aborting."
            fi
        fi
    done
    ok "Verified: MCUboot S0/S1 bake the real app pubkey (build's own signatures are discarded)."
else
    log "Harvesting from existing build dir: ${BUILD_DIR}"
fi

# --- Harvest unsigned binaries -----------------------------------------------

mkdir -p "${UNSIGNED_DIR}"
harvest() {  # harvest <src> <dst-name>
    require_file "$1" "unsigned build artifact for $2"
    cp "$1" "${UNSIGNED_DIR}/$2"
}
harvest "${BUILD_DIR}/app/zephyr/tfm_merged.hex"            "app_unsigned.hex"
if [ "${APP_UPDATE_ONLY}" -eq 0 ]; then
    harvest "${BUILD_DIR}/b0/zephyr/zephyr.hex"                 "b0.hex"
    harvest "${BUILD_DIR}/mcuboot/zephyr/zephyr.hex"            "mcuboot_s0.hex"
    harvest "${BUILD_DIR}/mcuboot_s1_variant/zephyr/zephyr.hex" "mcuboot_s1.hex"
fi

# --- Capture signing parameters from build.ninja into the manifest ----------
NINJA="${BUILD_DIR}/build.ninja"
require_file "${NINJA}" "build.ninja"
field() {  # field <command-substr> <flag-name-without-dashes>  -> value after the flag
    grep -rhoE "$1[^&]*" "${NINJA}" 2>/dev/null \
        | grep -oE "[-][-]$2 [^ ]+" | head -1 | awk '{print $2}'
}

MAGIC_VALUE="$(field 'validation_data\.py' 'magic-value')"
VAL_SKIP="$(field 'validation_data\.py' 'skip')"
VAL_OFFSET="$(field 'validation_data\.py' 'offset')"
PROV_S0_ADDR="$(field 'provision\.py' 's0-addr')"
PROV_S1_ADDR="$(field 'provision\.py' 's1-addr')"
PROV_ADDR="$(field 'provision\.py' 'provision-addr')"
PROV_MAX_SIZE="$(field 'provision\.py' 'max-size')"
PROV_COUNTER_SLOTS="$(field 'provision\.py' 'num-counter-slots-version')"
PROV_OTP_WIDTH="$(field 'provision\.py' 'otp-write-width')"

# Parse all flags from the MCUboot imgtool sign command (S0 slot, rom-fixed 0x8000).
_mb_cmd="$(grep -rhoE 'imgtool\.py sign[^&]*rom-fixed 0x8000[^&]*' \
    "${NINJA}" 2>/dev/null | head -1)"
_mbf() { printf '%s' "${_mb_cmd}" | grep -oE "[-][-]$1 [^ ]+" | awk '{print $2}'; }
MCUBOOT_SLOT_SIZE="$(_mbf slot-size)"
MCUBOOT_HEADER_SIZE="$(_mbf header-size)"
MCUBOOT_ALIGN="$(_mbf align)"
MCUBOOT_VERSION="${MCUBOOT_VERSION:-$(grep -oE \
    'CONFIG_FW_INFO_FIRMWARE_VERSION=[0-9]+' \
    "${BUILD_DIR}/mcuboot/zephyr/.config" 2>/dev/null | cut -d= -f2)}"

# Parse all flags from the app imgtool sign command (tfm_merged).
_app_cmd="$(grep -hoE 'imgtool\.py sign[^&]*tfm_merged[^&]*' \
    "${BUILD_DIR}/app/build.ninja" 2>/dev/null | head -1)"
_af() { printf '%s' "${_app_cmd}" | grep -oE "[-][-]$1 [^ ]+" | awk '{print $2}'; }
APP_SLOT_SIZE="$(_af slot-size)"
APP_HEADER_SIZE="$(_af header-size)"
APP_ALIGN="$(_af align)"
APP_LOAD_ADDR="$(grep -hoE 'app\.signed\.binload_address=0x[0-9a-fA-F]+' "${NINJA}" 2>/dev/null \
    | head -1 | grep -oE '0x[0-9a-fA-F]+')"
APP_VERSION="${APP_VERSION:-$(_af version)}"

# Board/SoC split from BOARD (e.g. nrf9151dk/nrf9151/ns).
BOARD_NAME="$(echo "${BOARD}" | cut -d/ -f1)"
SOC_NAME="$(echo "${BOARD}" | cut -d/ -f2)"

# sdk-nrf revision this bundle was built against; recorded so the signing stage
# can verify it is using matching tooling.
NCS_REVISION="$(git -C "${NCS_DIR}/nrf" rev-parse HEAD 2>/dev/null || true)"

# --- Write manifest-tosign.env with build parameters + Vault signing requests
WORK="$(mktemp -d)"; trap 'rm -rf "${WORK}"' EXIT
mkdir -p "$(dirname "${TOSIGN}")"

# Discover all B0 keys in ascending version order; default to the first (V0).
B0_KEY_NAMES_VAL="$(discover_b0_keys | tr '\n' ' ' | sed 's/ $//')"
B0_KEY_NAME="${B0_KEY_NAME:-${B0_KEY_NAMES_VAL%% *}}"

cat > "${TOSIGN}" <<EOF
# Signing parameters + Vault requests produced by sign-build-unsigned.sh.
# Non-secret, board/config-derived values. Safe to publish alongside artifacts.
BOARD_NAME="${BOARD_NAME}"
SOC_NAME="${SOC_NAME}"
NCS_REVISION="${NCS_REVISION}"
CERTS_ENV="${CERTS_ENV}"
B0_KEY_NAMES="${B0_KEY_NAMES_VAL}"

# NSIB (B0 signs MCUboot)
MAGIC_VALUE="${MAGIC_VALUE}"
VAL_SKIP="${VAL_SKIP}"
VAL_OFFSET="${VAL_OFFSET}"
MCUBOOT_VERSION="${MCUBOOT_VERSION}"
MCUBOOT_SLOT_SIZE="${MCUBOOT_SLOT_SIZE}"
MCUBOOT_HEADER_SIZE="${MCUBOOT_HEADER_SIZE}"
MCUBOOT_ALIGN="${MCUBOOT_ALIGN}"

# B0 provisioning (public key hash list)
PROV_S0_ADDR="${PROV_S0_ADDR}"
PROV_S1_ADDR="${PROV_S1_ADDR}"
PROV_ADDR="${PROV_ADDR}"
PROV_MAX_SIZE="${PROV_MAX_SIZE}"
PROV_COUNTER_SLOTS="${PROV_COUNTER_SLOTS}"
PROV_OTP_WIDTH="${PROV_OTP_WIDTH}"

# Application (MCUboot signs the app); load address captured from build.
APP_VERSION="${APP_VERSION}"
APP_SLOT_SIZE="${APP_SLOT_SIZE}"
APP_HEADER_SIZE="${APP_HEADER_SIZE}"
APP_ALIGN="${APP_ALIGN}"
APP_LOAD_ADDR="${APP_LOAD_ADDR}"
EOF

[ -n "${MAGIC_VALUE}" ]       || err "could not capture --magic-value from build.ninja"
[ -n "${VAL_SKIP}" ]          || err "could not capture --skip from build.ninja"
[ -n "${MCUBOOT_SLOT_SIZE}" ] || err "could not capture MCUboot --slot-size from build.ninja"
[ -n "${MCUBOOT_VERSION}" ]   || \
    err "could not capture CONFIG_FW_INFO_FIRMWARE_VERSION from mcuboot/.config"
[ -n "${PROV_S0_ADDR}" ]      || err "could not capture --s0-addr from build.ninja"
[ -n "${PROV_S1_ADDR}" ]      || err "could not capture --s1-addr from build.ninja"
[ -n "${PROV_ADDR}" ]         || err "could not capture --provision-addr from build.ninja"
[ -n "${APP_SLOT_SIZE}" ]     || err "could not capture app --slot-size from build.ninja"
[ -n "${APP_LOAD_ADDR}" ]     || err "could not capture app load_address from build.ninja"
[ -n "${APP_VERSION}" ]       || err "could not capture app --version from build.ninja"

# Verify captured parameters against the committed reference.
# Any deviation indicates a partition layout or NCS change requiring review.
SIGNING_PARAMS_REF="${SIGN_DIR}/expected-signing-params.env"
if [ -f "${SIGNING_PARAMS_REF}" ]; then
    _mismatch=0
    while IFS= read -r _line; do
        [[ "${_line}" =~ ^[[:space:]]*(#|$) ]] && continue
        _ref_key="${_line%%=*}"
        _ref_val="${_line#*=}"
        _actual="${!_ref_key:-}"
        if [ "${_actual}" != "${_ref_val}" ]; then
            warn "signing param mismatch: ${_ref_key}=${_actual} (expected ${_ref_val})"
            _mismatch=1
        fi
    done < "${SIGNING_PARAMS_REF}"
    [ "${_mismatch}" -eq 0 ] \
        || err "signing parameters do not match reference;" \
               "update ${SIGNING_PARAMS_REF} if the change is intentional"
    ok "Signing parameters match reference."
fi

# --- Compute signing requests and append to manifest-tosign.env --------------
{
    echo ""
    echo "# === Vault signing requests (see sign-hashes.sh)"
} >> "${TOSIGN}"

ITEMS=()
add_request() {  # <name> <key-name> <prehashed> <input-b64>
    local name="$1" key="$2" prehashed="$3" b64="$4"
    ITEMS+=("${name}")
    cat >> "${TOSIGN}" <<EOF
SIGN_${name}_KEY="${key}"
SIGN_${name}_PREHASHED="${prehashed}"
SIGN_${name}_HASH_ALGORITHM="sha2-256"
SIGN_${name}_MARSHALING="asn1"
SIGN_${name}_INPUT_B64="${b64}"
EOF
}

if [ "${APP_UPDATE_ONLY}" -eq 0 ]; then
    log "Hashing MCUboot slots (hash.py) for B0 to sign (key: ${B0_KEY_NAME})"
    nsib_hash "${UNSIGNED_DIR}/mcuboot_s0.hex" "${WORK}/s0.hash"
    nsib_hash "${UNSIGNED_DIR}/mcuboot_s1.hex" "${WORK}/s1.hash"
    add_request nsib_s0 "${B0_KEY_NAME}" false "$(base64 < "${WORK}/s0.hash" | tr -d '\n')"
    add_request nsib_s1 "${B0_KEY_NAME}" false "$(base64 < "${WORK}/s1.hash" | tr -d '\n')"
fi

log "Computing app image digest (imgtool) for MCUBOOT to sign (key: ${MCUBOOT_KEY_NAME})"
app_digest "$(mcuboot_pub_pem)" \
    "${UNSIGNED_DIR}/app_unsigned.hex" "${WORK}/app.digest"
add_request app "${MCUBOOT_KEY_NAME}" true "$(base64 < "${WORK}/app.digest" | tr -d '\n')"

echo "SIGN_ITEMS=\"${ITEMS[*]}\"" >> "${TOSIGN}"

ok "Unsigned artifact bundle -> ${UNSIGNED_DIR}"
log "  $(cd "${UNSIGNED_DIR}" && ls -1 | tr '\n' ' ')"
log "Signing requests (${#ITEMS[@]}): ${ITEMS[*]} -> ${TOSIGN}"
log "Next (in the secure env): ./sign-hashes.sh --in ${TOSIGN_FILE_NAME} --out manifest-signed.env"
