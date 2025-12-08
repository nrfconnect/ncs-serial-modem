# AGENTS.md - Serial Modem Project Guide

## Project Overview

This is a **Zephyr RTOS application** written in **C** that transforms an nRF91 Series device (nRF9151) into a standalone serial modem controlled via AT commands. The application runs on the nRF91 device and provides a UART interface for host devices (PCs or external MCUs) to control the LTE modem functionality.

### Key Concepts

- **Serial Modem (SM)**: The main application that runs on nRF91 devices
- **AT Commands**: Standard and proprietary AT commands for modem control
- **Host Device**: External device (PC or MCU) that communicates with SM via UART
- **Data Mode**: Raw data transmission mode (vs AT command mode)
- **CMUX**: Multiplexing protocol for multiple channels over single UART
- **PPP**: Point-to-Point Protocol for IP networking

## Repository Structure

```
ncs-serial-modem/
├── app/                    # Main Serial Modem application
│   ├── src/               # Application source code
│   ├── boards/            # Board-specific overlays/configs
│   ├── CMakeLists.txt     # Build configuration
│   ├── prj.conf          # Default Kconfig settings
│   └── overlay-*.conf    # Feature-specific overlays
├── doc/                   # Documentation in RST format
│   ├── app/              # Application documentation
│   ├── samples/          # Sample documentation
│   └── index.rst        # Documentation index
├── samples/              # Host device samples
│   ├── sm_at_client_shell/  # AT command shell sample
│   └── sm_ppp_shell/        # PPP shell sample
├── lib/                  # Libraries for host devices
│   └── sm_at_client/    # AT client library
├── drivers/              # Zephyr drivers
│   └── dtr_uart/        # DTR-controlled UART driver
└── sysbuild/            # Sysbuild configuration for bootloader
```

## Build System

### Build Command
```bash
west build -b nrf9151dk/nrf9151/ns app
```

### Key Build Details
- **Board target**: `nrf9151dk/nrf9151/ns` (non-secure variant)
- **Build system**: Zephyr CMake + Kconfig
- **Toolchain**: nRF Connect SDK (NCS) toolchain
- **Bootloader**: MCUBoot (via sysbuild)

### Configuration Files
- `app/prj.conf`: Base configuration
- `app/overlay-*.conf`: Feature overlays (CMUX, PPP, Carrier, etc.)
- `app/boards/*.overlay`: Board-specific devicetree overlays

### Building Documentation

The documentation is built using two commands that must be run in the `doc/` directory:

1. **Doxygen**: Generates XML output from C header files for API documentation
   ```bash
   cd doc
   doxygen
   ```
   This generates XML files in `doc/_build_doxygen/xml/` that are used by Sphinx/Breathe.

2. **Sphinx**: Generates HTML documentation from RST source files
   ```bash
   cd doc
   make html
   ```
   Or directly:
   ```bash
   cd doc
   sphinx-build -M html . _build_sphinx
   ```

**Prerequisites**:
- Python virtual environment with required packages (see `doc/requirements.txt`)
- Doxygen installed on the system
- Sphinx and related extensions (breathe, sphinx-ncs-theme, etc.)

**Output**: HTML documentation is generated in `doc/_build_sphinx/html/`

**Note**: The virtual environment used should be the same as for building the application (typically the Zephyr/NCS virtual environment).

## Coding Style

**CRITICAL**: Follow these rules strictly:

1. **Linux Kernel Coding Style**: Based on Linux kernel style guide
2. **Tab Width**: 8 spaces per tab
3. **Max Line Length**: 100 characters
4. **Indentation**: Use tabs (not spaces)
5. **Function Naming**: `snake_case` for functions
6. **Macro Naming**: `UPPER_SNAKE_CASE` for macros
7. **File Headers**: Must include copyright and SPDX license identifier

### Example Code Style
```c
/*
 * Copyright (c) 2025 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/kernel.h>

static int my_function(void)
{
	int ret;

	ret = do_something();
	if (ret) {
		return ret;
	}

	return 0;
}
```

## Application Architecture

### Main Entry Point
- **File**: `app/src/main.c`
- **Function**: `main()`
- **Initialization Flow**:
  1. Reset reason handling
  2. GPIO initialization (control pins)
  3. Settings initialization (NVS)
  4. Modem library initialization (`nrf_modem_lib_init()`)
  5. FOTA status check (MCUBoot swap type)
  6. AT host initialization (`sm_at_host_init()`)
  7. Auto-connect (if enabled)

### Core Components

#### 1. AT Host (`sm_at_host.c/h`)
- **Purpose**: Main AT command processing engine
- **Key Features**:
  - AT command parsing and routing
  - Response generation (`rsp_send()`, `rsp_send_ok()`, `rsp_send_error()`)
  - URC (Unsolicited Result Code) queuing (`urc_send()`)
  - Data mode management (`enter_datamode()`, `exit_datamode()`)
  - Echo control
