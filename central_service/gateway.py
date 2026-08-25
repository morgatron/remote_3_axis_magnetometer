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
import math
import socket
import json
import queue
import asyncio
import threading
import requests

# Ensure line-buffered stdout for real-time console & log output
if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(line_buffering=True)

# Try loading local .env file if present
env_path = os.path.join(os.path.dirname(__file__), ".env")
if os.path.exists(env_path):
    try:
        with open(env_path, "r", encoding="utf-8") as f:
            for line in f:
                line = line.strip()
                if line and not line.startswith("#") and "=" in line:
                    k, v = line.split("=", 1)
                    k = k.strip()
                    v = v.strip().strip("'\"")
                    if k and k not in os.environ:
                        os.environ[k] = v
    except Exception:
        pass

# Environment Configuration
CENTRAL_SERVER_URL = os.getenv("CENTRAL_SERVER_URL", "http://localhost:8000")
API_KEY = os.getenv("API_KEY", None)
ENABLE_UDP = os.getenv("ENABLE_UDP", "true").lower() == "true"
UDP_PORT = int(os.getenv("UDP_PORT", "9876"))

ENABLE_SERIAL = os.getenv("ENABLE_SERIAL", "true").lower() == "true"
SERIAL_PORT = os.getenv("SERIAL_PORT", None)
SERIAL_BAUD = int(os.getenv("SERIAL_BAUD", "921600"))

# Check Serial / pyserial Availability
HAS_SERIAL = False
HAS_LIST_PORTS = False
if ENABLE_SERIAL or SERIAL_PORT:
    try:
        import serial
        HAS_SERIAL = True
        try:
            import serial.tools.list_ports
            HAS_LIST_PORTS = True
        except Exception as e:
            print(f"[Gateway Notice] 'serial.tools.list_ports' not supported on this platform ({e}). Auto-discovery disabled.")
    except ImportError:
        print("[Gateway Notice] 'pyserial' package not found. Serial listener will be disabled (install via 'pip install pyserial').")

# BLE disabled by default (Coded PHY Extended Advertising cannot be scanned by standard host PC Bluetooth adapters)
ENABLE_BLE = os.getenv("ENABLE_BLE", "false").lower() == "true"
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


# Import shared stream parser and termux USB driver from local folder or repository root
sys.path.insert(0, os.path.dirname(__file__))
sys.path.insert(1, os.path.abspath(os.path.join(os.path.dirname(__file__), "..")))
sys.path.insert(2, os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "scripts")))

try:
    from stream_parser import parse_telemetry_line, parse_telemetry_batch
except ImportError as e:
    print(f"[Gateway Fatal] Could not import 'stream_parser': {e}")
    sys.exit(1)

try:
    from termux_usb_driver import TermuxUsbDevice
    HAS_TERMUX_USB = True
except ImportError:
    HAS_TERMUX_USB = False


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
    if API_KEY:
        print("[Gateway Forwarder] X-API-Key authentication header configured.")
    
    headers = {"Content-Type": "application/json"}
    if API_KEY and API_KEY.strip():
        headers["X-API-Key"] = API_KEY.strip()

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
                        resp = requests.post(f"{CENTRAL_SERVER_URL}/api/v1/telemetry/batch", json=payload, headers=headers, timeout=3.0)
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
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    try:
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEPORT, 1)
    except Exception:
        pass
    sock.bind(("0.0.0.0", UDP_PORT))
    sock.settimeout(2.0)  # 2.0 second socket timeout to prevent socket read stalling
    
    while True:
        try:
            data, _ = sock.recvfrom(4096)
            arrival_time = time.time()
            raw_str = data.decode("utf-8", errors="ignore")
            samples = parse_payload_batch(raw_str, arrival_time=arrival_time)
            for sample in samples:
                if not send_queue.full():
                    send_queue.put(sample)
        except socket.timeout:
            continue
        except Exception as e:
            print(f"[Gateway UDP Error] {e}")
            time.sleep(0.5)

