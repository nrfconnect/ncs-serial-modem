# GitHub Copilot Instructions — ncs-serial-modem

Trust these instructions. Only search the codebase when the information here is incomplete or appears to be in error.

---

## General Agent Rules

- **Never overwrite local user changes without reading first.** Before modifying any file, always read its current contents. Do not assume the file matches what was previously seen in context — the user may have edited it between prompts.
- **Never install or upgrade pip packages without explicit user permission.** Ask before running `pip install`, `pip install --upgrade`, or any equivalent.
- **Never flash the application to a device without asking the user first.** Do not run `west flash` or any equivalent flashing command without explicit confirmation.

## What This Repository Does

**ncs-serial-modem** is a Zephyr RTOS application (written in C) that turns an nRF9151 SiP into a standalone serial modem controlled over UART via AT commands. A host device (PC or external MCU) sends AT commands over UART; the application forwards them to the LTE modem, returns responses and URCs, and exposes additional proprietary AT commands (sockets, GNSS, FOTA, SMS, MQTT, HTTP, nRF Cloud, PPP, CMUX, etc.).

**Repository type:** West manifest repo — `west.yml` at the repo root defines the full NCS workspace.
**Language:** C (Zephyr/NCS application)
**Target SoC:** nRF9151 (nRF91 Series, Cortex-M33, TF-M non-secure variant)
**SDK:** nRF Connect SDK (NCS) on top of Zephyr RTOS
**Build system:** West + CMake + Kconfig + Ninja
**Approx. source size:** ~40 C source files in `app/src/`, ~2 500 lines of documentation RST

---

## Environment Preconditions

The following must be satisfied before running any build or test command:

1. **Verify the west workspace is active** before running any `west` command:
   ```bash
   west status
   echo $ZEPHYR_BASE
   ```
   - `west status` must exit 0 and list workspace projects.
   - `ZEPHYR_BASE` must be set to the path of the `zephyr/` directory in the workspace.
   - If either check fails, **do not attempt to run `west init` or `west update`**. Instead, ask the user how to activate their Zephyr/NCS build environment (e.g., which virtualenv to source, which toolchain script to run).

   > **NEVER run `west init` or `west update`** — these commands modify the shared workspace and must only be run by the developer explicitly.

2. The Python virtualenv used for the NCS toolchain (typically `~/.virtualenvs/zephyr/` or `.venv/`) must be **activated** before running builds.

---

## Build

### Default application build

```bash
# From the repository root:
west build -b nrf9151dk/nrf9151/ns app
```

- **Always** use the `/ns` (non-secure) board target — TF-M is required. Omitting `/ns` causes a build failure.
- The board target `nrf9151dk/nrf9151/ns` is the primary and mandatory target.
- The secondary supported target is `thingy91x/nrf9151/ns`.
- Sysbuild is enabled by default (`app/sysbuild.conf`); the build produces MCUBoot + app images.
- With the command above run from the repository root, build output lands in `build/` unless `-d` is used.

### Clean (pristine) build

```bash
west build -b nrf9151dk/nrf9151/ns app --pristine
```

Use `--pristine` after Kconfig, devicetree overlay, or `CMakeLists.txt` changes.

### Build with feature overlays

Pass extra Kconfig files and DTC overlays via `--`:

```bash
west build -b nrf9151dk/nrf9151/ns app -- \
  -DEXTRA_CONF_FILE="overlay-cmux.conf;overlay-ppp.conf" \
  -DEXTRA_DTC_OVERLAY_FILE="overlay-external-mcu.overlay"
```

Available overlay files (in `app/`):
| File | Purpose |
|---|---|
| `overlay-cmux.conf` | Enable CMUX multiplexing |
| `overlay-ppp.conf` | Enable PPP networking |
| `overlay-carrier.conf` | LwM2M carrier library |
| `overlay-full-fota.conf` | Full MFW FOTA support |
| `overlay-memfault.conf` | Memfault integration |
| `overlay-nrf-device-provisioning.conf` | nRF Device Provisioning |
| `overlay-external-mcu.overlay` | External MCU UART wiring |
| `overlay-disable-dtr.overlay` | Disable DTR pin |
| `overlay-external-flash.overlay` | External flash |
| `overlay-uart1-hwfc.overlay` | UART1 hardware flow control |
| `overlay-nrf91m1.overlay` | nRF91M1 variant |
| `overlay-trace-backend-uart.conf` | Modem trace over UART |
| `overlay-trace-backend-cmux.conf` | Modem trace over CMUX |

---

## Running Tests

Tests use Zephyr's Twister framework and run on `native_sim` (no hardware needed).

