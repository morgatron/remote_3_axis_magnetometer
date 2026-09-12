#!/usr/bin/env python3
"""
Device Profile Configurator & Flasher (`scripts/configure_profile.py`)

Automates building, flashing, and configuring the in-use project profiles:
  1. SPRINGBANK    - Heltec V4 Sensor (ESP32-S3 + FLC100 Fluxgate, LoRa-only batch transmission)
  2. ROOF          - Heltec V4 Receiver (LoRa RX, USB Serial egress to host computer)
  3. CREEK         - Seeed Studio XIAO ESP32-C6 Sensor (FLC100 Fluxgate, BLE Coded PHY batch transmission)
  4. CREEK_TEST    - Seeed Studio XIAO ESP32-C6 Sensor Test Unit (FLC100 Fluxgate, node ID: _CREEK)
  5. CREEK_GATEWAY - Seeed Studio XIAO ESP32-C6 Receiver Gateway (BLE Coded PHY RX -> 1M BLE NUS Relay)

Usage:
  python3 scripts/configure_profile.py --profile SPRINGBANK [--port /dev/ttyACM0]
  python3 scripts/configure_profile.py --profile ROOF [--port /dev/ttyACM0]
  python3 scripts/configure_profile.py --profile CREEK [--port /dev/ttyACM0]
  python3 scripts/configure_profile.py --profile CREEK_TEST [--port /dev/ttyACM0]
  python3 scripts/configure_profile.py --profile CREEK_GATEWAY [--port /dev/ttyACM0]
  python3 scripts/configure_profile.py --profile SPRINGBANK --no-flash   # Skip PlatformIO upload
  python3 scripts/configure_profile.py --profile SPRINGBANK --mock       # Run synthetic test data
"""

import sys
import time
import glob
import serial
import argparse
import subprocess
from typing import Optional, Dict, Any

