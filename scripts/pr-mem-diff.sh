#!/usr/bin/env bash
# Generate a sticky-comment-ready markdown body comparing the PR HEAD app
# image against its merge base
#
# Required env vars: PR_HEAD_ELF PR_HEAD_BUILD_LOG BASE_SHA BOARD MEMORY_REPORT_PATH
#   PR_HEAD_ELF and PR_HEAD_BUILD_LOG are relative to the west workspace dir
#   (parent of this checkout). PR_HEAD_BUILD_LOG is the per-scenario Twister
#   build.log (it carries the "Memory region" table flash%/ram% are read from).
# Optional env vars:
#   CI_RUN_URL   Link to the CI run, embedded in the comment.
#   SCENARIO     Twister scenario for memory report (default: serial_modem.nrf91m1).
#   TWISTER_ROOT Twister testsuite root, relative to the repo (default: app).
#
# The baseline is built the same way CI builds the PR: a single Twister
# scenario, so overlays/Kconfig come from app/sample.yaml (one source of truth)
# and the diff reflects code changes, not config drift.

set -euo pipefail
: "${PR_HEAD_ELF:?}" "${PR_HEAD_BUILD_LOG:?}" "${BASE_SHA:?}" "${BOARD:?}" "${MEMORY_REPORT_PATH:?}"
SCENARIO="${SCENARIO:-serial_modem.nrf91m1}"
TWISTER_ROOT="${TWISTER_ROOT:-app}"

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"       # serial_modem/
WORKSPACE_DIR="$(cd "$REPO_ROOT/.." && pwd)"        # west workspace
MEMORY_REPORT_DIR="$WORKSPACE_DIR/artifacts/size"
mkdir -p "$MEMORY_REPORT_DIR" "$(dirname "$MEMORY_REPORT_PATH")"

PR_SHA=$(git -C "$REPO_ROOT" rev-parse HEAD)

if ! command -v size >/dev/null 2>&1; then
  echo "App memory report unavailable: 'size' (binutils) not found on PATH." \
       "See [CI run](${CI_RUN_URL:-})" > "$MEMORY_REPORT_PATH"
  exit 0
fi

# Extract `flash%` and `ram%` for the app image from a Zephyr build log
extract_app_pct() {
  awk '
    /Memory region[[:space:]]+Used Size/ { flash=""; ram="" }
    $1 == "FLASH:" { flash=$NF }
    $1 == "RAM:"   { ram=$NF }
    /Generating files from.*\/app\/zephyr\/zephyr\.elf/ {
      print flash, ram
      exit
    }
  ' "$1"
}

# Render an ELF's size as a tab-free, fixed-width 7-column table
# (text/data/bss/dec/hex/flash%/ram%). The filename column is dropped so the
# diff isn't dominated by long absolute paths. flash%/ram% come from the
# Memory region report Zephyr prints at link time and are read out of the
# corresponding build log.
size_report() {
  local elf="$1" build_log="$2"
  local flash_pct ram_pct
  flash_pct=""; ram_pct=""
  read -r flash_pct ram_pct < <(extract_app_pct "$build_log") || true
  size -d "$elf" \
    | awk -v fp="${flash_pct:-?}" -v rp="${ram_pct:-?}" '
        NR == 1 {
          printf "%10s %10s %10s %10s %10s %10s %10s\n", \
            "text", "data", "bss", "dec", "hex", "flash%", "ram%"
        }
        NR == 2 {
          printf "%10s %10s %10s %10s %10s %10s %10s\n", \
            $1, $2, $3, $4, $5, fp, rp
        }
      '
}

require_size_file() {
  [ -s "$1" ] && [ "$(wc -l < "$1")" -ge 2 ]
}

restore_pr_checkout() {
  git -C "$REPO_ROOT" checkout --detach "$PR_SHA" 2>/dev/null || true
}

report_failed() {
  trap - ERR EXIT
  restore_pr_checkout
  echo "App memory report failed. See [CI run](${CI_RUN_URL:-})" > "$MEMORY_REPORT_PATH"
  exit 0
}

trap report_failed ERR
trap restore_pr_checkout EXIT

PR_ELF="$WORKSPACE_DIR/$PR_HEAD_ELF"
PR_LOG="$WORKSPACE_DIR/$PR_HEAD_BUILD_LOG"
BASELINE_OUT="$MEMORY_REPORT_DIR/twister-baseline"

[ -f "$PR_ELF" ] && [ -f "$PR_LOG" ] || report_failed
size_report "$PR_ELF" "$PR_LOG" > "$MEMORY_REPORT_DIR/pr.size"
require_size_file "$MEMORY_REPORT_DIR/pr.size" || report_failed

git -C "$REPO_ROOT" fetch --depth=1 origin "$BASE_SHA"
git -C "$REPO_ROOT" checkout --detach "$BASE_SHA"
(cd "$WORKSPACE_DIR" && west update -o=--depth=1 -n)
(cd "$REPO_ROOT" && rm -rf "$BASELINE_OUT" \
  && west twister -T "$TWISTER_ROOT" -s "$SCENARIO" -p "$BOARD" \
       --build-only --overflow-as-errors -O "$BASELINE_OUT")
BASELINE_ELF="$(find "$BASELINE_OUT" -type f -path "*/$SCENARIO/app/zephyr/zephyr.elf" | head -n1)"
BASELINE_LOG="$(find "$BASELINE_OUT" -type f -path "*/$SCENARIO/build.log" | head -n1)"
[ -n "$BASELINE_ELF" ] && [ -f "$BASELINE_ELF" ] || report_failed
size_report "$BASELINE_ELF" "$BASELINE_LOG" > "$MEMORY_REPORT_DIR/baseline.size"
require_size_file "$MEMORY_REPORT_DIR/baseline.size" || report_failed

restore_pr_checkout
trap - ERR EXIT

diff -u0 --label baseline --label pr \
  "$MEMORY_REPORT_DIR/baseline.size" "$MEMORY_REPORT_DIR/pr.size" \
  > "$MEMORY_REPORT_DIR/size_change.diff" || true
if [ -s "$MEMORY_REPORT_DIR/size_change.diff" ]; then
  {
    echo "Memory usage changed for nRF91M1 configuration. See [CI run](${CI_RUN_URL:-})"
    echo '```diff'
    printf ' '; head -n 1 "$MEMORY_REPORT_DIR/baseline.size"
    cat "$MEMORY_REPORT_DIR/size_change.diff"
    echo '```'
  } > "$MEMORY_REPORT_PATH"
else
  echo "Memory usage did not change for nRF91M1 configuration. See [CI run](${CI_RUN_URL:-})" \
    > "$MEMORY_REPORT_PATH"
fi