# --- 2. Serial / USB Listener ---
def termux_usb_listener_thread(fd: int):
    """Direct USB Bulk Transfer Reader for unrooted Android via termux-usb."""
    if not HAS_TERMUX_USB:
        print("[Gateway Error] 'termux_usb_driver' module not found. Termux USB listener disabled.")
        return

    try:
        dev = TermuxUsbDevice(fd=fd, baudrate=SERIAL_BAUD)
    except Exception as e:
        print(f"[Gateway Error] Failed to initialize Termux USB device on FD {fd}: {e}")
        return

    print(f"[Gateway Termux USB] Connected: {dev.chipset}")
    print(f"                     Bulk IN EP: 0x{dev.in_ep:02X} | DTR=1, RTS=0 (Normal Run)")

    # Kickstart MCU telemetry stream in case CLI is in idle mode
    time.sleep(0.1)
    dev.write(b"\r\nSTREAM ON\r\n")

    rx_buf = ""
    rx_count = 0
    while True:
        try:
            chunk = dev.read(size=4096, timeout_ms=500)
            if chunk:
                rx_buf += chunk.decode("utf-8", errors="ignore")
                while "\n" in rx_buf:
                    line, rx_buf = rx_buf.split("\n", 1)
                    line = line.strip()
                    if line:
                        sample = parse_csv_line(line)
                        if sample:
                            rx_count += 1
                            if rx_count % 10 == 1:
                                print(f"[Gateway Termux USB RX #{rx_count}] {sample['node_id']} -> |B| = {math.sqrt(sample['x']**2 + sample['y']**2 + sample['z']**2):.1f} nT (RSSI: {sample.get('rssi')} dBm)")
                            if not send_queue.full():
                                send_queue.put(sample)
            else:
                time.sleep(0.01)
        except Exception:
            time.sleep(0.05)

def serial_listener_thread(port, baud):
    if not HAS_SERIAL:
        print("[Gateway Notice] 'pyserial' not available. Serial listener disabled.")
        return

    rx_count = 0
    while True:
        target_port = port
        if not target_port:
            # Auto-detect available ESP32 USB CDC / ACM / USB serial ports if supported
            acm_ports = []
            if HAS_LIST_PORTS:
                try:
                    acm_ports = sorted([p.device for p in serial.tools.list_ports.comports() if 'ACM' in p.device or 'USB' in p.device])
                except Exception:
                    acm_ports = []
            if acm_ports:
                target_port = acm_ports[0]
            else:
                target_port = "/dev/ttyACM0"

        try:
            print(f"[Gateway Serial] Opening {target_port} at {baud} baud...")
            ser = serial.Serial(target_port, baud, timeout=1.0)
            ser.dtr = True
            ser.rts = False # CRITICAL: RTS must be False for ESP32 run mode
            print(f"[Gateway Serial Connected] Active on {target_port}")

            # Send STREAM ON trigger
            ser.write(b"\r\nSTREAM ON\r\n")

            while True:
                line = ser.readline().decode("utf-8", errors="ignore").strip()
                if line:
                    sample = parse_csv_line(line)
                    if sample:
                        rx_count += 1
                        if rx_count % 10 == 1:
                            print(f"[Gateway Serial RX #{rx_count}] {sample['node_id']} -> |B| = {math.sqrt(sample['x']**2 + sample['y']**2 + sample['z']**2):.1f} nT (RSSI: {sample.get('rssi')} dBm)")
                        if not send_queue.full():
                            send_queue.put(sample)
        except Exception as e:
            print(f"[Gateway Serial Disconnected] {target_port}: {e}. Retrying in 2.0s...")
            time.sleep(2.0)



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

    # Start Serial / Termux USB Listener
    termux_fd = None
    if os.getenv("TERMUX_USB_FD"):
        try:
            termux_fd = int(os.getenv("TERMUX_USB_FD"))
        except ValueError:
            pass
    elif len(sys.argv) > 1 and sys.argv[1].isdigit():
        try:
            termux_fd = int(sys.argv[1])
        except ValueError:
            pass

    if termux_fd is not None:
        t_termux_usb = threading.Thread(target=termux_usb_listener_thread, args=(termux_fd,), daemon=True)
        t_termux_usb.start()
    elif (ENABLE_SERIAL or SERIAL_PORT) and HAS_SERIAL:
        t_ser = threading.Thread(target=serial_listener_thread, args=(SERIAL_PORT, SERIAL_BAUD), daemon=True)
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