PROFILES: Dict[str, Dict[str, Any]] = {
    "SPRINGBANK": {
        "description": "Heltec V4 Sensor + FLC100 (LoRa-only batch transmission)",
        "board_type": "Heltec WiFi LoRa 32 V4 (ESP32-S3 + SX1262)",
        "pio_env": "heltec_v4_sensor",
        "role": "sensor",
        "cli_commands": [
            "ID SPRINGBANK",
            "MODE LORA",
            "BATCH 10",
            "SENSOR FLC100",
            "DOWNSAMPLE 1000",
            "START",
            "SAVE",
            "STATUS"
        ],
        "expected_current": {
            "idle_sleep": "12 - 15 mA (MCU) + 14 mA (FLC100/Boost/ADC) = ~26 - 29 mA",
            "tx_peak": "110 - 130 mA (+22 dBm LoRa burst for ~180 ms)",
            "average": "28 - 32 mA (with real FLC100) / ~15 - 18 mA (mock data)",
            "runtime_1000mah": "~30 - 35 hours (~1.3 days)",
            "runtime_3000mah": "~4 - 4.5 days"
        }
    },
    "ROOF": {
        "description": "Heltec V4 Receiver (LoRa RX -> USB Serial Egress)",
        "board_type": "Heltec WiFi LoRa 32 V4 (ESP32-S3 + SX1262)",
        "pio_env": "heltec_v4_receiver",
        "role": "receiver",
        "cli_commands": [
            "MODE SERIAL",
            "SAVE",
            "STATUS"
        ],
        "expected_current": {
            "idle_sleep": "N/A (Continuous RX)",
            "tx_peak": "N/A (Receiver only)",
            "average": "28 - 35 mA @ 5V USB (~140 - 175 mW)",
            "runtime_1000mah": "Wired USB powered (Battery: ~30 - 35 hours)",
            "runtime_3000mah": "Wired USB powered (Battery: ~4 days)"
        }
    },
    "CREEK": {
        "description": "Seeed Studio XIAO ESP32-C6 Sensor + FLC100 (BLE Coded PHY batch transmission)",
        "board_type": "Seeed Studio XIAO ESP32-C6 (RISC-V)",
        "pio_env": "esp32-c6-devkitc-1",
        "role": "sensor",
        "cli_commands": [
            "ID CREEK",
            "MODE BLE",
            "BATCH 10",
            "SENSOR FLC100",
            "DOWNSAMPLE 1000",
            "START",
            "SAVE",
            "STATUS"
        ],
        "expected_current": {
            "idle_sleep": "7 - 9 mA (C6 RISC-V) + 14 mA (FLC100/Boost/ADC) = ~21 - 23 mA",
            "tx_peak": "50 - 65 mA (+15 dBm BLE Coded PHY burst for 1.0s)",
            "average": "22 - 25 mA (with real FLC100) / ~8 - 10 mA (mock data)",
            "runtime_1000mah": "~40 - 45 hours (~1.8 days)",
            "runtime_3000mah": "~5 - 6 days"
        }
    },
    "CREEK_TEST": {
        "description": "Seeed Studio XIAO ESP32-C6 Sensor Test Unit + FLC100 (node ID: _CREEK)",
        "board_type": "Seeed Studio XIAO ESP32-C6 (RISC-V)",
        "pio_env": "esp32-c6-devkitc-1",
        "role": "sensor",
        "cli_commands": [
            "ID _CREEK",
            "MODE BLE",
            "BATCH 10",
            "SENSOR FLC100",
            "DOWNSAMPLE 1000",
            "START",
            "SAVE",
            "STATUS"
        ],
        "expected_current": {
            "idle_sleep": "7 - 9 mA (C6 RISC-V) + 14 mA (FLC100/Boost/ADC) = ~21 - 23 mA",
            "tx_peak": "50 - 65 mA (+15 dBm BLE Coded PHY burst for 1.0s)",
            "average": "22 - 25 mA (with real FLC100) / ~8 - 10 mA (mock data)",
            "runtime_1000mah": "~40 - 45 hours (~1.8 days)",
            "runtime_3000mah": "~5 - 6 days"
        }
    },
    "CREEK2": {
        "description": "ESP32-C3 Supermini Sensor + FLC100 (BLE Coded PHY batch transmission)",
        "board_type": "ESP32-C3 Supermini (RISC-V)",
        "pio_env": "esp32-c3-devkitm-1",
        "role": "sensor",
        "cli_commands": [
            "ID CREEK2",
            "MODE BLE",
            "BATCH 10",
            "SENSOR FLC100",
            "DOWNSAMPLE 1000",
            "START",
            "SAVE",
            "STATUS"
        ],
        "expected_current": {
            "idle_sleep": "11 - 13 mA (C3 RISC-V) + 14 mA (FLC100/Boost/ADC) = ~25 - 27 mA",
            "tx_peak": "60 - 80 mA (+20 dBm BLE Coded PHY burst for 1.0s)",
            "average": "26 - 29 mA (with real FLC100) / ~12 - 14 mA (mock data)",
            "runtime_1000mah": "~35 - 38 hours (~1.5 days)",
            "runtime_3000mah": "~4.5 - 5 days"
        }
    },
    "CREEK_GATEWAY": {
        "description": "Seeed Studio XIAO ESP32-C6 Receiver Gateway (BLE Coded PHY RX -> 1M BLE NUS Relay)",
        "board_type": "Seeed Studio XIAO ESP32-C6 (RISC-V)",
        "pio_env": "esp32c6_receiver",
        "role": "receiver",
        "cli_commands": [
            "MODE BLE",
            "SAVE",
            "STATUS"
        ],
        "expected_current": {
            "idle_sleep": "7 - 9 mA (C6 RISC-V idle) + Wi-Fi OFF",
            "tx_peak": "25 - 35 mA (1M BLE GATT Notification bursts)",
            "average": "11 - 14 mA (25% duty-cycle Coded scan + 1M BLE connected)",
            "runtime_1000mah": "~75 - 90 hours (~3.5 days)",
            "runtime_3000mah": "~9 - 11 days"
        }
    },
    "SUPERMINI_GATEWAY": {
        "description": "ESP32-C3 Supermini Receiver Gateway (BLE Coded PHY RX -> 1M BLE NUS Relay)",
        "board_type": "ESP32-C3 Supermini (RISC-V)",
        "pio_env": "esp32c6_receiver" if False else "esp32c3_receiver",
        "role": "receiver",
        "cli_commands": [
            "MODE BLE",
            "SAVE",
            "STATUS"
        ],
        "expected_current": {
            "idle_sleep": "11 - 13 mA (C3 RISC-V idle) + Wi-Fi OFF",
            "tx_peak": "25 - 35 mA (1M BLE GATT Notification bursts)",
            "average": "15 - 18 mA (25% duty-cycle Coded scan + 1M BLE connected)",
            "runtime_1000mah": "~55 - 65 hours (~2.5 days)",
            "runtime_3000mah": "~7 - 8 days"
        }
    }
}


def find_serial_port() -> Optional[str]:
    ports = glob.glob("/dev/ttyACM*") + glob.glob("/dev/ttyUSB*")
    return ports[0] if ports else None


def flash_firmware(env: str, port: str):
    print(f"\n[*] Building & Flashing PlatformIO environment: '{env}' to {port}...")
    pio_bin = "pio"
    # Check ~/.platformio/penv/bin/pio fallback
    import shutil
    import os
    if not shutil.which("pio"):
        fallback = os.path.expanduser("~/.platformio/penv/bin/pio")
        if os.path.exists(fallback):
            pio_bin = fallback

    cmd = [pio_bin, "run", "-e", env, "-t", "upload", "--upload-port", port]
    res = subprocess.run(cmd)
    if res.returncode != 0:
        print(f"\n[ERROR] Flashing environment '{env}' failed!")
        sys.exit(1)
    print(f"[SUCCESS] Environment '{env}' flashed successfully.\n")


