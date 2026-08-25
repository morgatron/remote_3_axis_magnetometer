#!/usr/bin/env bash
#
# Termux USB Hardware Probe Launcher (`scripts/run_termux_probe.sh`)
#
# Usage:
#   ./scripts/run_termux_probe.sh        -> Auto-detects USB device (if only 1 connected)
#   ./scripts/run_termux_probe.sh 2      -> Selects /dev/bus/usb/001/002
#   ./scripts/run_termux_probe.sh 002    -> Selects /dev/bus/usb/001/002
#

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Prevent Android CPU sleep while monitor is running
if command -v termux-wake-lock >/dev/null 2>&1; then
    termux-wake-lock
fi

# Ensure termux-usb is available
if ! command -v termux-usb >/dev/null 2>&1; then
    echo "[Error] 'termux-usb' not found. Please run: pkg install termux-api"
    exit 1
fi

ARG="$1"

# Query connected USB devices from Termux API
DEV_JSON="$(termux-usb -l 2>/dev/null || echo '[]')"

# Resolve the target device path via Python helper
DEV_PATH="$(python3 -c "
import sys, json

raw = '''$DEV_JSON'''
arg = '$ARG'.strip()

try:
    devices = json.loads(raw)
except Exception:
    devices = []

if not devices:
    print('ERROR: No USB devices detected. Make sure the ESP32 is plugged in with an OTG adapter.', file=sys.stderr)
    sys.exit(1)

if arg:
    padded = f'{int(arg):03d}' if arg.isdigit() else arg
    matched = [d for d in devices if d.endswith(f'/{padded}') or d.endswith(f'/{arg}') or arg in d]
    if not matched:
        print(f'ERROR: Device \"{arg}\" not found. Available devices: {devices}', file=sys.stderr)
        sys.exit(1)
    print(matched[0])
else:
    if len(devices) == 1:
        print(devices[0])
    else:
        print(f'ERROR: Multiple USB devices found: {devices}. Please specify the number (e.g. ./run_termux_probe.sh 2)', file=sys.stderr)
        sys.exit(1)
")"

echo "[Probe Launcher] Target USB device: $DEV_PATH"
echo "[Probe Launcher] Launching hardware diagnostic probe via termux-usb..."

chmod +x "$SCRIPT_DIR/termux_usb_probe.py"
exec termux-usb -r -e "$SCRIPT_DIR/termux_usb_probe.py" "$DEV_PATH"