```bash
# From the repository root — runs all unit tests:
west twister -T app/tests --platform native_sim -v
```

- All tests must pass before submitting a change.
- Test suites are located in `app/tests/at_commands/` and `app/tests/at_socket/`.
- Each test suite declares `platform_allow: native_sim` in its `testcase.yaml`.
- CI also runs build-only integration tests via `--integration` flag; those require the full hardware target and are skipped locally.

### Test output

Results are written to `twister-out/` (JSON + xUnit XML). Both test suites should report `PASSED`.

---

## Lint / Code Style

The project uses **clang-format** (config: `.clang-format` in the repo root, based on LLVM + Linux kernel style).

**Check formatting (dry-run, non-destructive):**
```bash
clang-format --style=file --dry-run --Werror app/src/sm_at_commands.c
```
Exit code 0 = clean; exit code 1 = violations found.

**Auto-fix formatting in place:**
```bash
clang-format --style=file -i app/src/*.c app/src/*.h
```

**Note:** Some existing files in `app/src/` have pre-existing clang-format violations (e.g., `sm_at_commands.c`). Only fix violations introduced by your own changes unless you are explicitly asked to fix style.

**Note:** Not all developers have `clang-format` set up locally. Ignore silently if the user does not have it installed or configured.

CI compliance checks (run on PRs via `.github/workflows/compliance.yml`) also validate:
- `LicenseAndCopyrightCheck` — every new `.c`/`.h` file must start with the SPDX header.
- `ClangFormat` — formatting must be compliant.
- `Kconfig` / `SysbuildKconfig` — Kconfig symbols must be valid.

**Required file header for all new C/H files:**
```c
/*
 * Copyright (c) <YEAR> Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */
```

---

## Documentation Build

Documentation is built in **two steps** from the `doc/` directory. The Zephyr/NCS Python virtualenv must be active.

### Step 1 — Doxygen (generates XML for API reference)

```bash
cd doc
doxygen
```

Output: `doc/_build_doxygen/xml/`
This step is required before Sphinx; Breathe reads the Doxygen XML.

### Step 2 — Sphinx (generates HTML)

```bash
cd doc
make html
# or equivalently:
sphinx-build -M html . _build_sphinx
```

Output: `doc/_build_sphinx/html/`

---

## Coding Style Reference

- **Linux Kernel Coding Style** (Zephyr variant)
- **Tab width:** 8 spaces per tab (use actual tab characters, not spaces)
- **Max line length:** 100 characters
- **Indentation:** tabs (not spaces)
- **Functions:** `snake_case`
- **Macros:** `UPPER_SNAKE_CASE`
- All new files require the copyright/SPDX header shown above.

---

## Project Layout