- **Operation Modes**:
  - `SM_AT_COMMAND_MODE`: Normal AT command processing
  - `SM_DATA_MODE`: Raw data transmission
  - `SM_NULL_MODE`: Discard incoming data

#### 2. AT Commands (`sm_at_commands.c`)
- **Purpose**: Implementation of proprietary AT commands
- **Registration**: Uses `SM_AT_CMD_CUSTOM()` macro
- **Example**:
  ```c
  SM_AT_CMD_CUSTOM(xsmver, "AT#XSMVER", handle_at_smver);
  static int handle_at_smver(enum at_parser_cmd_type cmd_type,
                             struct at_parser *parser,
                             uint32_t param_count)
  {
      // Command implementation
  }
  ```

#### 3. UART Handler (`sm_uart_handler.c/h`)
- **Purpose**: Low-level UART communication
- **Features**:
  - Async UART API
  - Buffer management (RX/TX)
  - Modem pipe integration (for CMUX/PPP)
  - Hardware flow control support

#### 4. Socket Management (`sm_at_socket.c`)
- **Purpose**: Socket-based AT commands (#XSOCKET, etc.)
- **Features**:
  - TCP/UDP socket creation
  - TLS/DTLS support
  - Socket data mode handling

#### 5. Feature Modules (Conditional Compilation)
- **SMS** (`sm_at_sms.c`): `CONFIG_SM_SMS`
- **GNSS** (`sm_at_gnss.c`): `CONFIG_SM_GNSS`
- **nRF Cloud** (`sm_at_nrfcloud.c`): `CONFIG_SM_NRF_CLOUD`
- **MQTT** (`sm_at_mqtt.c`): `CONFIG_SM_MQTTC`
- **FOTA** (`sm_at_fota.c`): Application and modem firmware updates
- **CMUX** (`sm_cmux.c`): `CONFIG_SM_CMUX` - Multiplexing protocol
- **PPP** (`sm_ppp.c`): `CONFIG_SM_PPP` - Point-to-Point Protocol
- **Carrier** (`lwm2m_carrier/`): `CONFIG_SM_CARRIER` - LwM2M carrier library

### AT Command Flow

1. **Reception**: UART RX interrupt → `sm_uart_handler.c`
2. **Buffering**: Data stored in ring buffers
3. **Parsing**: `sm_at_receive()` processes incoming bytes
4. **Routing**:
   - Proprietary commands → `SM_AT_CMD_CUSTOM` handlers
   - Standard modem commands → Forwarded to modem via `sm_util_at_printf()`
5. **Response**: Generated via `rsp_send()` functions
6. **Transmission**: UART TX via `sm_tx_write()`

### Data Mode

Data mode allows raw data transmission without AT command parsing:
- **Entry**: `enter_datamode(handler, data_len)`
- **Exit**: Pattern-based (`+++` by default) or length-based
- **Handler**: Module-specific callback for data processing
- **Use Cases**: Socket data, FOTA downloads, etc.

### URC (Unsolicited Result Codes)

URCs are asynchronous notifications from the modem:
- **Queuing**: `urc_send()` queues messages
- **Context**: `sm_urc_ctx` manages ownership (AT vs CMUX)
- **Delay**: URCs delayed during incomplete AT command echo

## Key Data Structures

### AT Parser
```c
struct at_parser {
    // Used by AT command handlers to parse parameters
};
```

### Work Queue
```c
extern struct k_work_q sm_work_q;  // Serial Modem work queue
```

### Buffers
```c
extern uint8_t sm_at_buf[CONFIG_SM_AT_BUF_SIZE + 1];  // AT command buffer
extern uint8_t sm_data_buf[SM_MAX_MESSAGE_SIZE];      // Socket data buffer
```

## Configuration System

### Kconfig Options (`app/Kconfig`)
- `CONFIG_SM_AT_BUF_SIZE`: AT command buffer size (default: 4096)
- `CONFIG_SM_UART_RX_BUF_SIZE`: UART RX buffer size (default: 256)
- `CONFIG_SM_UART_TX_BUF_SIZE`: UART TX buffer size (default: 256)
- `CONFIG_SM_URC_BUFFER_SIZE`: URC buffer size (default: 4096)
- `CONFIG_SM_DATAMODE_BUF_SIZE`: Data mode buffer (default: 4096)

### Feature Flags
- `CONFIG_SM_SMS`: Enable SMS support
- `CONFIG_SM_GNSS`: Enable GNSS support
- `CONFIG_SM_NRF_CLOUD`: Enable nRF Cloud support
- `CONFIG_SM_MQTTC`: Enable MQTT client
- `CONFIG_SM_PPP`: Enable PPP support
- `CONFIG_SM_CMUX`: Enable CMUX support
- `CONFIG_SM_AUTO_CONNECT`: Auto-connect to network on startup

## Samples (Host Device)

### 1. sm_at_client_shell
- **Purpose**: Shell-based AT command interface for host MCU
- **Location**: `samples/sm_at_client_shell/`
- **Library**: Uses `lib/sm_at_client/`
- **Use Case**: nRF54 Series DK controlling nRF91 Series DK

### 2. sm_ppp_shell
- **Purpose**: PPP-based networking shell
- **Location**: `samples/sm_ppp_shell/`
- **Use Case**: IP networking over PPP

### AT Client Library (`lib/sm_at_client/`)
- **Purpose**: Helper library for host devices
- **Features**:
  - UART communication
  - AT command sending/receiving
  - Response parsing
  - URC monitoring (`SM_MONITOR()` macro)
  - DTR/RI pin handling

## Drivers

### DTR UART (`drivers/dtr_uart/`)
- **Purpose**: UART that can be power-managed via DTR pin
- **Use Case**: External MCU communication with power control

## Documentation

- **Format**: RST (reStructuredText) with Doxygen for API docs
- **Location**: `doc/` directory
- **Build System**: Sphinx-based documentation with Breathe extension for Doxygen integration
- **Build Commands**: See "Building Documentation" section above
- **Key Documents**:
  - `doc/app/sm_description.rst`: Application overview
  - `doc/app/AT_commands.rst`: AT command reference
  - `doc/samples/README.rst`: Sample documentation
- **Structure**:
  - `doc/app/`: Application documentation (AT commands, features, etc.)
  - `doc/samples/`: Sample application documentation
  - `doc/lib/`: Library documentation (sm_at_client)
  - `doc/drivers/`: Driver documentation
  - `doc/_doxygen/`: Doxygen main page and configuration
  - `doc/Doxyfile`: Doxygen configuration file
  - `doc/conf.py`: Sphinx configuration file

## Testing

### Initialization Sequence
1. Device boots → sends `Ready\r\n` on UART
2. On error → sends `INIT ERROR\r\n`
3. Host can then send AT commands

### Logging
- **Default**: SEGGER RTT (via J-Link)
- **Alternative**: UART0 console (via overlay)
- **Module**: `LOG_MODULE_REGISTER(sm, CONFIG_SM_LOG_LEVEL)`

## Common Patterns

### Adding a New AT Command
1. Create handler function:
   ```c
   static int handle_at_mycmd(enum at_parser_cmd_type cmd_type,
                              struct at_parser *parser,
                              uint32_t param_count)
   {
       // Parse parameters using at_parser API
       // Generate response using rsp_send()
       return 0;
   }
   ```
2. Register command:
   ```c
   SM_AT_CMD_CUSTOM(mycmd, "AT#XMYCMD", handle_at_mycmd);
   ```

### Modem AT Command Forwarding
```c
// Use utility functions (bypasses interception)
int ret = sm_util_at_printf("AT+CFUN=1");
ret = sm_util_at_scanf("AT+CFUN?", "+CFUN: %d", &cfun);
```

### Error Handling
- Always check return values
- Use negative errno codes for errors
- Log errors with appropriate level (`LOG_ERR`, `LOG_WRN`, `LOG_INF`)

## Dependencies

### NCS Libraries
- `nrf_modem_lib`: Modem library integration
- `at_parser`: AT command parsing
- `at_monitor`: AT response monitoring
- `lte_lc`: LTE link control
- `modem_jwt`: JWT generation
- `fota_download`: Firmware over-the-air updates
- `nrf_cloud`: nRF Cloud services

### Zephyr Subsystems
- Networking (IPv4/IPv6)
- Sockets API
- Settings (NVS)
- GPIO
- UART (async API)
- Work queues
- Ring buffers

## Important Notes

1. **Non-Secure Variant**: Always use `*/ns` board variant for TF-M support
2. **Modem Library**: Must be initialized before AT commands
3. **Thread Safety**: Use mutexes (`mutex_mode`, `mutex_data`) for shared state
4. **Work Queue**: Use `sm_work_q` for deferred work
5. **Buffer Sizes**: Be mindful of buffer limits (AT: 4096, Data: 1024)
6. **URC Ownership**: CMUX and AT modes share URC context (use acquire/release)

## Development Workflow

1. **Modify Code**: Edit files in `app/src/`
2. **Configure**: Adjust `prj.conf` or add overlays
3. **Build**: `west build -b nrf9151dk/nrf9151/ns app`
4. **Flash**: `west flash`
5. **Test**: Connect serial terminal, send AT commands
6. **Debug**: Use RTT viewer or UART console for logs

## File Naming Conventions

- **Source files**: `sm_*.c` (Serial Modem prefix)
- **Header files**: `sm_*.h`
- **AT command handlers**: `sm_at_*.c`
- **Feature modules**: `sm_at_<feature>.c`

## Memory Management

- **Static Allocation**: Preferred (no dynamic allocation in critical paths)
- **Ring Buffers**: Used for UART RX/TX
- **Work Queues**: For async operations
- **Slabs**: Used in AT client library for RX buffers

## Power Management

- **DTR Pin**: Controls UART power state
- **RI Pin**: Ring indicator (notification to host)
- **Sleep Modes**: Deep sleep and idle sleep supported
- **Modem Power**: Controlled via `AT+CFUN` commands
