#!/usr/bin/env python3
"""
Gateway Receiver Provisioning Script (`provision_receiver.py`)

Automates hardware provisioning for ESP32 Multi-Protocol Receiver Nodes over Serial CLI.

Configuration Options:
  - Egress Forwarding Mode: SERIAL (USB CDC), WIFI (HTTP/UDP Central), or BOTH
  - External WiFi Router SSID & WPA2 Password
  - Target Central Server IP and UDP Port
  - ESP-NOW WiFi Radio Channel (1-13)

Configuration Priority:
  1. Command line arguments (--port, --mode, --ssid, --target, --channel)
  2. JSON configuration file (specified by -c / --config or provision_receiver_config.json)
  3. Legacy wifi.cfg (SSID / Password fallback)

Usage:
  python3 provision_receiver.py --port /dev/ttyACM0 --mode SERIAL
  python3 provision_receiver.py -c receiver_config.json --port /dev/ttyUSB0
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


def load_config(config_path="provision_receiver_config.json", user_specified=False):
    """Loads receiver configuration JSON file, searching relative path, root directory, or scripts dir."""
    defaults = {
        "port": "/dev/ttyACM0",
        "baud": 921600,
        "output_mode": "SERIAL",
        "espnow_channel": 1,
        "wifi": {
            "ssid": "",
            "password": "",
            "target_ip": "255.255.255.255",
            "target_port": 9876
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
                for key in ["port", "baud", "output_mode", "espnow_channel"]:
                    if key in data:
                        defaults[key] = data[key]
                if "wifi" in data and isinstance(data["wifi"], dict):
                    defaults["wifi"].update(data["wifi"])
        except Exception as e:
            print(f"[CONFIG WARNING] Could not read {config_path}: {e}")
            if user_specified:
                sys.exit(1)

    # Fallback to wifi.cfg if SSID is still empty
    if not defaults["wifi"]["ssid"]:
        fb_ssid, fb_pass = load_wifi_cfg_fallback()
        if fb_ssid:
            defaults["wifi"]["ssid"] = fb_ssid
            defaults["wifi"]["password"] = fb_pass

    return defaults


def send_cli_cmd(ser, cmd: str, delay_sec=0.4):
    """Sends a CLI command to the ESP32 receiver and prints returned output lines."""
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
    user_specified = (args.config != "provision_receiver_config.json") or ("--config" in sys.argv) or ("-c" in sys.argv)
    cfg = load_config(args.config, user_specified=user_specified)

    # Command line argument overrides
    port = args.port or cfg["port"]
    baud = args.baud or cfg["baud"]
    output_mode = (args.mode or cfg["output_mode"]).upper()
    channel = args.channel if args.channel is not None else cfg["espnow_channel"]

    ssid = args.ssid if args.ssid is not None else cfg["wifi"]["ssid"]
    password = args.pass_word if args.pass_word is not None else cfg["wifi"]["password"]
    target_ip = args.target if args.target is not None else cfg["wifi"]["target_ip"]
    target_port = args.target_port if args.target_port is not None else cfg["wifi"]["target_port"]

    print("\n" + "=" * 70)
    print(" GATEWAY RECEIVER PROVISIONING SUMMARY")
    print("=" * 70)
    print(f"  Config Source:   {args.config}" + (" (custom file)" if user_specified else " (default)"))
    print(f"  Target Port:     {port} @ {baud} baud")
    print(f"  Egress Mode:     {output_mode}")
    print(f"  ESP-NOW Channel: {channel}")
    print(f"  WiFi SSID:       '{ssid}'")
    print(f"  Target IP/Port:  {target_ip}:{target_port}")
    print("=" * 70 + "\n")

    print(f"Connecting to ESP32 Receiver on {port}...")
    try:
        ser = serial.Serial(port, baud, timeout=0.5)
    except Exception as e:
        print(f"[ERROR] Failed to open serial port {port}: {e}")
        sys.exit(1)

    time.sleep(1.0)
    ser.reset_input_buffer()

    print("\nExecuting Receiver CLI Provisioning Sequence...")

    # 1. Set egress mode
    send_cli_cmd(ser, f"MODE {output_mode}")

    # 2. Configure ESP-NOW channel
    send_cli_cmd(ser, f"CHANNEL {channel}")

    # 3. Configure target central server IP and UDP port
    if target_ip:
        send_cli_cmd(ser, f"TARGET {target_ip} {target_port}")

    # 4. Configure WiFi credentials if provided (Note: WiFi command restarts MCU on change)
    if ssid:
        if password:
            send_cli_cmd(ser, f"WIFI \"{ssid}\" {password}", delay_sec=1.5)
        else:
            send_cli_cmd(ser, f"WIFI \"{ssid}\"", delay_sec=1.5)
    else:
        # Save explicitly if WiFi credentials were not updated
        send_cli_cmd(ser, "SAVE")

    # 5. Verify status
    print("\nVerifying Receiver Status...")
    time.sleep(0.5)
    send_cli_cmd(ser, "STATUS", delay_sec=0.5)

    ser.close()

    print("\n" + "=" * 70)
    print(" [SUCCESS] Gateway Receiver provisioning completed!")
    print("           Settings saved to ESP32 NVS Flash.")
    print("=" * 70 + "\n")


def main():
    if len(sys.argv) == 1:
        print("\n[NOTICE] No arguments provided. Printing available CLI options below:\n")
        parser = argparse.ArgumentParser(
            description="Provision ESP32 Multi-Protocol Receiver Nodes over Serial CLI."
        )
        parser.add_argument("-d", "--default-config", action="store_true", help="Use default configuration file (provision_receiver_config.json)")
        parser.add_argument("-c", "--config", type=str, default="provision_receiver_config.json", help="Path to receiver JSON config file (default: provision_receiver_config.json)")
        parser.add_argument("--port", type=str, help="Serial port (e.g. /dev/ttyACM0 or /dev/ttyUSB0)")
        parser.add_argument("--baud", type=int, help="Baud rate (default: 921600)")
        parser.add_argument("--mode", type=str, choices=["SERIAL", "WIFI", "BOTH"], help="Egress mode (SERIAL, WIFI, or BOTH)")
        parser.add_argument("--channel", type=int, help="ESP-NOW radio channel (1-13)")
        parser.add_argument("--ssid", type=str, help="External WiFi router SSID")
        parser.add_argument("--pass", dest="pass_word", type=str, help="External WiFi WPA2 password")
        parser.add_argument("--target", type=str, help="Target server IP for WiFi egress")
        parser.add_argument("--target-port", type=int, help="Target server UDP port for WiFi egress (default: 9876)")
        parser.print_help()
        sys.exit(0)

    parser = argparse.ArgumentParser(
        description="Provision ESP32 Multi-Protocol Receiver Nodes over Serial CLI."
    )
    parser.add_argument("-d", "--default-config", action="store_true", help="Use default configuration file (provision_receiver_config.json)")
    parser.add_argument("-c", "--config", type=str, default="provision_receiver_config.json", help="Path to receiver JSON config file (default: provision_receiver_config.json)")
    parser.add_argument("--port", type=str, help="Serial port (e.g. /dev/ttyACM0 or /dev/ttyUSB0)")
    parser.add_argument("--baud", type=int, help="Baud rate (default: 921600)")
    parser.add_argument("--mode", type=str, choices=["SERIAL", "WIFI", "BOTH"], help="Egress mode (SERIAL, WIFI, or BOTH)")
    parser.add_argument("--channel", type=int, help="ESP-NOW radio channel (1-13)")
    parser.add_argument("--ssid", type=str, help="External WiFi router SSID")
    parser.add_argument("--pass", dest="pass_word", type=str, help="External WiFi WPA2 password")
    parser.add_argument("--target", type=str, help="Target server IP for WiFi egress")
    parser.add_argument("--target-port", type=int, help="Target server UDP port for WiFi egress (default: 9876)")

    args = parser.parse_args()
    provision(args)


if __name__ == "__main__":
    main()