```
ncs-serial-modem/
├── west.yml                    # West manifest (defines full NCS workspace)
├── app/                        # Main Serial Modem application (the primary deliverable)
│   ├── CMakeLists.txt          # Build config; add new source files here
│   ├── Kconfig                 # Application Kconfig; add new CONFIG_SM_* symbols here
│   ├── prj.conf                # Default Kconfig settings
│   ├── sample.yaml             # Twister test matrix for the app build-only tests
│   ├── boards/                 # Board-specific DTC overlays
│   │   ├── nrf9151dk_nrf9151_ns.overlay
│   │   └── thingy91x_nrf9151_ns.overlay
│   ├── src/                    # Application C source (all files prefixed sm_*)
│   │   ├── main.c              # Entry point; init sequence
│   │   ├── sm_at_host.c/.h     # AT command engine: parsing, routing, responses, data mode
│   │   ├── sm_at_commands.c    # Proprietary AT command handlers (SM_AT_CMD_CUSTOM macros)
│   │   ├── sm_uart_handler.c/.h # Async UART driver integration
│   │   ├── sm_util.c/.h        # Modem AT forwarding helpers (sm_util_at_printf/scanf)
│   │   ├── sm_ctrl_pin.c/.h    # DTR/RI GPIO control
│   │   ├── sm_settings.c/.h    # NVS-backed settings
│   │   ├── sm_at_socket.c/.h   # #XSOCKET AT commands (TCP/UDP/TLS)
│   │   ├── sm_at_fota.c/.h     # FOTA AT commands
│   │   ├── sm_at_dfu.c/.h      # DFU AT commands
│   │   ├── sm_at_icmp.c        # ICMP/ping AT commands
│   │   ├── sm_at_gnss.c        # GNSS AT commands (CONFIG_SM_GNSS)
│   │   ├── sm_at_sms.c         # SMS AT commands (CONFIG_SM_SMS)
│   │   ├── sm_at_mqtt.c        # MQTT AT commands (CONFIG_SM_MQTTC)
│   │   ├── sm_at_httpc.c/.h    # HTTP client AT commands (CONFIG_SM_HTTPC)
│   │   ├── sm_at_nrfcloud.c/.h # nRF Cloud AT commands (CONFIG_SM_NRF_CLOUD)
│   │   ├── sm_at_provisioning.c# Device provisioning (CONFIG_NRF_PROVISIONING)
│   │   ├── sm_cmux.c/.h        # CMUX multiplexing (CONFIG_SM_CMUX)
│   │   ├── sm_ppp.c/.h         # PPP networking (CONFIG_SM_PPP)
│   │   ├── sm_log.c/.h         # Log control
│   │   ├── sm_defines.h        # Shared constants and enums
│   │   ├── sm_sockopt.h        # Socket option helpers
│   │   ├── sm_trap_macros.h    # Trap/assertion macros
│   │   └── sm_trace_backend_*.c # Modem trace backends
│   ├── tests/                  # Unit tests (run on native_sim via Twister)
│   │   ├── at_commands/        # Tests for sm_at_commands.c
│   │   └── at_socket/          # Tests for sm_at_socket.c
│   ├── overlay-*.conf          # Feature Kconfig overlays
│   ├── overlay-*.overlay       # Feature DTC overlays
│   └── sysbuild.conf           # Sysbuild Kconfig (MCUBoot, B0)
├── doc/                        # Sphinx + Doxygen documentation source
│   ├── Doxyfile                # Doxygen configuration
│   ├── conf.py                 # Sphinx configuration
│   ├── requirements.txt        # Python doc build dependencies
│   ├── app/                    # Application RST docs (AT commands, features, etc.)
│   ├── lib/                    # sm_at_client library docs
│   ├── samples/                # Sample app docs
│   ├── images/                 # SVG/PNG diagrams
│   ├── _doxygen/               # Doxygen main page source
│   ├── _build_doxygen/xml/     # Doxygen output (auto-generated, not committed)
│   └── _build_sphinx/html/     # Sphinx output (auto-generated, not committed)
├── lib/                        # Host-side AT client library
│   └── sm_at_client/           # sm_at_client library (for host MCU samples)
├── samples/                    # Host device sample applications
│   ├── sm_at_client_shell/     # Shell-based AT command interface (nRF54 host)
│   └── sm_ppp_shell/           # PPP shell sample (nRF54 host)
├── drivers/
│   └── dtr_uart/               # DTR-controlled UART driver
├── include/
│   └── sm_at_client.h          # Public API for sm_at_client library
├── .clang-format               # Clang-format style config (LLVM + Linux kernel)
├── .github/
│   ├── workflows/build.yml     # CI: Twister build+test (PR and nightly)
│   └── workflows/compliance.yml# CI: clang-format, license, Kconfig checks
└── sysbuild/                   # Sysbuild board-level configs
```

---

## Architecture: Key Components

### Initialization sequence (`app/src/main.c`)
1. Reset reason / modem fault handling
2. GPIO init (DTR/RI control pins via `sm_ctrl_pin`)
3. Settings init (`sm_settings`, backed by NVS)
4. `nrf_modem_lib_init()`
5. FOTA status check (MCUBoot swap type)
6. `sm_at_host_init()` — starts UART, AT parser, work queue
7. Auto-connect if `CONFIG_SM_AUTO_CONNECT=y`

On success, sends `Ready\r\n` over UART. On failure, sends `INIT ERROR\r\n`.

### AT Command Engine (`app/src/sm_at_host.c/.h`)
Central module. Key public API:
- `rsp_send(fmt, ...)` — send AT response to current pipe
- `rsp_send_ok()` / `rsp_send_error()` — send `\r\nOK\r\n` / `\r\nERROR\r\n`
- `urc_send(fmt, ...)` — queue a URC for async delivery
- `enter_datamode(handler, data_len)` — switch to raw data mode
- `sm_at_host_echo(bool)` — enable/disable ATE echo
- `in_datamode(X)` / `in_at_mode(X)` — mode query generics (accept ctx or pipe)

Work queue: `sm_work_q` (declared `extern` in `sm_util.h`, defined in `main.c`).

### Adding a New Proprietary AT Command

