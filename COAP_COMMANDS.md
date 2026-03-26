# CoAP AT Commands for Serial Modem

This document describes the CoAP AT commands added to the Serial Modem application.

## Overview

The CoAP implementation provides AT commands to interact with CoAP servers over UDP or DTLS. It supports up to 3 concurrent CoAP contexts.

## AT Commands

### AT%COAPCREATE - Create CoAP Context

Creates a new CoAP context and returns its ID.

**Syntax:**
```
AT%COAPCREATE
```

**Response:**
```
%COAPCREATE: <id>
OK
```

**Parameters:**
- `<id>`: Context ID (0-2)

**Example:**
```
AT%COAPCREATE
%COAPCREATE: 0
OK
```

---

### AT%COAPSERVER - Set CoAP Server

Configures the CoAP server address for a context.

**Syntax:**
```
AT%COAPSERVER=<id>,"<host>",<port>[,<sec_tag>]
```

**Parameters:**
- `<id>`: Context ID (0-2)
- `<host>`: Server hostname or IP address (string)
- `<port>`: Server port number (integer)
- `<sec_tag>`: Optional security tag for DTLS (integer)

**Response:**
```
OK
```

**Examples:**
```
AT%COAPSERVER=0,"coap.example.com",5683
OK

AT%COAPSERVER=0,"coaps.example.com",5684,16842753
OK
```

---

### AT%COAPGET - Send CoAP GET Request

Sends a GET request to the configured server.

**Syntax:**
```
AT%COAPGET=<id>,"<path>"
```

**Parameters:**
- `<id>`: Context ID (0-2)
- `<path>`: Resource path (string)

**Response:**
```
OK
```

**Example:**
```
AT%COAPGET=0,"/sensor/temperature"
OK
```

The response will be received asynchronously via `%COAPRECV` URC.

---

### AT%COAPPOST - Send CoAP POST Request

Sends a POST request with optional payload.

**Syntax:**
```
AT%COAPPOST=<id>,"<path>"[,<hex_payload>]
```

**Parameters:**
- `<id>`: Context ID (0-2)
- `<path>`: Resource path (string)
- `<hex_payload>`: Optional payload as hex string (without quotes)

**Response:**
```
OK
```

**Examples:**
```
AT%COAPPOST=0,"/actuator/led"
OK

AT%COAPPOST=0,"/data/upload",48656C6C6F
OK
```

---

### AT%COAPPUT - Send CoAP PUT Request

Sends a PUT request with optional payload.

**Syntax:**
```
AT%COAPPUT=<id>,"<path>"[,<hex_payload>]
```

**Parameters:**
- `<id>`: Context ID (0-2)
- `<path>`: Resource path (string)
- `<hex_payload>`: Optional payload as hex string (without quotes)

**Response:**
```
OK
```

**Example:**
```
AT%COAPPUT=0,"/config/brightness",3735
OK
```

---

### AT%COAPDELETE - Delete CoAP Context

Deletes a CoAP context and frees its resources.

**Syntax:**
```
AT%COAPDELETE=<id>
```

**Parameters:**
- `<id>`: Context ID (0-2)

**Response:**
```
OK
```

**Example:**
```
AT%COAPDELETE=0
OK
```

---

## Unsolicited Result Codes (URCs)

### %COAPRECV - CoAP Response Received

Sent when a CoAP response is received from the server.

**Format:**
```
%COAPRECV: <id>,"<path>",<code>,<len>,<hex_payload>
```

**Parameters:**
- `<id>`: Context ID that received the response
- `<path>`: Resource path (currently empty in this implementation)
- `<code>`: CoAP response code (integer)
  - 65 (0x41): 2.01 Created
  - 66 (0x42): 2.02 Deleted
  - 67 (0x43): 2.03 Valid
  - 68 (0x44): 2.04 Changed
  - 69 (0x45): 2.05 Content
- `<len>`: Payload length in bytes
- `<hex_payload>`: Response payload as hex string

**Example:**
```
%COAPRECV: 0,"",69,5,48656C6C6F
```

---

## CoAP Response Codes

Common CoAP response codes:

| Code | Class.Detail | Description |
|------|--------------|-------------|
| 65   | 2.01         | Created |
| 66   | 2.02         | Deleted |
| 67   | 2.03         | Valid |
| 68   | 2.04         | Changed |
| 69   | 2.05         | Content |
| 128  | 4.00         | Bad Request |
| 132  | 4.04         | Not Found |
| 133  | 4.05         | Method Not Allowed |
| 160  | 5.00         | Internal Server Error |

---

## Usage Example

Complete workflow for sending CoAP requests:

```
# 1. Create CoAP context
AT%COAPCREATE
%COAPCREATE: 0
OK

# 2. Set server
AT%COAPSERVER=0,"coap.example.com",5683
OK

# 3. Send GET request
AT%COAPGET=0,"/temperature"
OK

# 4. Receive response (asynchronous URC)
%COAPRECV: 0,"",69,4,32332E35

# 5. Send POST with payload
AT%COAPPOST=0,"/data",7B2274656D70223A32332E357D
OK

%COAPRECV: 0,"",68,0,

# 6. Clean up
AT%COAPDELETE=0
OK
```

---

## Configuration Requirements

The following Kconfig options must be enabled in `prj.conf`:

```
CONFIG_COAP=y
CONFIG_COAP_CLIENT=y
CONFIG_NETWORKING=y
CONFIG_NET_SOCKETS=y
CONFIG_NET_SOCKETS_SOCKOPT_TLS=y  # For DTLS support
```

---

## Limitations

1. Maximum 3 concurrent CoAP contexts
2. Maximum payload size: 512 bytes
3. Maximum path length: 128 characters
4. Only confirmable (CON) requests are supported
5. No request retransmission tracking
6. Response URC does not include original request path

---

## Implementation Notes

- The implementation uses Zephyr's CoAP library
- Each context maintains its own UDP/DTLS socket
- A background thread monitors all active sockets for incoming responses
- Hex encoding is used for binary payloads to avoid AT command parsing issues
- Socket operations are non-blocking

---

## Comparison with Standard Modem AT%COAP* Commands

This Serial Modem implementation provides similar functionality to the standard nRF91 modem firmware AT%COAP* commands, but runs as an application-level implementation using Zephyr's CoAP library instead of the modem's built-in CoAP stack.

**Advantages:**
- More control over CoAP behavior
- Can be customized and extended
- Independent of modem firmware version

**Differences:**
- Context management is application-level (not modem-level)
- May have different performance characteristics
- Response format may differ slightly

---

## Troubleshooting

### Context creation fails
- Check that fewer than 3 contexts are already in use
- Delete unused contexts with AT%COAPDELETE

### Server connection fails
- Verify network connectivity (use AT+CEREG? and AT+CGPADDR)
- Confirm DNS resolution works for hostname
- Check firewall and network policies

### No response received
- Verify server is reachable and responding
- Check if server requires DTLS (use sec_tag parameter)
- Ensure request path is correct

### DTLS connection fails
- Verify security tag is provisioned with credentials
- Check that server certificate is valid
- Confirm cipher suites are compatible
