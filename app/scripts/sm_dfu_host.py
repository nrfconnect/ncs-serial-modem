#!/usr/bin/env python3
#
# Copyright (c) 2025, Nordic Semiconductor ASA
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause

import io
import sys
import time
import argparse
from pathlib import Path

import serial
import cbor2

LINE_END = "\r"

ser = None


def init_serial(port: str, baudrate: int):
    """Initialize the global serial device."""
    global ser
    ser = serial.Serial(port, baudrate, rtscts=True)
    time.sleep(0.1)
    ser.reset_input_buffer()


def close_serial():
    """Close the global serial device."""
    global ser
    if ser:
        ser.close()
        ser = None


def send_command(command: str, *params):
    """Send an AT command with optional parameters."""
    if params:
        cmd_str = f"{command}={','.join(str(p) for p in params)}{LINE_END}"
    else:
        cmd_str = f"{command}{LINE_END}"

    ser.write(cmd_str.encode())


def wait_for_response(expected: str, timeout: float) -> bool:
    """Wait for expected string in serial output. Returns False on ERROR or timeout."""
    end_time = time.time() + timeout
    buffer = ""

    while time.time() < end_time:
        if ser.in_waiting:
            data = ser.read(ser.in_waiting)
            buffer += data.decode(errors="ignore")
            if expected in buffer:
                return True
            if "ERROR" in buffer:
                return False
        time.sleep(0.01)

    return False


def wait_for_device(max_attempts: int = 10,
                    timeout: float = 3.0,
                    max_backoff: float = 10.0) -> bool:
    """Ping device with AT until OK, using exponential backoff."""
    backoff = 0.5
    for attempt in range(1, max_attempts + 1):
        print(f"Pinging device (attempt {attempt}/{max_attempts})...")
        send_command("AT")
        if wait_for_response("OK", timeout):
            print("Device ready")
            return True
        if attempt < max_attempts:
            time.sleep(backoff)
            backoff = min(backoff * 2, max_backoff)
    return False


def wait_for_urc(timeout: float) -> tuple[int | None, str]:
    """Wait for #XDFU URC, return (status_code, urc_line)."""
    end_time = time.time() + timeout
    buffer = ""

    while time.time() < end_time:
        if ser.in_waiting:
            data = ser.read(ser.in_waiting)
            buffer += data.decode(errors="ignore")

            # Look for #XDFU line
            if "#XDFU:" in buffer:
                for line in buffer.split("\n"):
                    if "#XDFU:" in line:
                        urc_line = line.strip()
                        # Parse "#XDFU:<type>,<operation>,<status>"
                        try:
                            parts = line.split(":")[1].strip().split(",")
                            if len(parts) >= 3:
                                return int(parts[2]), urc_line
                        except (ValueError, IndexError):
                            pass
        time.sleep(0.01)

    return None, ""


def send_data(data: bytes):
    """Send raw bytes."""
    ser.write(data)
    ser.flush()


_prev_line_len = 0

def print_progress(label: str, current: int, total: int, bytes_sent: int, total_bytes: int,
                   start_time: float, cmd: str = ""):
    """Print single-line progress indicator."""
    global _prev_line_len
    percent = (current / total) * 100 if total else 0
    elapsed = max(time.time() - start_time, 0.001)
    speed = bytes_sent / elapsed / 1024

    line = (
        f"{label} [{current:4d}/{total:<4d}] "
        f"{percent:5.1f}%  {bytes_sent/1024:7.1f} KB / {total_bytes/1024:7.1f} KB  {speed:5.0f} KB/s"
    )
    if cmd:
        line += f"  {cmd}"
    padding = max(0, _prev_line_len - len(line))
    sys.stdout.write("\r" + line + " " * padding)
    sys.stdout.flush()
    _prev_line_len = len(line)


def parse_chunks_bin(filepath: str, chunk_size: int = 4096) -> list[tuple[int, bytes]]:
    """Return list of (offset, data) tuples from a .bin file."""
    chunks = []
    offset = 0
    with open(filepath, "rb") as f:
        while chunk := f.read(chunk_size):
            chunks.append((offset, chunk))
            offset += len(chunk)
    return chunks


