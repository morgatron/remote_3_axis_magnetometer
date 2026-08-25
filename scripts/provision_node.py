#!/usr/bin/env python3
"""
Remote Node Provisioning Script (`provision_node.py`)

Automates hardware provisioning for ESP32-based magnetometer nodes over Serial CLI.

Supported Protocols / Modes:
  - LORA:    SX1262 LoRa 915 MHz long-range batch transmission.
  - BLE:     Bluetooth Low Energy 5.0 LE Coded PHY long-range batch mode.
  - WIFI:    Wi-Fi UDP unicast / broadcast streaming.
  - BOTH:    USB Serial CDC + BLE Coded PHY concurrent streaming.
  - SERIAL:  USB Serial CDC local streaming only.

Supported Sensors:
  - RM3100:  High-resolution digital PNI magneto-inductive sensor.
  - FLC100:  Analog fluxgate magnetometer sampled via 24-bit TI ADS131E08 ADC.
  - MOCK:    Synthetic geomagnetic simulation sensor for RF range testing.

Usage:
  python3 provision_node.py -i                                   # Interactive wizard
  python3 provision_node.py --mode LORA --sensor MOCK --batch 10  # LoRa range test node
  python3 provision_node.py --mode BLE --sensor RM3100 --batch 10 # BLE battery field node
  python3 provision_node.py --sensor FLC100 --mode WIFI --ssid "FieldNet" --pass "secret"
"""

import sys
import os
import time
import json
import argparse

try:
    import serial
    import serial.tools.list_ports
except ImportError:
    print("Error: 'pyserial' package is required. Install via: pip install pyserial")
    sys.exit(1)


def find_serial_ports():
    """Discovers available serial ports for ESP32 devices."""
    ports = sorted([p.device for p in serial.tools.list_ports.comports() if 'ACM' in p.device or 'USB' in p.device])
    return ports


def load_wifi_cfg_fallback():
    """Parses legacy wifi.cfg file if available in root or scripts directory."""
    paths = [
        os.path.join(os.path.dirname(__file__), "..", "wifi.cfg"),
        os.path.join(os.path.dirname(__file__), "wifi.cfg")
    ]
    ssid, pwd = "", ""
    for wifi_cfg_path in paths:
        if os.path.exists(wifi_cfg_path):
            try:
                with open(wifi_cfg_path, "r", encoding="utf-8") as f:
                    for line in f:
                        line = line.strip()
                        if line.startswith("SSID:"):
                            ssid = line.split(":", 1)[1].strip()
                        elif line.startswith("PWD:"):
                            pwd = line.split(":", 1)[1].strip()
                if ssid:
                    break
            except Exception:
                pass
    return ssid, pwd


def load_config(config_path="provision_config.json", user_specified=False):
    """Loads configuration JSON file, searching relative path, root directory, or scripts dir."""
    defaults = {
        "port": "/dev/ttyACM0",
        "baud": 921600,
        "sensor": "RM3100",
        "device_id": "",
        "output_mode": "BOTH",
        "batch_size": 10,
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
            "gain": 1,
            "vref": 2.4,
            "downsample": 1000
        }
    }

    resolved_path = config_path
    if not os.path.exists(resolved_path):
        root_path = os.path.join(os.path.dirname(__file__), "..", config_path)
        if os.path.exists(root_path):
            resolved_path = root_path

    if user_specified and not os.path.exists(resolved_path):
        print(f"[ERROR] Specified configuration file '{config_path}' was not found!")
        sys.exit(1)

    if os.path.exists(resolved_path):
        try:
            with open(resolved_path, "r", encoding="utf-8") as f:
                data = json.load(f)
                for key in ["port", "baud", "sensor", "device_id", "output_mode", "batch_size"]:
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
            if user_specified:
                sys.exit(1)

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


