"""
Unified Sensor Gateway Service (`gateway.py`)

Bridges isolated field sensor networks (WiFi UDP, Bluetooth LE, Serial/USB)
to the Central Data Server via HTTP POST (/api/telemetry/batch).

Supported Interfaces (all run concurrently if enabled):
1. WiFi / UDP Listener (Port 9876) - Receives UDP CSV streams from ESP32 nodes
2. Bluetooth LE Listener (Nordic UART Service) - Connects as BLE Central to ESP32 nodes
3. Serial / USB Listener - Reads CSV strings directly from microcontrollers

Features:
- Unified Store-and-Forward Queue for offline network resilience
- Graceful degradation (e.g. BLE disabled automatically if 'bleak' is not installed)
"""

import os
import sys
import time
import socket
import json
import queue
import asyncio
import threading
import requests

# Environment Configuration
CENTRAL_SERVER_URL = os.getenv("CENTRAL_SERVER_URL", "http://localhost:8000")
ENABLE_UDP = os.getenv("ENABLE_UDP", "true").lower() == "true"
UDP_PORT = int(os.getenv("UDP_PORT", "9876"))

ENABLE_SERIAL = os.getenv("ENABLE_SERIAL", "false").lower() == "true"
SERIAL_PORT = os.getenv("SERIAL_PORT", None)
SERIAL_BAUD = int(os.getenv("SERIAL_BAUD", "921600"))

ENABLE_BLE = os.getenv("ENABLE_BLE", "true").lower() == "true"
NUS_SERVICE_UUID = "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
NUS_TX_CHAR_UUID = "6e400003-b5a3-f393-e0a9-e50e24dcca9e"

# Check Bleak Availability for BLE
HAS_BLEAK = False
if ENABLE_BLE:
    try:
        from bleak import BleakScanner, BleakClient
        HAS_BLEAK = True
    except ImportError:
        print("[Gateway Notice] 'bleak' package not found. BLE listener will be disabled (install via 'pip install bleak').")

# Import shared stream parser from repository root
sys.path.append(os.path.abspath(os.path.join(os.path.dirname(__file__), "..")))
from stream_parser import parse_telemetry_line, parse_telemetry_batch

# Thread-safe / Async-safe Telemetry Queue
send_queue = queue.Queue(maxsize=10000)

def parse_payload_batch(raw_payload: str, arrival_time: float = None):
    """Parses multi-line CSV payload and applies relative delta-t back-calculation anchored to arrival_time."""
    parsed_list = parse_telemetry_batch(raw_payload, arrival_wall_time=arrival_time)
    results = []
    for parsed in parsed_list:
        results.append({
            "node_id": parsed["node_id"],
            "x": parsed["x"],
            "y": parsed["y"],
            "z": parsed["z"],
            "status_flags": parsed["status_hex"],
            "timestamp": parsed["timestamp_iso"],
            "temp": parsed.get("temp"),
            "vbat": parsed.get("vbat"),
            "rssi": parsed.get("rssi")
        })
    return results

def parse_csv_line(line: str):
    """Parses standard CSV line from ESP32 using shared stream_parser module."""
    parsed = parse_telemetry_line(line)
    if parsed:
        return {
            "node_id": parsed["node_id"],
            "x": parsed["x"],
            "y": parsed["y"],
            "z": parsed["z"],
            "status_flags": parsed["status_hex"],
            "timestamp": parsed["timestamp_iso"],
            "temp": parsed.get("temp"),
            "vbat": parsed.get("vbat"),
            "rssi": parsed.get("rssi")
        }
    return None

def forwarder_worker():
    """Flushes queued telemetry from all interfaces to the Central Data Server."""
    print(f"[Gateway Forwarder] Worker active. Target: {CENTRAL_SERVER_URL}")
    
    while True:
        try:
            if not send_queue.empty():
                batches_by_node = {}
                count = 0
                while not send_queue.empty() and count < 100:
                    try:
                        sample = send_queue.get_nowait()
                        nid = sample["node_id"]
                        if nid not in batches_by_node:
                            batches_by_node[nid] = []
                        batches_by_node[nid].append(sample)
                        count += 1
                    except queue.Empty:
                        break

                for nid, node_batch in batches_by_node.items():
                    payload = {"node_id": nid, "points": node_batch}
                    try:
                        resp = requests.post(f"{CENTRAL_SERVER_URL}/api/v1/telemetry/batch", json=payload, timeout=3.0)
                        if resp.status_code != 201:
                            print(f"[Gateway Warning] HTTP {resp.status_code} from central server: {resp.text}")
                            for s in node_batch:
                                send_queue.put(s)
                            time.sleep(1.0)
                    except Exception as net_err:
                        print(f"[Gateway Network Error] Central Server unreachable: {net_err}")
                        for s in node_batch:
                            send_queue.put(s)
                        time.sleep(2.0)

            time.sleep(0.2)
        except Exception as e:
            print(f"[Gateway Error] {e}")
            time.sleep(1.0)