def parse_chunks_cbor(filepath: str, chunk_size: int = 4096) -> tuple[list, list]:
    """Parse .cbor file, return (boot_chunks, fw_chunks) as (addr, data) tuples."""
    with open(filepath, "rb") as f:
        raw = f.read()

    # Parse signed CBOR wrapper (contains manifest + Nordic's signature)
    wrapper = cbor2.loads(raw)
    if hasattr(wrapper, "value"):
        wrapper = wrapper.value
    payload = wrapper[2]

    # Parse manifest
    manifest = cbor2.loads(payload)
    segments_cbor = manifest[3]
    segments_flat = cbor2.loads(segments_cbor)

    # Find where segment data starts in file
    stream = io.BytesIO(raw)
    cbor2.CBORDecoder(stream).decode()
    blob_offset = stream.tell()

    # Build segment list
    segments = []
    data_offset = blob_offset
    for i in range(0, len(segments_flat), 2):
        addr = segments_flat[i]
        length = segments_flat[i + 1]
        is_boot = i == 0
        segments.append({
            "addr": addr,
            "length": length,
            "offset": data_offset,
            "is_boot": is_boot,
        })
        data_offset += length

    # Split each segment into chunks
    boot_chunks = []
    fw_chunks = []

    for seg in segments:
        seg_data = raw[seg["offset"]:seg["offset"] + seg["length"]]

        for i in range(0, seg["length"], chunk_size):
            chunk_addr = seg["addr"] + i
            chunk_data = seg_data[i:i + chunk_size]

            if seg["is_boot"]:
                boot_chunks.append((chunk_addr, chunk_data))
            else:
                fw_chunks.append((chunk_addr, chunk_data))

    return boot_chunks, fw_chunks


# DFU types
DFU_TYPE_APP = 0
DFU_TYPE_DELTA = 1
DFU_TYPE_FULL = 2


def dfu_init(dfu_type: int, size: int = None) -> bool:
    """Initialize DFU. Size required for APP/DELTA. FULL reboots device."""
    if dfu_type == DFU_TYPE_APP:
        if size is None:
            return False
        print(f"AT#XDFUINIT={dfu_type},{size}")
        send_command("AT#XDFUINIT", dfu_type, size)
        expected, timeout = "OK", 5.0
    elif dfu_type == DFU_TYPE_DELTA:
        if size is None:
            return False
        print(f"AT#XDFUINIT={dfu_type},{size}")
        send_command("AT#XDFUINIT", dfu_type, size)
        expected, timeout = "OK", 120.0  # Flash erase can take minutes
    elif dfu_type == DFU_TYPE_FULL:
        print(f"AT#XDFUINIT={dfu_type}")
        send_command("AT#XDFUINIT", dfu_type)
        expected, timeout = "Bootloader mode ready", 30.0  # Device reboots
    else:
        return False

    # Wait for expected response or ERROR
    end_time = time.time() + timeout
    buffer = ""
    while time.time() < end_time:
        if ser.in_waiting:
            data = ser.read(ser.in_waiting)
            buffer += data.decode(errors="ignore")
            if expected in buffer:
                return True
            if "ERROR" in buffer:
                return False
        time.sleep(0.01)
    return False


def dfu_write(dfu_type: int, addr: int, data: bytes) -> tuple[bool, str]:
    """Write firmware chunk. Returns (success, urc_line)."""
    send_command("AT#XDFUWRITE", dfu_type, addr, len(data))
    if not wait_for_response("OK", 2.0):
        return False, ""

    send_data(data)

    status, urc_line = wait_for_urc(10.0)
    return status == 0, urc_line


def dfu_apply(dfu_type: int) -> bool:
    """Apply firmware update. Waits for URC status."""
    print(f"AT#XDFUAPPLY={dfu_type}")
    send_command("AT#XDFUAPPLY", dfu_type)

    status, urc_line = wait_for_urc(10.0)
    if urc_line:
        print(urc_line)
    return status == 0


