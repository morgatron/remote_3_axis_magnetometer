#!/usr/bin/env python3
"""
Remote Node Provisioning Script (`provision_node.py`)

Automates hardware provisioning for ESP32-based magnetometer nodes over Serial CLI.

Default Sensor Profiles:
  - RM3100:  75 Hz ODR (0x95), 200 cycle count (max safe for 75Hz), 75x downsample -> 1 Hz output stream.
  - FLC100:  1 kSPS ADC rate (0x06), 1000x downsample -> 1 Hz output stream.
  - Default Output Mode: WIFI (WiFi UDP streaming only, remote mode).

Configuration Priority:
  1. Command line arguments (--sensor, --port, --ssid, etc.)
  2. provision_config.json
  3. Legacy wifi.cfg (SSID / Password fallback)

Usage:
  python3 provision_node.py --sensor RM3100 --port /dev/ttyACM0
  python3 provision_node.py --sensor FLC100 --port /dev/ttyUSB0
"""

import sys
import os
import time
import json
import argparse

try:
    import serial
except ImportError:
    print("Error: 'pyserial' package is required. Install via: pip install pyserial")
    sys.exit(1)


def load_wifi_cfg_fallback():
    """Parses legacy wifi.cfg file if available."""
    wifi_cfg_path = os.path.join(os.path.dirname(__file__), "wifi.cfg")
    ssid, pwd = "", ""
    if os.path.exists(wifi_cfg_path):
        try:
            with open(wifi_cfg_path, "r", encoding="utf-8") as f:
                for line in f:
                    line = line.strip()
                    if line.startswith("SSID:"):
                        ssid = line.split(":", 1)[1].strip()
                    elif line.startswith("PWD:"):
                        pwd = line.split(":", 1)[1].strip()
        except Exception:
            pass
    return ssid, pwd


def load_config(config_path="provision_config.json"):
    """Loads configuration JSON file, applying defaults and fallback wifi.cfg."""
    defaults = {
        "port": "/dev/ttyACM0",
        "baud": 921600,
        "sensor": "RM3100",
        "device_id": "",
        "output_mode": "WIFI",
        "wifi": {
            "ssid": "",
            "password": "",
            "target_ip": "255.255.255.255"
        },
        "rm3100": {
            "rate": "0x95",
            "cycle_count": 200,
            "downsample": 75
        },
        "flc100": {
            "rate": "0x06",
            "downsample": 1000
        }
    }

    if os.path.exists(config_path):
        try:
            with open(config_path, "r", encoding="utf-8") as f:
                data = json.load(f)
                # Deep merge top-level and nested dicts
                for key in ["port", "baud", "sensor", "device_id", "output_mode"]:
                    if key in data:
                        defaults[key] = data[key]
                if "wifi" in data and isinstance(data["wifi"], dict):
                    defaults["wifi"].update(data["wifi"])
                if "rm3100" in data and isinstance(data["rm3100"], dict):
                    defaults["rm3100"].update(data["rm3100"])
                if "flc100" in data and isinstance(data["flc100"], dict):
                    defaults["flc100"].update(data["flc100"])
        except Exception as e:
            print(f"[CONFIG WARNING] Could not read {config_path}: {e}")

    # Fallback to wifi.cfg if SSID is still empty
    if not defaults["wifi"]["ssid"]:
        fb_ssid, fb_pass = load_wifi_cfg_fallback()
        if fb_ssid:
            defaults["wifi"]["ssid"] = fb_ssid
            defaults["wifi"]["password"] = fb_pass

    return defaults


def send_cli_cmd(ser, cmd: str, delay_sec=0.4):
    """Sends a CLI command to the ESP32 and prints returned output lines."""
    print(f"  >>> {cmd}")
    ser.write((cmd + "\r\n").encode("utf-8"))
    time.sleep(delay_sec)
    lines = []
    while ser.in_waiting:
        line = ser.readline().decode("utf-8", errors="ignore").strip()
        if line:
            print(f"      [MCU] {line}")
            lines.append(line)
    return lines