# --- 1. UDP Listener ---
def udp_listener_thread():
    print(f"[Gateway UDP] Listening for telemetry on 0.0.0.0:{UDP_PORT}...")
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind(("0.0.0.0", UDP_PORT))
    
    while True:
        try:
            data, _ = sock.recvfrom(4096)
            arrival_time = time.time()
            raw_str = data.decode("utf-8", errors="ignore")
            samples = parse_payload_batch(raw_str, arrival_time=arrival_time)
            for sample in samples:
                if not send_queue.full():
                    send_queue.put(sample)
        except Exception as e:
            print(f"[Gateway UDP Error] {e}")
            time.sleep(0.5)

# --- 2. Serial / USB Listener ---
def serial_listener_thread(port, baud):
    try:
        import serial
        print(f"[Gateway Serial] Opening {port} at {baud} baud...")
        ser = serial.Serial(port, baud, timeout=1.0)
        while True:
            line = ser.readline().decode("utf-8", errors="ignore").strip()
            if line:
                sample = parse_csv_line(line)
                if sample and not send_queue.full():
                    send_queue.put(sample)
    except Exception as e:
        print(f"[Gateway Serial Error] {e}")

# --- 3. BLE Listener (Async) ---
async def ble_listener_loop():
    connected_devices = set()
    print("[Gateway BLE] Scanning for BLE Magnetometers (Nordic UART Service)...")
    
    async def connect_node(device):
        addr = device.address
        name = device.name or addr
        if addr in connected_devices:
            return
        connected_devices.add(addr)
        print(f"[Gateway BLE] Connecting to '{name}' ({addr})...")
        
        def notify_handler(sender, data: bytearray):
            raw_str = data.decode("utf-8", errors="ignore")
            for line in raw_str.splitlines():
                line = line.strip()
                if line:
                    sample = parse_csv_line(line)
                    if sample and not send_queue.full():
                        send_queue.put(sample)

        try:
            async with BleakClient(addr, timeout=10.0) as client:
                print(f"[Gateway BLE] Connected to '{name}'. Subscribing...")
                await client.start_notify(NUS_TX_CHAR_UUID, notify_handler)
                while client.is_connected:
                    await asyncio.sleep(2.0)
        except Exception as e:
            print(f"[Gateway BLE] Connection lost with '{name}': {e}")
        finally:
            connected_devices.discard(addr)

    while True:
        try:
            devices = await BleakScanner.discover(service_uuids=[NUS_SERVICE_UUID], timeout=4.0)
            for dev in devices:
                if dev.address not in connected_devices:
                    asyncio.create_task(connect_node(dev))
        except Exception as err:
            print(f"[Gateway BLE Scan Error] {err}")
        await asyncio.sleep(5.0)

def start_ble_thread():
    loop = asyncio.new_event_loop()
    asyncio.set_event_loop(loop)
    loop.run_until_complete(ble_listener_loop())

if __name__ == "__main__":
    print("=== Starting Unified Magnetometer Gateway ===")

    # Start Forwarder Thread
    t_fwd = threading.Thread(target=forwarder_worker, daemon=True)
    t_fwd.start()

    # Start UDP Listener
    if ENABLE_UDP:
        t_udp = threading.Thread(target=udp_listener_thread, daemon=True)
        t_udp.start()

    # Start Serial Listener
    if ENABLE_SERIAL or SERIAL_PORT:
        port = SERIAL_PORT or "/dev/ttyUSB0"
        t_ser = threading.Thread(target=serial_listener_thread, args=(port, SERIAL_BAUD), daemon=True)
        t_ser.start()

    # Start BLE Listener
    if ENABLE_BLE and HAS_BLEAK:
        t_ble = threading.Thread(target=start_ble_thread, daemon=True)
        t_ble.start()

    # Keep main thread alive
    try:
        while True:
            time.sleep(1.0)
    except KeyboardInterrupt:
        print("\n[Gateway] Shutting down...")
