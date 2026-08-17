"""
Hardware Scaling Invariance Test (`test_cycle_count_invariance.py`)

Verifies that the calibrated magnetic field magnitude |B| (in nT) coming from the ESP32
remains invariant when dynamically changing RM3100 cycle counts and sampling rates over Serial CLI.

Usage:
    python3 test_cycle_count_invariance.py --port /dev/ttyACM0
    python3 test_cycle_count_invariance.py --port /dev/ttyUSB0 --baud 921600
"""

import sys
import time
import argparse
import math
import numpy as np

try:
    import serial
except ImportError:
    print("Error: 'pyserial' package is required. Install via: pip install pyserial")
    sys.exit(1)

CYCLE_COUNTS = [50, 100, 200, 300, 400]
RATES = ["0x95", "0x94", "0x93", "0x92"]  # 75Hz, 150Hz, 300Hz, 600Hz

def parse_csv_sample(line: str):
    """
    Parses CSV line: device_id,timestamp_us,x_nT,y_nT,z_nT,status
    Example: NODE_686F80,12345678,23415.20,-4120.80,48910.10,C00000
    """
    parts = line.strip().split(",")
    if len(parts) >= 5:
        try:
            x = float(parts[2])
            y = float(parts[3])
            z = float(parts[4])
            mag = math.sqrt(x*x + y*y + z*z)
            return x, y, z, mag
        except ValueError:
            pass
    return None

def collect_samples(ser, duration_sec=2.0):
    """Collects stream samples for a specified duration."""
    samples = []
    start_t = time.time()
    while time.time() - start_t < duration_sec:
        raw_line = ser.readline().decode("utf-8", errors="ignore")
        if raw_line:
            parsed = parse_csv_sample(raw_line)
            if parsed:
                samples.append(parsed)
    return samples

def run_test(port: str, baud: int):
    print(f"Connecting to ESP32 on {port} at {baud} baud...")
    try:
        ser = serial.Serial(port, baud, timeout=0.5)
    except Exception as e:
        print(f"Failed to open serial port {port}: {e}")
        sys.exit(1)

    # Ensure device is streaming
    ser.write(b"STREAM ON\n")
    time.sleep(0.5)
    ser.reset_input_buffer()

    print("\n" + "="*70)
    print(" 1. TESTING CYCLE COUNT INVARIANCE")
    print("="*70)

    results_cycle = {}

    for cc in CYCLE_COUNTS:
        print(f"Setting Cycle Count to {cc}...")
        ser.write(f"CYCLE {cc}\n".encode("utf-8"))
        time.sleep(0.5)
        ser.reset_input_buffer()

        samples = collect_samples(ser, duration_sec=2.5)
        if not samples:
            print(f"  [WARNING] No valid samples received for CYCLE {cc}")
            continue

        mags = [s[3] for s in samples]
        avg_mag = float(np.mean(mags))
        std_mag = float(np.std(mags))
        avg_x = float(np.mean([s[0] for s in samples]))
        avg_y = float(np.mean([s[1] for s in samples]))
        avg_z = float(np.mean([s[2] for s in samples]))

        results_cycle[cc] = {
            "avg_mag": avg_mag,
            "std_mag": std_mag,
            "avg_x": avg_x,
            "avg_y": avg_y,
            "avg_z": avg_z,
            "count": len(samples)
        }
        print(f"  -> Samples: {len(samples)} | |B|: {avg_mag:.2f} nT (±{std_mag:.2f}) | Bx: {avg_x:.1f}, By: {avg_y:.1f}, Bz: {avg_z:.1f}")

    print("\n" + "="*70)
    print(" 2. TESTING SAMPLING RATE INVARIANCE")
    print("="*70)

    results_rate = {}
    for rate_hex in RATES:
        print(f"Setting Rate to {rate_hex}...")
        ser.write(f"RATE {rate_hex}\n".encode("utf-8"))
        time.sleep(0.5)
        ser.reset_input_buffer()

        samples = collect_samples(ser, duration_sec=2.0)
        if not samples:
            print(f"  [WARNING] No valid samples received for RATE {rate_hex}")
            continue

        mags = [s[3] for s in samples]
        avg_mag = float(np.mean(mags))
        std_mag = float(np.std(mags))

        results_rate[rate_hex] = {
            "avg_mag": avg_mag,
            "std_mag": std_mag,
            "count": len(samples)
        }
        print(f"  -> Samples: {len(samples)} | |B|: {avg_mag:.2f} nT (±{std_mag:.2f})")

    # Restore default cycle count (200) and default rate (0x95)
    ser.write(b"CYCLE 200\n")
    ser.write(b"RATE 0x95\n")
    ser.close()

    # --- SUMMARY & VERIFICATION ---
    print("\n" + "="*70)
    print(" TEST SUMMARY: MAGNITUDE INVARIANCE REPORT")
    print("="*70)
    
    print("\nCycle Count Results:")
    print(f"{'Cycle Count':<15} | {'Samples':<10} | {'|B| Magnitude (nT)':<22} | {'Std Dev (nT)':<15}")
    print("-" * 68)
    
    cycle_mags = []
    for cc, r in results_cycle.items():
        cycle_mags.append(r["avg_mag"])
        print(f"{cc:<15} | {r['count']:<10} | {r['avg_mag']:<22.2f} | {r['std_mag']:<15.2f}")

    if cycle_mags:
        overall_mean = np.mean(cycle_mags)
        max_dev_pct = (np.max(np.abs(cycle_mags - overall_mean)) / overall_mean) * 100.0
        
        print("\n" + "-"*70)
        print(f"Overall Mean Field Magnitude: {overall_mean:.2f} nT")
        print(f"Max Magnitude Deviation across Cycle Counts: {max_dev_pct:.2f}%")
        
        if max_dev_pct <= 3.0:
            print(" [PASS] SUCCESS: Field magnitude remains invariant across cycle count settings!")
        else:
            print(f" [FAIL] WARNING: Field magnitude varied by {max_dev_pct:.2f}% (> 3% threshold). Check scaling formulas!")
        print("="*70 + "\n")

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Test RM3100 magnitude scaling invariance across cycle counts and rates.")
    parser.add_argument("--port", type=str, default="/dev/ttyACM0", help="Serial port (e.g. /dev/ttyACM0 or /dev/ttyUSB0)")
    parser.add_argument("--baud", type=int, default=921600, help="Baud rate (default: 921600)")
    args = parser.parse_args()
    
    run_test(args.port, args.baud)