def provision(args):
    cfg = load_config(args.config)

    # Command line argument overrides
    port = args.port or cfg["port"]
    baud = args.baud or cfg["baud"]
    sensor_type = (args.sensor or cfg["sensor"]).upper()
    device_id = args.id if args.id is not None else cfg["device_id"]
    output_mode = (args.mode or cfg["output_mode"]).upper()

    ssid = args.ssid if args.ssid is not None else cfg["wifi"]["ssid"]
    password = args.pass_word if args.pass_word is not None else cfg["wifi"]["password"]
    target_ip = args.target if args.target is not None else cfg["wifi"]["target_ip"]

    # Profile selection
    if sensor_type in ["RM3100", "MOCK"]:
        rate = args.rate or cfg["rm3100"]["rate"]
        cycle_count = args.cycle if args.cycle is not None else cfg["rm3100"]["cycle_count"]
        downsample = args.downsample if args.downsample is not None else cfg["rm3100"]["downsample"]
    else:  # FLC100
        rate = args.rate or cfg["flc100"]["rate"]
        cycle_count = None
        downsample = args.downsample if args.downsample is not None else cfg["flc100"]["downsample"]

    print("\n" + "=" * 70)
    print(" REMOTE NODE PROVISIONING SUMMARY")
    print("=" * 70)
    print(f"  Target Port:     {port} @ {baud} baud")
    print(f"  Sensor Model:    {sensor_type}")
    if sensor_type == "RM3100":
        print(f"  Hardware Rate:   {rate} (75 Hz ODR)")
        print(f"  Cycle Count:     {cycle_count} (Max safe for 75 Hz)")
        print(f"  Downsample Ratio:{downsample}x -> Effective Stream Rate: {75 // downsample} Hz")
    else:
        print(f"  Hardware Rate:   {rate} (1 kSPS ADC)")
        print(f"  Downsample Ratio:{downsample}x -> Effective Stream Rate: {1000 // downsample} Hz")
    print(f"  Output Mode:     {output_mode} (Remote Mode)")
    print(f"  WiFi SSID:       '{ssid}'")
    print(f"  Target IP:       {target_ip}")
    if device_id:
        print(f"  Device ID:       {device_id}")
    print("=" * 70 + "\n")

    if not ssid:
        print("[PROVISION WARNING] WiFi SSID is empty! The node will not be able to connect to WiFi.")
        print("                  Provide --ssid <SSID> or update provision_config.json / wifi.cfg.\n")

    print(f"Connecting to ESP32 on {port}...")
    try:
        ser = serial.Serial(port, baud, timeout=0.5)
    except Exception as e:
        print(f"[ERROR] Failed to open serial port {port}: {e}")
        sys.exit(1)

    time.sleep(1.0)
    ser.reset_input_buffer()

    print("\nExecuting CLI Provisioning Sequence...")

    # 1. Stop continuous streaming while provisioning
    send_cli_cmd(ser, "STREAM OFF")

    # 2. Select sensor model
    send_cli_cmd(ser, f"SENSOR {sensor_type}", delay_sec=0.8)

    # 3. Configure cycle count (RM3100 specific)
    if sensor_type == "RM3100" and cycle_count is not None:
        send_cli_cmd(ser, f"CYCLE {cycle_count}")

    # 4. Configure hardware output data rate
    send_cli_cmd(ser, f"RATE {rate}")

    # 5. Configure MCU software downsample ratio
    send_cli_cmd(ser, f"DOWNSAMPLE {downsample}")

    # 6. Configure Device ID / Node name if specified
    if device_id:
        send_cli_cmd(ser, f"ID {device_id}")

    # 7. Configure WiFi parameters
    if ssid:
        send_cli_cmd(ser, f"SSID {ssid}")
    if password:
        send_cli_cmd(ser, f"PASS {password}")
    if target_ip:
        send_cli_cmd(ser, f"TARGET {target_ip}")

    # 8. Set output mode (WIFI only / remote mode)
    send_cli_cmd(ser, f"MODE {output_mode}")

    # 9. Verify current MCU status
    print("\nVerifying Provisioned Settings on MCU...")
    send_cli_cmd(ser, "STATUS", delay_sec=0.5)
    send_cli_cmd(ser, "WIFI STATUS", delay_sec=0.5)

    # 10. Re-enable streaming
    send_cli_cmd(ser, "STREAM ON")

    ser.close()

    print("\n" + "=" * 70)
    print(" [SUCCESS] Provisioning completed! Settings saved to ESP32 NVS Flash.")
    print("           The node is now configured for remote WiFi streaming at 1 Hz.")
    print("=" * 70 + "\n")


def main():
    parser = argparse.ArgumentParser(
        description="Provision ESP32 magnetometer nodes for remote 1 Hz WiFi streaming."
    )
    parser.add_argument("--port", type=str, help="Serial port (e.g. /dev/ttyACM0 or /dev/ttyUSB0)")
    parser.add_argument("--baud", type=int, help="Baud rate (default: 921600)")
    parser.add_argument("--sensor", type=str, choices=["RM3100", "FLC100", "MOCK"], help="Sensor model (RM3100, FLC100, or MOCK)")
    parser.add_argument("--ssid", type=str, help="WiFi SSID network name")
    parser.add_argument("--pass", dest="pass_word", type=str, help="WiFi network WPA2 password")
    parser.add_argument("--target", type=str, help="Target UDP server IP (default: 255.255.255.255 broadcast)")
    parser.add_argument("--mode", type=str, choices=["WIFI", "SERIAL", "BLE", "BOTH"], help="Output stream mode")
    parser.add_argument("--rate", type=str, help="Rate code (e.g., 0x95 for 75Hz RM3100)")
    parser.add_argument("--cycle", type=int, help="RM3100 cycle count (default: 200)")
    parser.add_argument("--downsample", type=int, help="Downsample ratio (75 for RM3100, 1000 for FLC100)")
    parser.add_argument("--id", type=str, help="Custom Device ID / Node name")
    parser.add_argument("--config", type=str, default="provision_config.json", help="Path to config JSON file")

    args = parser.parse_args()
    provision(args)


if __name__ == "__main__":
    main()
