---
name: code-review
description: Review process and checklist for pull requests and code changes in the ncs-serial-modem repository (a Zephyr/NCS embedded C application for the nRF9151). Use this when reviewing a pull request, branch diff, or set of code changes for correctness, security, maintainability, and operability.
---

You are reviewing changes to **ncs-serial-modem**, a Zephyr RTOS application (C) that turns an
nRF9151 SiP into a standalone AT-command serial modem. Focus on evidence from the diff: reference
concrete files, functions, and line numbers. Do not propose speculative changes without evidence,
and mark anything you cannot validate as "N/A" with a note on what's missing.

## Start here: existing repository conventions

Before applying the checklist below, make sure you are also applying the repository's existing
Copilot custom instructions, which describe build/test/lint commands, architecture, and
conventions in detail:

- `.github/copilot-instructions.md` — repository overview, build/test/validation checklist,
  architecture (AT command engine, data mode, modem AT forwarding), Kconfig/file-naming
  conventions, and the required SPDX/copyright header for new files.
- `.github/instructions/C-CODE.instructions.md` — applies to all `**/*.c`/`**/*.h` files: embedded
  C best practices (const/static usage, error handling, avoiding magic numbers, memory/stack
  discipline).
- `.github/instructions/DOC.instructions.md` — applies to `doc/**`: reStructuredText style,
  nRF documentation styleguide and templates.

Apply the repository's existing Copilot instructions in addition to this checklist. The two path-scoped instruction files below apply only when the reviewed paths match their `applyTo` patterns:

## Review checklist

### Functionality & correctness
- New/changed AT command handlers do what they claim; edge cases (missing/invalid parameters,
  boundary values) are handled and return appropriate error codes.
- Return values follow repo convention: negative errno on error, `0` on success, or the
  `SILENT_AT_COMMAND_RET` / `AT_COMMAND_CONTINUE_RET` sentinels where appropriate
  (`app/src/sm_defines.h`).
- No obvious logic errors, off-by-one errors, or unhandled branches.

### NCS/Zephyr conventions
- New `.c`/`.h` files start with the required SPDX header:
  `SPDX-License-Identifier: LicenseRef-Nordic-5-Clause` and a Nordic Semiconductor copyright line.
- New proprietary AT commands are registered via `SM_AT_CMD_CUSTOM` (see `sm_at_commands.c` or a
  dedicated `sm_at_<feature>.c`) rather than some ad hoc dispatch mechanism.
- New feature modules add a `target_sources_ifdef(CONFIG_SM_<FEATURE> ...)` line to
  `app/CMakeLists.txt` and a matching `CONFIG_SM_<FEATURE>` symbol in `app/Kconfig`, enabled via
  `prj.conf` or a feature `.conf`/`.overlay` file as appropriate.
- File names follow the `sm_*.c` / `sm_*.h` convention; functions are `snake_case`, macros are
  `UPPER_SNAKE_CASE`; indentation uses tabs (Linux kernel / Zephyr style), lines ≤ 100 columns.

### Modem AT forwarding & response handling
- Handlers never call `nrf_modem_at_printf()` / `nrf_modem_at_scanf()` directly — they use
  `sm_util_at_printf()` / `sm_util_at_scanf()` so AT interception still works.
- Responses use `rsp_send()` only for output that comfortably fits the 512-byte
  (`SM_AT_MAX_RSP_LEN`) static buffer; anything larger or non-`printf`-style uses `data_send()`
  with a pre-formatted buffer.
- URCs use `urc_send()` (safe from any thread) rather than writing to the UART pipe directly.

### Concurrency & data mode
- Work submitted from handlers goes through `sm_work_q` (or `sm_blocking_work_q` for
  nRF Cloud/other blocking operations) via the existing work-submission helpers, not ad hoc
  threads.
- Any new data-mode handler correctly handles both `DATAMODE_SEND` and `DATAMODE_EXIT`, and its
  exit path is idempotent (`exit_datamode_handler()` may legitimately be invoked twice for the
  same exit).
- Only one module is assumed to be in data mode at a time; new code doesn't violate that
  assumption.

### Security & robustness
- AT command parameters are validated (length, range, type) before use; no unchecked
  `at_parser` output is used to size buffers or index arrays.
- No hardcoded secrets, tokens, or credentials.
- Logging does not leak sensitive data (credentials, PII) and defaults to INFO level unless
  `CONFIG_SM_LOG_LEVEL_DBG` is explicitly required for the change.

### Testing
- Changes to AT-command modules are covered by the corresponding Twister unit tests under `app/tests/` (including `at_commands`, `at_socket`, `at_mqtt`, `at_sms`, `at_ppp`, and `at_nrfcloud`), runnable on `native_sim`.
- New tests are deterministic and cover both success and failure/edge cases.

### Documentation
- New or changed AT commands have matching updates under `doc/app/` (for example
  `at_socket.rst`, `at_httpc.rst`, `at_mqtt.rst`), following the existing definition-list style:
  parameter terms at column 0, descriptions indented three spaces.
- Non-obvious design decisions have a short rationale in a comment or in the docs.

## Output format

- Group comments under the checklist headings above where relevant.
- For each finding: 1–3 concise, evidence-based notes with file/function references.
- End with a short **Top Risks** list (highest-impact issues first) and, if useful,
  **Recommended Next Actions**.