def interactive_wizard():
    """Guides user through interactive configuration."""
    print("\n" + "=" * 70)
    print("      MAGNETOMETER REMOTE NODE PROVISIONING WIZARD")
    print("=" * 70 + "\n")

    available_ports = find_serial_ports()
    if available_ports:
        print("Available Serial Ports:")
        for idx, p in enumerate(available_ports, 1):
            print(f"  [{idx}] {p}")
        p_choice = input(f"Select serial port [1-{len(available_ports)}, default 1]: ").strip()
        if p_choice.isdigit() and 1 <= int(p_choice) <= len(available_ports):
            port = available_ports[int(p_choice) - 1]
        else:
            port = available_ports[0]
    else:
        port = input("Enter Serial Port [/dev/ttyACM0]: ").strip() or "/dev/ttyACM0"

    print("\nSensor Model:")
    print("  [1] RM3100 (PNI SPI Magnetometer - Digital)")
    print("  [2] FLC100 (Analog Fluxgate via ADS131E08 ADC)")
    print("  [3] MOCK   (Synthetic Simulation - Range Testing)")
    s_choice = input("Select sensor [1-3, default 1]: ").strip()
    sensor_map = {"1": "RM3100", "2": "FLC100", "3": "MOCK"}
    sensor = sensor_map.get(s_choice, "RM3100")

    print("\nTransmission / Output Mode:")
    print("  [1] LORA   - SX1262 915 MHz LoRa (Long-Range Field Batches)")
    print("  [2] BLE    - BLE 5.0 LE Coded PHY (Long-Range Field Batches)")
    print("  [3] BOTH   - USB Serial CDC + BLE Coded PHY (Default)")
    print("  [4] WIFI   - 2.4 GHz Wi-Fi UDP Streaming (Lab / Remote)")
    print("  [5] SERIAL - USB Serial CDC Only (High-Speed Continuous)")
    m_choice = input("Select output mode [1-5, default 3]: ").strip()
    mode_map = {"1": "LORA", "2": "BLE", "3": "BOTH", "4": "WIFI", "5": "SERIAL"}
    mode = mode_map.get(m_choice, "BOTH")

    batch_size = 10
    if mode in ["LORA", "BLE", "BOTH"]:
        b_input = input("\nBatch burst size (samples per burst) [1-10, default 10]: ").strip()
        if b_input.isdigit() and 1 <= int(b_input) <= 10:
            batch_size = int(b_input)

    dev_id = input("\nCustom Device ID / Node name [press Enter for auto MAC]: ").strip()

    ssid, password, target_ip = "", "", "255.255.255.255"
    if mode in ["WIFI", "BOTH"]:
        default_ssid, default_pass = load_wifi_cfg_fallback()
        ssid = input(f"WiFi SSID [{default_ssid}]: ").strip() or default_ssid
        password = input(f"WiFi Password: ").strip() or default_pass
        target_ip = input(f"Target UDP Server IP [255.255.255.255]: ").strip() or "255.255.255.255"

    class Args:
        pass
    args = Args()
    args.config = "provision_config.json"
    args.port = port
    args.baud = 921600
    args.sensor = sensor
    args.mode = mode
    args.batch = batch_size
    args.id = dev_id
    args.ssid = ssid
    args.pass_word = password
    args.target = target_ip
    args.rate = None
    args.cycle = None
    args.downsample = None
    args.gain = None
    args.vref = None
    args.test = False

    return args