def do_dfu_app(filepath: str, retries: int = 3) -> bool:
    """Perform application DFU."""
    chunks = parse_chunks_bin(filepath)
    total_size = sum(len(data) for _, data in chunks)
    total_chunks = len(chunks)

    print(f"Application DFU: {total_size:,} bytes in {total_chunks} chunks")

    # Initialize
    if not dfu_init(DFU_TYPE_APP, total_size):
        print("\nERROR: Init failed")
        return False

    # Write chunks
    bytes_sent = 0
    start_time = time.time()

    for i, (addr, data) in enumerate(chunks, 1):
        success = False
        urc = ""
        for _ in range(retries):
            success, urc = dfu_write(DFU_TYPE_APP, addr, data)
            if success:
                break
        if not success:
            print(f"\nERROR: Chunk {i} failed after {retries} retries")
            return False

        bytes_sent += len(data)
        cmd = f"AT#XDFUWRITE={DFU_TYPE_APP},{addr},{len(data)} -> {urc}"
        print_progress("Application", i, total_chunks, bytes_sent, total_size, start_time, cmd)

    print()

    # Apply update
    if not dfu_apply(DFU_TYPE_APP):
        print("ERROR: Apply failed")
        return False

    print("OK: Application DFU complete. Reboot to activate.")
    return True


def do_dfu_delta(filepath: str, retries: int = 3) -> bool:
    """Perform delta modem DFU."""
    chunks = parse_chunks_bin(filepath)
    total_size = sum(len(data) for _, data in chunks)
    total_chunks = len(chunks)

    print(f"Modem delta DFU: {total_size:,} bytes in {total_chunks} chunks")

    # Initialize
    if not dfu_init(DFU_TYPE_DELTA, total_size):
        print("\nERROR: Init failed")
        return False

    # Write chunks
    bytes_sent = 0
    start_time = time.time()

    for i, (addr, data) in enumerate(chunks, 1):
        success = False
        urc = ""
        for _ in range(retries):
            success, urc = dfu_write(DFU_TYPE_DELTA, addr, data)
            if success:
                break
        if not success:
            print(f"\nERROR: Chunk {i} failed after {retries} retries")
            return False

        bytes_sent += len(data)
        cmd = f"AT#XDFUWRITE={DFU_TYPE_DELTA},{addr},{len(data)} -> {urc}"
        print_progress("Modem delta", i, total_chunks, bytes_sent, total_size, start_time, cmd)

    print()

    # Apply update
    if not dfu_apply(DFU_TYPE_DELTA):
        print("ERROR: Apply failed")
        return False

    print("OK: Modem delta DFU complete. Reset modem to activate.")
    return True


def do_dfu_full(filepath: str, retries: int = 3) -> bool:
    """Perform full modem DFU."""
    boot_chunks, fw_chunks = parse_chunks_cbor(filepath)
    boot_size = sum(len(data) for _, data in boot_chunks)
    fw_size = sum(len(data) for _, data in fw_chunks)

    print(f"Modem full DFU: Boot={boot_size:,} bytes, FW={fw_size:,} bytes")

    # Initialize
    print(f"AT#XDFUINIT={DFU_TYPE_FULL}")
    send_command("AT#XDFUINIT", DFU_TYPE_FULL)

    if not wait_for_response("Bootloader mode ready", 30.0):
        print("ERROR: Device did not enter bootloader mode")
        return False
    print("OK: Device in bootloader mode")

    # Phase 1: Write bootloader chunks
    print("Phase 1: Bootloader segment")
    bytes_sent = 0
    start_time = time.time()

    for i, (addr, data) in enumerate(boot_chunks, 1):
        success, urc = dfu_write(DFU_TYPE_FULL, addr, data)
        if not success:
            print(f"\nERROR: Boot chunk {i} failed")
            return False

        bytes_sent += len(data)
        cmd = f"AT#XDFUWRITE={DFU_TYPE_FULL},{addr},{len(data)} -> {urc}"
        print_progress("BOOT", i, len(boot_chunks), bytes_sent, boot_size, start_time, cmd)

    print()

    # Apply boot (switches to firmware mode)
    if not dfu_apply(DFU_TYPE_FULL):
        print("ERROR: Boot apply failed")
        return False
    print("OK: Bootloader committed")

    # Phase 2: Write firmware chunks
    # Warning: After the first firmware segment write, the modem will be corrupted if
    # the update is not completed successfully.
    print("Phase 2: Firmware segments")
    print("WARNING: After the first firmware segment write, the modem will be "
          "corrupted if the update is not completed successfully.")
    bytes_sent = 0
    start_time = time.time()

    for i, (addr, data) in enumerate(fw_chunks, 1):
        success = False
        urc = ""
        for _ in range(retries):
            success, urc = dfu_write(DFU_TYPE_FULL, addr, data)
            if success:
                break
        if not success:
            print(f"\nERROR: FW chunk {i} failed after {retries} retries")
            return False

        bytes_sent += len(data)
        cmd = f"AT#XDFUWRITE={DFU_TYPE_FULL},{addr},{len(data)} -> {urc}"
        print_progress("FW", i, len(fw_chunks), bytes_sent, fw_size, start_time, cmd)

    print()

    # Apply firmware (triggers reboot)
    print(f"AT#XDFUAPPLY={DFU_TYPE_FULL}")
    send_command("AT#XDFUAPPLY", DFU_TYPE_FULL)

    # Wait for device to become responsive
    if not wait_for_device(max_attempts=10, timeout=5.0, max_backoff=5.0):
        print("ERROR: Device did not respond to AT after reboot")
        return False

    print("OK: Modem full DFU complete!")
    return True


