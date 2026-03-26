# Wi-Fi Scanning AT Command - Quick Reference

## Overview
The `AT#XWIFISCAN` command enables Wi-Fi access point scanning on Thingy:91 X.

## Files Changed/Created
1. **Created**: `app/src/sm_at_wifi.c` - Wi-Fi AT command implementation
2. **Updated**: `app/Kconfig` - Added CONFIG_SM_WIFI option
3. **Updated**: `app/CMakeLists.txt` - Added sm_at_wifi.c to build
4. **Updated**: `app/prj.conf` - Enabled Wi-Fi support
5. **Updated**: `app/boards/thingy91x_nrf9151_ns.overlay` - Added Wi-Fi device tree
6. **Created**: `doc/app/at_wifi.rst` - Documentation
7. **Updated**: `doc/app/at_commands.rst` - Added to doc index

## Usage Examples

### Basic Scan
```
AT#XWIFISCAN

#XWIFISCAN: 3
#XWIFISCAN: 1,"aa:bb:cc:dd:ee:ff",-65
#XWIFISCAN: 2,"11:22:33:44:55:66",-72
#XWIFISCAN: 3,"99:88:77:66:55:44",-80
OK
```

### Scan with Custom Timeout
```
AT#XWIFISCAN=15

#XWIFISCAN: 2
#XWIFISCAN: 1,"aa:bb:cc:dd:ee:ff",-65
#XWIFISCAN: 2,"11:22:33:44:55:66",-72
OK
```

### Use with nRF Cloud Location
```
# Step 1: Scan for Wi-Fi APs
AT#XWIFISCAN

#XWIFISCAN: 2
#XWIFISCAN: 1,"aa:bb:cc:dd:ee:ff",-65
#XWIFISCAN: 2,"11:22:33:44:55:66",-72
OK

# Step 2: Request location using Wi-Fi data
AT#XNRFCLOUDPOS=0,1,"aa:bb:cc:dd:ee:ff",-65,"11:22:33:44:55:66",-72
OK

#XNRFCLOUDPOS: 2,35.455833,139.626111,50
```

## Build Instructions

### For Thingy:91 X
The Wi-Fi support is automatically enabled for Thingy:91 X. Just build normally:

```bash
cd ncs-serial-modem/app
west build -b thingy91x/nrf9151/ns
```

### Configuration
Wi-Fi is enabled by default in `prj.conf`:
- `CONFIG_WIFI=y`
- `CONFIG_WIFI_NRF70=y`
- `CONFIG_NET_L2_WIFI_MGMT=y`

## Features
- **Power Efficient**: Wi-Fi interface is only powered up during scanning
- **Up to 20 APs**: Can detect maximum 20 access points per scan
- **Configurable Timeout**: 1-60 seconds (default: 10)
- **MAC & RSSI**: Returns both MAC address and signal strength
- **nRF Cloud Compatible**: Output format works directly with AT#XNRFCLOUDPOS

## Error Codes
- `-EBUSY`: Scan already in progress
- `-ENODEV`: Wi-Fi device not found/ready
- `-EFAULT`: Failed to start scan
- `-ETIMEDOUT`: Scan timeout expired
- `-EINVAL`: Invalid parameter

## Notes
- Requires nRF7002 Wi-Fi companion chip (standard on Thingy:91 X)
- Wi-Fi interface automatically powered down after scan to save battery
- Typical scan duration: 2-5 seconds depending on environment