def provision(args):
    user_specified = hasattr(args, "config") and ((args.config != "provision_config.json") or ("--config" in sys.argv) or ("-c" in sys.argv))
    cfg = load_config(getattr(args, "config", "provision_config.json"), user_specified=user_specified)

    # Resolve port (auto-discover if missing or invalid)
    port = getattr(args, "port", None) or cfg["port"]
    if not os.path.exists(port):
        available = find_serial_ports()
        if available:
            port = available[0]
            print(f"[PORT NOTICE] Port '{cfg.get('port')}' not found. Auto-selected active port: {port}")

    baud = getattr(args, "baud", None) or cfg["baud"]
    sensor_type = (getattr(args, "sensor", None) or cfg["sensor"]).upper()
    device_id = getattr(args, "id", None) if getattr(args, "id", None) is not None else cfg["device_id"]
    output_mode = (getattr(args, "mode", None) or cfg["output_mode"]).upper()
    batch_size = getattr(args, "batch", None) if getattr(args, "batch", None) is not None else cfg.get("batch_size", 10)

    ssid = getattr(args, "ssid", None) if getattr(args, "ssid", None) is not None else cfg["wifi"]["ssid"]
    password = getattr(args, "pass_word", None) if getattr(args, "pass_word", None) is not None else cfg["wifi"]["password"]
    target_ip = getattr(args, "target", None) if getattr(args, "target", None) is not None else cfg["wifi"]["target_ip"]

    # Sensor Profiles
    if sensor_type in ["RM3100", "MOCK"]:
        rate = getattr(args, "rate", None) or cfg["rm3100"]["rate"]
        cycle_count = getattr(args, "cycle", None) if getattr(args, "cycle", None) is not None else cfg["rm3100"]["cycle_count"]
        downsample = getattr(args, "downsample", None) if getattr(args, "downsample", None) is not None else cfg["rm3100"]["downsample"]
        gain = None
        vref = None
    else:  # FLC100
        rate = getattr(args, "rate", None) or cfg["flc100"]["rate"]
        cycle_count = None
        gain = getattr(args, "gain", None) if getattr(args, "gain", None) is not None else cfg["flc100"].get("gain", 1)
        vref = getattr(args, "vref", None) if getattr(args, "vref", None) is not None else cfg["flc100"].get("vref", 2.4)
        downsample = getattr(args, "downsample", None) if getattr(args, "downsample", None) is not None else cfg["flc100"]["downsample"]

    print("\n" + "=" * 70)
    print("             REMOTE NODE PROVISIONING SUMMARY")
    print("=" * 70)
    print(f"  Target Port:       {port} @ {baud} baud")
    print(f"  Sensor Model:      {sensor_type}")
    if sensor_type == "RM3100":
        print(f"  Hardware Rate:     {rate} (75 Hz ODR)")
        print(f"  Cycle Count:       {cycle_count} (Max safe for 75 Hz)")
        print(f"  Downsample Ratio:  {downsample}x -> Effective Stream Rate: {75 // downsample} Hz")
    elif sensor_type == "FLC100":
        print(f"  Hardware Rate:     {rate} (1 kSPS ADC)")
        print(f"  PGA Gain:          {gain}x | VREF: {vref} V")
        print(f"  Downsample Ratio:  {downsample}x -> Effective Stream Rate: {1000 // downsample} Hz")
    else:
        print(f"  Hardware Rate:     {rate} (Synthetic 75 Hz generator)")

    print(f"  Output Mode:       {output_mode}")
    if output_mode in ["LORA", "BLE", "BOTH"]:
        print(f"  Batch Burst Size:  {batch_size} samples / burst (1 burst every {batch_size}s)")
    if output_mode in ["WIFI", "BOTH"] and ssid:
        print(f"  WiFi SSID:         '{ssid}'")
        print(f"  Target UDP Server: {target_ip}")
    if device_id:
        print(f"  Custom Device ID:  {device_id}")
    print("=" * 70 + "\n")

    print(f"Connecting to ESP32 on {port}...")
    try:
        ser = serial.Serial(port, baud, timeout=0.5)
    except Exception as e:
        print(f"[ERROR] Failed to open serial port {port}: {e}")
        sys.exit(1)

    time.sleep(1.0)
    ser.reset_input_buffer()

    print("Executing CLI Provisioning Sequence...")

    # 1. Stop continuous streaming while provisioning
    send_cli_cmd(ser, "STREAM OFF")

    # 2. Select sensor model
    send_cli_cmd(ser, f"SENSOR {sensor_type}", delay_sec=0.8)

    # 3. Sensor specific setup
    if sensor_type == "RM3100" and cycle_count is not None:
        send_cli_cmd(ser, f"CYCLE {cycle_count}")
    elif sensor_type == "FLC100":
        if gain is not None:
            send_cli_cmd(ser, f"GAIN {gain}")
        if vref is not None:
            send_cli_cmd(ser, f"VREF {vref}")
        if getattr(args, "test", False):
            send_cli_cmd(ser, "TEST ON")

    # 4. Hardware ODR and software downsample
    if rate:
        send_cli_cmd(ser, f"RATE {rate}")
    if downsample:
        send_cli_cmd(ser, f"DOWNSAMPLE {downsample}")

    # 5. Device ID
    if device_id:
        send_cli_cmd(ser, f"ID {device_id}")

    # 6. Batch Burst Size
    if batch_size is not None:
        send_cli_cmd(ser, f"BATCH {batch_size}")

    # 7. WiFi Configuration (if relevant)
    if output_mode in ["WIFI", "BOTH"]:
        if ssid:
            send_cli_cmd(ser, f"SSID {ssid}")
        if password:
            send_cli_cmd(ser, f"PASS {password}")
        if target_ip:
            send_cli_cmd(ser, f"TARGET {target_ip}")

    # 8. Output Mode
    send_cli_cmd(ser, f"MODE {output_mode}")

    # 9. Verify current MCU status
    print("\nVerifying Provisioned Settings on MCU...")
    send_cli_cmd(ser, "STATUS", delay_sec=0.5)
    if output_mode in ["WIFI", "BOTH"] and ssid:
        send_cli_cmd(ser, "WIFI STATUS", delay_sec=0.5)

    # 10. Re-enable streaming
    send_cli_cmd(ser, "STREAM ON")

    ser.close()

    print("\n" + "=" * 70)
    print(" [SUCCESS] Provisioning completed! Settings saved to ESP32 NVS Flash.")
    print(f"           Mode: {output_mode} | Sensor: {sensor_type} | Batch: {batch_size}s")
    print("=" * 70 + "\n")


