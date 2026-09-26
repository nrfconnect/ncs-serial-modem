---
applyTo: "**/*.c,**/*.h"
---
# Zephyr Log Style

These rules apply to all `LOG_DBG` / `LOG_INF` / `LOG_WRN` / `LOG_ERR` / `LOG_HEXDUMP_DBG` calls in `app/src/`.

## Log levels

| Level | Use when |
|---|---|
| `LOG_ERR` | Operation failed unrecoverably; include the returned error code (`err` / `-errno`) when one is available |
| `LOG_WRN` | Unexpected but recoverable; or a degraded fallback was taken |
| `LOG_INF` | Significant state changes visible to a field support engineer |
| `LOG_DBG` | Developer-only detail; removed or left off at default log level |

**Promote DBG → INF** only when all three are true: (1) the value is not already visible in AT traffic at `AT#XLOG=1`, (2) a support engineer diagnosing a field issue would need it, and (3) it fires at most once per session or significant operation.

**Remove DBG** when it duplicates an INF emitted later for the same event, or fires every poll cycle / data packet (high-frequency steady-state).

## Sensitive AT commands — INF/DBG split

AT commands are logged by `sm_log_rx_command()` / `sm_log_tx()`. Sensitive commands (those in `sm_log_cmd_sensitive_prefix()`) have their parameters redacted in the AT traffic log. Currently sensitive: `AT#XCOAPCREQ`, `AT#XHTTPCREQ`, `AT#XMQTTCON/PUB/SUB`, `AT+CGAUTH`, `AT%CMNG`, `AT#XSEND`, and others carrying credentials or user data.

For sensitive commands, log non-secret metadata at INF and potentially sensitive fields (path, payload, credentials) at DBG only:
```c
LOG_INF("CoAP request method=%d confirmable=%d payload_len=%d", ...);
LOG_DBG("CoAP path: %s", req->path);   // path may be sensitive → DBG only
```

## Value separators — use exactly one style per message, never mix

**`": %value"`** — single trailing value after an event description:
```c
"CoAP start failed: %d"
"Bootloader mode request save failed: %d"
```

**`key=value`** — named fields in a multi-value list, space between pairs:
```c
"CoAP response callback: result_code=%d, len=%d, last_block=%d"
"Unexpected data mode op=%u flags=0x%02x"
```

Do not mix unnamed `: %value` fields with `key=value` fields in the same message. A colon may separate an event description from a subsequent `key=value` list, as in the example above. When a qualifier (fd, handle) follows a noun-phrase event, use a comma:
```c
"AT#XCOAPCDATA timeout, handle %d"
"Cancelling CoAP request, handle %d"
```

## Socket / handle terminology

- **`sm_at_socket.c` (socket layer)**: `socket handle %d` for messages that print the fd.
- **Higher-level protocol files** (CoAP, HTTP, MQTT): bare `handle %d` — "socket" is implied. Both layers share `handle N`, so cross-layer grep works by the integer alone.
- **Do not** change "socket" in messages about socket properties that carry no fd (`"Max socket count reached"`, `"Socket family not supported"`, etc.).

## Format string style

- No trailing periods or ellipsis.
- No mid-sentence breaks (use comma + continuation in one string).
- No parenthesised IDs: `"PPP PDN %d activated"` not `"PPP PDN (%d) activated."`.
- Hex for bitmasks (`0x%x`), decimal for counts and status codes.
- Short field names: `"bytes"` not `"bytes_sent"`, `"len"` not `"payload_len"` in compact contexts.

## Compactness — remove filler words

- `"successfully"` / `"successful"` — always drop: `"initialized successfully"` → `"initialized"`.
- `"Please …"` — always drop.
- `"set to:"` after a description — drop: `"mode set to: %s"` → `"mode: %s"`.
- `"error:"` after `"failed"` — redundant: `"Failed to X, error: %d"` → `"Failed to X: %d"`.
- Prefer noun phrases: `"Timeout while waiting for registration"` → `"Registration timeout"`.

## Anti-patterns

**Missing error code in ERR** — always log `err` / `-errno`, not the input parameter that triggered the failure:
```c
// Wrong — logs what was attempted, not what failed
LOG_ERR("Failed to set bootloader mode to: %s", enable ? "enabled" : "disabled");
// Right
LOG_ERR("Bootloader mode request save failed: %d", err);
```

**Function name as message**: `"sm_settings_save: %d"` → `"Settings save failed: %d"`.

**`WARNING!` prefix** — do not use except at a genuine point of no return where interruption causes permanent hardware damage or data loss. The `WRN` level alone is sufficient in all other cases.

## Flash-saving `%s` pattern

When multiple call sites share the same format string differing only in one embedded noun, using `%s` with a string literal keeps one copy of the format string in flash:
```c
// One format string, three call sites — intentional optimisation
LOG_ERR("Failed to write %s: %d", "delta modem firmware", err);
LOG_ERR("Failed to write %s: %d", "bootloader segment", err);
```
Do **not** inline the literal when the format string is already shared. Suggest this pattern proactively when reviewing a file where two or more calls have near-identical strings differing only in an embedded noun.

**Minimum savings threshold**: only apply when the *shared* part of the format string is at least 3 words. Do not apply when the strings differ only in a 1–2 word suffix or prefix — the `%s` indirection adds complexity for negligible gain:
```c
// Wrong — shared part is only "failed: %d" (1 word); not worth it
LOG_ERR("%s failed: %d", "Modem shutdown", err);
LOG_ERR("%s failed: %d", "FMFU fdev load", err);

// Right — keep unique format strings
LOG_ERR("Modem shutdown failed: %d", err);
LOG_ERR("FMFU fdev load failed: %d", err);
```
