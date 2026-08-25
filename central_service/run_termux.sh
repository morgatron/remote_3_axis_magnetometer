#!/usr/bin/env bash
#
# Termux USB Gateway Launcher (`run_termux.sh`)
#
# Usage:
#   ./run_termux.sh        -> Auto-detects USB device (if only 1 connected)
#   ./run_termux.sh 2      -> Selects /dev/bus/usb/001/002
#   ./run_termux.sh 002    -> Selects /dev/bus/usb/001/002
#

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# Prevent Android CPU sleep while gateway is running
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
    # Match exact path, padded number (e.g. 2 -> 002), or suffix
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
        print(f'ERROR: Multiple USB devices found: {devices}. Please specify the number (e.g. ./run_termux.sh 2)', file=sys.stderr)
        sys.exit(1)
")"

echo "[Launcher] Target USB device: $DEV_PATH"
echo "[Launcher] Launching gateway via termux-usb..."

exec termux-usb -r -E -e "python -u gateway.py" "$DEV_PATH"