def main():
    if "-i" in sys.argv or "--interactive" in sys.argv:
        args = interactive_wizard()
        provision(args)
        return

    parser = argparse.ArgumentParser(
        description="Provision ESP32 magnetometer nodes for remote field deployment."
    )
    parser.add_argument("-i", "--interactive", action="store_true", help="Launch interactive provisioning wizard")
    parser.add_argument("-d", "--default-config", action="store_true", help="Use default configuration file (provision_config.json)")
    parser.add_argument("-c", "--config", type=str, default="provision_config.json", help="Path to JSON configuration file (default: provision_config.json)")
    parser.add_argument("--port", type=str, help="Serial port (e.g. /dev/ttyACM0 or /dev/ttyUSB0)")
    parser.add_argument("--baud", type=int, default=921600, help="Baud rate (default: 921600)")
    parser.add_argument("--sensor", type=str, choices=["RM3100", "FLC100", "MOCK"], help="Sensor model (RM3100, FLC100, or MOCK)")
    parser.add_argument("--mode", type=str, choices=["LORA", "BLE", "WIFI", "BOTH", "SERIAL"], help="Output stream mode")
    parser.add_argument("--batch", type=int, default=10, help="Batch burst size (1-10 samples per burst, default: 10)")
    parser.add_argument("--id", type=str, help="Custom Device ID / Node name")
    parser.add_argument("--ssid", type=str, help="WiFi SSID network name")
    parser.add_argument("--pass", dest="pass_word", type=str, help="WiFi network WPA2 password")
    parser.add_argument("--target", type=str, help="Target UDP server IP (default: 255.255.255.255 broadcast)")
    parser.add_argument("--rate", type=str, help="Rate code (e.g., 0x95 for 75Hz RM3100, 0x06 for 1kSPS FLC100)")
    parser.add_argument("--cycle", type=int, help="RM3100 cycle count (default: 200)")
    parser.add_argument("--gain", type=int, choices=[1, 2, 4, 8, 12], help="FLC100-ADS131 PGA Gain (1, 2, 4, 8, 12)")
    parser.add_argument("--vref", type=float, help="FLC100-ADS131 VREF voltage (default: 2.4V)")
    parser.add_argument("--test", action="store_true", help="Enable ADS131 internal 1 Hz test signal")
    parser.add_argument("--downsample", type=int, help="Downsample ratio (75 for RM3100, 1000 for FLC100)")

    args = parser.parse_args()
    provision(args)


if __name__ == "__main__":
    main()