1. **Implement the handler** in `app/src/sm_at_commands.c` (or a new `sm_at_<feature>.c`):
   ```c
   SM_AT_CMD_CUSTOM(xmycmd, "AT#XMYCMD", handle_at_mycmd);
   STATIC int handle_at_mycmd(enum at_parser_cmd_type cmd_type,
                               struct at_parser *parser, uint32_t param_count)
   {
       if (cmd_type == AT_PARSER_CMD_TYPE_SET) {
           // parse params, do work, call rsp_send()
       }
       return 0; /* or negative errno on error */
   }
   ```
2. No registration step beyond the macro — `SM_AT_CMD_CUSTOM` registers via linker section.

### Adding a New Feature Module

1. Create `app/src/sm_at_<feature>.c` (and optionally `sm_at_<feature>.h`).
2. Add to `app/CMakeLists.txt`:
   ```cmake
   target_sources_ifdef(CONFIG_SM_<FEATURE> app PRIVATE src/sm_at_<feature>.c)
   ```
3. Add `CONFIG_SM_<FEATURE>` bool Kconfig option in `app/Kconfig` (inside the `menu "Nordic Serial Modem"` block).
4. Enable in `prj.conf` or a new `overlay-<feature>.conf`.

### Modem AT Command Forwarding

**Never** call `nrf_modem_at_printf()` or `nrf_modem_at_scanf()` directly from command handlers — this bypasses AT interception. Always use:
```c
int ret = sm_util_at_printf("AT+CFUN=1");
int n   = sm_util_at_scanf("AT+CFUN?", "+CFUN: %d", &cfun);
```

### Data Mode

- Entered via `enter_datamode(handler_cb, expected_len)`.
- Handler callback receives `DATAMODE_SEND` (data available) or `DATAMODE_EXIT` (exit requested).
- Exit triggered by `+++` (configurable) or by reaching `expected_len` bytes.
- Only one module may be in data mode at a time.

---

## Configuration System

| Symbol | Default | Purpose |
|---|---|---|
| `CONFIG_SM_AT_BUF_SIZE` | 4096 | AT command buffer (bytes) |
| `CONFIG_SM_UART_RX_BUF_SIZE` | 256 (2048 on Thingy:91 X) | UART RX single-buffer size |
| `CONFIG_SM_UART_TX_BUF_SIZE` | 256 | UART TX buffer size |
| `CONFIG_SM_URC_BUFFER_SIZE` | 4096 | URC queue buffer |
| `CONFIG_SM_DATAMODE_BUF_SIZE` | 4096 | Data mode buffer |
| `CONFIG_SM_SMS` | n | SMS support |
| `CONFIG_SM_GNSS` | n | GNSS support |
| `CONFIG_SM_NRF_CLOUD` | y | nRF Cloud support |
| `CONFIG_SM_MQTTC` | n | MQTT client |
| `CONFIG_SM_HTTPC` | n | HTTP client |
| `CONFIG_SM_PPP` | n | PPP networking |
| `CONFIG_SM_CMUX` | n | CMUX multiplexing |
| `CONFIG_SM_AUTO_CONNECT` | n | Auto network attach on boot |

---

## File Naming Conventions

- All application source files: `sm_*.c` / `sm_*.h` prefix.
- AT command handlers: `sm_at_<feature>.c`.
- Feature modules: same, conditionally compiled via `target_sources_ifdef`.

---

## Samples vs Application

- `app/` — the Serial Modem firmware that runs **on the nRF9151**.
- `samples/sm_at_client_shell/` and `samples/sm_ppp_shell/` — example firmware for a **separate host MCU** (e.g., nRF54 Series DK) that communicates with the Serial Modem over UART. Do not confuse these with the main application.
- `lib/sm_at_client/` — host-side C library used by the samples. Public header: `include/sm_at_client.h`.

---

## Validation Checklist After Making Changes

1. **Build:** `west build -b nrf9151dk/nrf9151/ns app` exits 0.
2. **Tests:** `west twister -T app/tests --platform native_sim -v` — all 2 tests PASSED.
3. **Lint:** `clang-format --style=file --dry-run --Werror <changed files>` exits 0.
4. **License header** present on any new `.c`/`.h` file.
5. **Docs** (if RST or headers changed): `cd doc && doxygen && make html` completes without new errors.

---

## Known Issues / Notes

- The `STATIC` macro (defined in `sm_at_host.h`) expands to `static` in production and to nothing when `CONFIG_UNITY` is set, to allow unit tests to access internal functions.
- The `SILENT_AT_COMMAND_RET` and `AT_COMMAND_CONTINUE_RET` sentinel values (defined in `sm_defines.h`) signal special handling to the AT host dispatcher — returning them from a handler suppresses automatic OK/ERROR sending.
- `urc_send()` is safe to call from any thread; it queues the URC and sends it from `sm_work_q`.
