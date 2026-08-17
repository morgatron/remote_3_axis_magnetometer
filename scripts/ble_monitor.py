#!/usr/bin/env python3
"""
BLE Advertising Monitor for Remote 3-Axis Magnetometer Sensor Nodes.

Passively scans BLE advertisements from your Linux laptop's Bluetooth adapter
and decodes SensorBinaryPacket manufacturer data payloads in real time.

Requires: bleak  (pip install bleak)

Usage:
    python3 ble_monitor.py              # scan for 30 seconds
    python3 ble_monitor.py --duration 60 # scan for 60 seconds
    python3 ble_monitor.py --duration 0  # scan forever (Ctrl+C to stop)

Expected packet layout (26 bytes, packed):
    Offset  Size   Field
    0       8      device_id       char[8]  null-terminated ASCII
    8       4      timestamp_ms    uint32   MCU millis() uptime
    12      4      x_nT            float    magnetic field X (nT)
    16      4      y_nT            float    magnetic field Y (nT)
    20      4      z_nT            float    magnetic field Z (nT)
    24      2      status          uint16   hardware status word
"""

import argparse
import asyncio
import struct
import time
from bleak import BleakScanner

# SensorBinaryPacket: 8s I f f f H  = 26 bytes
PACKET_FMT = "<8s I f f f H"
PACKET_SIZE = struct.calcsize(PACKET_FMT)  # 26

# BLE Manufacturer Specific Data AD type = 0xFF
# NimBLE prepends a 2-byte company ID to the manufacturer data,
# so the raw payload from bleak may be 28 bytes (2 + 26).
# We handle both 26 and 28 byte payloads.

last_ts_by_device = {}
pkt_count_by_device = {}


def decode_sensor_packet(raw: bytes):
    """Decode a SensorBinaryPacket from raw manufacturer data bytes.
    Returns (device_id, timestamp_ms, x, y, z, status) or None on failure."""
    if len(raw) < PACKET_SIZE:
        return None

    # Skip company ID prefix if present (NimBLE adds 2 bytes)
    offset = len(raw) - PACKET_SIZE
    if offset > 4:
        return None  # Too much extra data, probably not our packet

    data = raw[offset:]
    try:
        device_id_raw, ts_ms, x, y, z, status = struct.unpack(PACKET_FMT, data)
    except struct.error:
        print("Invalid packetL ", data)
        return None

    # Decode device_id (null-terminated ASCII)
    try:
        device_id = device_id_raw.split(b'\x00', 1)[0].decode('ascii')
    except (UnicodeDecodeError, ValueError):
        return None

    # Validate: printable ASCII device ID
    if not device_id or not all(32 <= ord(c) <= 126 for c in device_id):
        return None

    # Validate: finite float values within plausible magnetometer range
    import math
    for val in (x, y, z):
        if math.isnan(val) or math.isinf(val) or abs(val) > 10_000_000.0:
            return None

    return device_id, ts_ms, x, y, z, status


def on_advertisement(device, advertisement_data):
    """Callback for each BLE advertisement received."""
    mfr_data = advertisement_data.manufacturer_data
    if not mfr_data:
        return

    for company_id, payload in mfr_data.items():
        # Try decoding with the company_id bytes prepended (as bleak strips them)
        # and also the raw payload directly
        result = decode_sensor_packet(payload)
        if result is None:
            # Try prepending company ID as 2 LE bytes
            full = struct.pack("<H", company_id) + payload
            result = decode_sensor_packet(full)

        if result is not None:
            device_id, ts_ms, x, y, z, status = result
            now = time.time()

            # De-duplicate: skip if same timestamp as last seen
            if device_id in last_ts_by_device and last_ts_by_device[device_id] == ts_ms:
                return

            last_ts_by_device[device_id] = ts_ms
            pkt_count_by_device[device_id] = pkt_count_by_device.get(device_id, 0) + 1
            count = pkt_count_by_device[device_id]

            bmag = (x**2 + y**2 + z**2) ** 0.5

            name = advertisement_data.local_name or device.name or "?"
            rssi = advertisement_data.rssi if hasattr(advertisement_data, 'rssi') else "?"
            if rssi == "?" and hasattr(device, 'rssi'):
                rssi = device.rssi

            print(
                f"[#{count:4d}] {device_id:8s} | "
                f"ts={ts_ms:10d}ms | "
                f"B=({x:+10.2f}, {y:+10.2f}, {z:+10.2f}) nT | "
                f"|B|={bmag:10.2f} nT | "
                f"status=0x{status:04X} | "
                f"RSSI={rssi} dBm | "
                f"addr={device.address}"
            )


async def scan(duration: float):
    """Run BLE scan for the given duration (0 = indefinite)."""
    print(f"{'='*100}")
    print(f"BLE Advertising Monitor — Scanning for SensorBinaryPacket ({PACKET_SIZE} bytes)")
    print(f"{'='*100}")
    print(f"  Packet format: device_id[8] + timestamp_ms[4] + x_nT[4] + y_nT[4] + z_nT[4] + status[2]")
    print(f"  Duration: {'indefinite (Ctrl+C to stop)' if duration == 0 else f'{duration}s'}")
    print(f"{'='*100}")
    print()

    scanner = BleakScanner(detection_callback=on_advertisement)
    await scanner.start()

    try:
        if duration == 0:
            while True:
                await asyncio.sleep(1.0)
        else:
            await asyncio.sleep(duration)
    except asyncio.CancelledError:
        pass
    finally:
        await scanner.stop()

    print()
    print(f"{'='*100}")
    print("Summary:")
    for dev_id, count in sorted(pkt_count_by_device.items()):
        print(f"  {dev_id}: {count} unique packets received")
    if not pkt_count_by_device:
        print("  (no valid sensor packets detected)")
    print(f"{'='*100}")


def main():
    parser = argparse.ArgumentParser(description="BLE monitor for magnetometer sensor nodes")
    parser.add_argument("--duration", type=float, default=30,
                        help="Scan duration in seconds (0 = indefinite, default 30)")
    args = parser.parse_args()

    try:
        asyncio.run(scan(args.duration))
    except KeyboardInterrupt:
        print("\n\nStopped by user.")
        for dev_id, count in sorted(pkt_count_by_device.items()):
            print(f"  {dev_id}: {count} unique packets received")


if __name__ == "__main__":
    main()