def configure_device(port: str, commands: list, is_mock: bool = False):
    print(f"[*] Connecting to {port} @ 921600 baud to configure parameters...")
    time.sleep(2.0)  # Wait for USB reset after flashing
    try:
        s = serial.Serial()
        s.port = port
        s.baudrate = 921600
        s.timeout = 1.0
        s.dtr = False
        s.rts = False
        s.open()
    except Exception as e:
        print(f"[ERROR] Unable to open serial port {port}: {e}")
        sys.exit(1)

    time.sleep(1.0)
    s.reset_input_buffer()
    s.write(b"\n")
    time.sleep(0.3)

    for cmd in commands:
        print(f"  -> Sending: {cmd}")
        s.write((cmd + "\n").encode("utf-8"))
        time.sleep(0.35)

    time.sleep(1.0)
    response = s.read_all().decode("utf-8", errors="replace")

    # If physical sensor was requested but response indicates MOCK or failure, retry SENSOR command
    if not is_mock and any(c.startswith("SENSOR ") and not "MOCK" in c for c in commands):
        target_sensor_cmd = [c for c in commands if c.startswith("SENSOR ")][0]
        if "failed initialization" in response or "MOCK_SENSOR_ACTIVE" in response or "Sensor: Mock" in response:
            print(f"\n[WARNING] Sensor initialization reported failure on initial probe. Retrying '{target_sensor_cmd}'...")
            time.sleep(0.5)
            s.write((target_sensor_cmd + "\n").encode("utf-8"))
            time.sleep(0.5)
            s.write(b"SAVE\nSTATUS\n")
            time.sleep(1.0)
            retry_resp = s.read_all().decode("utf-8", errors="replace")
            response += "\n--- Retry Output ---\n" + retry_resp

    s.close()

    print("\n--- Device Serial Output ---")
    for line in response.splitlines():
        if line.strip():
            print(f"  {line}")
    print("----------------------------\n")

    if not is_mock and any(c.startswith("SENSOR ") and not "MOCK" in c for c in commands):
        if "MOCK_SENSOR_ACTIVE" in response or "Sensor: Mock" in response:
            print("\n[ERROR] Device is still running in MOCK mode! Check hardware seating and power.")
        else:
            print("\n[SUCCESS] Physical sensor successfully verified active on hardware.")


def print_power_summary(profile_name: str):
    p = PROFILES[profile_name]
    exp = p["expected_current"]
    print("=" * 80)
    print(f"        BENCH CURRENT DRAW PREDICTION: [{profile_name}]")
    print("=" * 80)
    print(f"  Hardware:            {p['board_type']}")
    print(f"  Profile Role:        {p['description']}")
    print("-" * 80)
    print(f"  Base Idle Current:   {exp['idle_sleep']}")
    print(f"  RF Peak Spikes:      {exp['tx_peak']}")
    print(f"  Expected Avg Draw:   {exp['average']}")
    print("-" * 80)
    print(f"  1,000 mAh LiPo Life: {exp['runtime_1000mah']}")
    print(f"  3,000 mAh 18650 Life:{exp['runtime_3000mah']}")
    print("=" * 80)


def main():
    parser = argparse.ArgumentParser(
        description="Configure and flash devices to the 6 in-use project profiles."
    )
    parser.add_argument(
        "--profile",
        choices=["SPRINGBANK", "ROOF", "CREEK", "CREEK_TEST", "CREEK2", "CREEK_GATEWAY", "SUPERMINI_GATEWAY"],
        required=True,
        help="Target profile name to deploy"
    )
    parser.add_argument("--port", type=str, help="Target serial port (e.g. /dev/ttyACM0). Auto-detects if omitted.")
    parser.add_argument("--no-flash", action="store_true", help="Skip PlatformIO flashing; only send serial CLI configuration")
    parser.add_argument("--mock", action="store_true", help="Use synthetic mock sensor data instead of real FLC100 hardware")

    args = parser.parse_args()

    port = args.port or find_serial_port()
    if not port:
        print("[ERROR] No serial port detected. Connect the board and retry.")
        sys.exit(1)

    profile = PROFILES[args.profile]
    commands = list(profile["cli_commands"])

    if args.mock and profile["role"] == "sensor":
        commands = [c if not c.startswith("SENSOR ") else "SENSOR MOCK" for c in commands]
        commands = [c if not c.startswith("DOWNSAMPLE ") else "DOWNSAMPLE 75" for c in commands]
        print("[NOTICE] Configuring device with --mock (synthetic test data).")

    print("\n" + "=" * 80)
    print(f"  CONFIGURING TARGET: [{args.profile}] on port {port}")
    print("=" * 80)

    if not args.no_flash:
        flash_firmware(profile["pio_env"], port)

    configure_device(port, commands, is_mock=args.mock)
    print_power_summary(args.profile)


if __name__ == "__main__":
    main()