def do_ping() -> bool:
    """Ping device with AT, return True if OK received."""
    print("AT")
    send_command("AT")
    if wait_for_response("OK", 3.0):
        print("OK")
        return True
    print("ERROR: No response")
    return False


def do_reset():
    """Reset device with AT#XRESET."""
    print("AT#XRESET")
    send_command("AT#XRESET")


def do_modem_reset():
    """Reset modem with AT#XMODEMRESET."""
    print("AT#XMODEMRESET")
    send_command("AT#XMODEMRESET")


def main() -> int:
    parser = argparse.ArgumentParser(description="Serial Modem DFU Host", allow_abbrev=False)
    parser.add_argument("--port", required=True, help="Serial port")
    parser.add_argument("--baudrate", required=True, type=int, help="Baud rate")
    parser.add_argument("--file", help="Firmware file path")
    parser.add_argument("--type", choices=["application", "modem-delta", "modem-full"],
                        help="DFU type")
    parser.add_argument("--ping", action="store_true", help="Ping device (AT)")
    parser.add_argument("--reset", action="store_true", help="Reset device (AT#XRESET)")
    parser.add_argument("--modem-reset", action="store_true",
                        help="Reset modem (AT#XMODEMRESET)")
    args = parser.parse_args()

    # Validate arguments
    utility_mode = args.ping or args.reset or getattr(args, 'modem_reset', False)
    if utility_mode:
        if args.file or args.type:
            print("ERROR: --ping/--reset/--modem-reset cannot be combined with --file or --type")
            return 1
        utility_count = sum([args.ping, args.reset, getattr(args, 'modem_reset', False)])
        if utility_count > 1:
            print("ERROR: --ping, --reset, and --modem-reset cannot be combined")
            return 1
    else:
        if not args.file or not args.type:
            print("ERROR: --file and --type are required for DFU")
            return 1

    print(f"Using: {args.port} @ {args.baudrate} baud")

    try:
        init_serial(args.port, args.baudrate)

        if args.ping:
            success = do_ping()
        elif args.reset:
            do_reset()
            success = True
        elif getattr(args, 'modem_reset', False):
            do_modem_reset()
            success = True
        else:
            filepath = Path(args.file).expanduser()
            if not filepath.is_file():
                print(f"ERROR: File not found: {filepath}")
                return 1
            print(f"File: {filepath}")
            print(f"Type: {args.type}")

            if args.type == "application":
                success = do_dfu_app(str(filepath))
            elif args.type == "modem-delta":
                success = do_dfu_delta(str(filepath))
            elif args.type == "modem-full":
                success = do_dfu_full(str(filepath))
            else:
                print(f"ERROR: Unknown DFU type: {args.type}")
                success = False
    except KeyboardInterrupt:
        print("\nAborted by user")
        success = False
    except serial.SerialException as e:
        print(f"\nERROR: Serial connection lost: {e}")
        success = False
    finally:
        close_serial()

    return 0 if success else 1


if __name__ == "__main__":
    sys.exit(main())
