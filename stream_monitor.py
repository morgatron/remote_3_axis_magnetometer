import serial
import time
import sys

PORT = '/dev/ttyACM0'
BAUD = 921600

def monitor(duration_sec=120):
    print(f"Opening {PORT} at {BAUD} baud for {duration_sec} seconds...")
    try:
        ser = serial.Serial(PORT, BAUD, timeout=1)
    except Exception as e:
        print(f"Error opening serial port: {e}")
        return

    # Flush input
    ser.reset_input_buffer()

    start_time = time.time()
    total_samples = 0
    anomaly_count = 0

    prev_x, prev_y, prev_z = None, None, None
    prev_ts = None

    print(f"Monitoring stream on {PORT}... Press Ctrl+C or wait {duration_sec}s.\n")

    while (time.time() - start_time) < duration_sec:
        line = ser.readline().decode('ascii', errors='ignore').strip()
        if not line or line.startswith("=") or line.startswith("FIRMWARE") or line.startswith("timestamp") or line.startswith("Sensor") or line.startswith("Initializing") or line.startswith("Auto"):
            continue

        parts = line.split(',')
        if len(parts) != 5:
            anomaly_count += 1
            print(f"[MALFORMED LINE #{total_samples}] raw='{line}'")
            continue

        try:
            ts = int(parts[0])
            x = int(parts[1])
            y = int(parts[2])
            z = int(parts[3])
            status_hex = parts[4]
            status_val = int(status_hex, 16)
        except ValueError as e:
            anomaly_count += 1
            print(f"[PARSE ERROR #{total_samples}] raw='{line}' error={e}")
            continue

        total_samples += 1

        is_anomaly = False
        reasons = []

        # Status check (ADS131E08 upper nibble should be 0xC)
        if (status_val & 0xF00000) != 0xC00000:
            is_anomaly = True
            reasons.append(f"Status=0x{status_hex}")

        # Check all 0s
        if x == 0 and y == 0 and z == 0:
            is_anomaly = True
            reasons.append("All axes 0")

        # Spike detection
        if prev_x is not None:
            dx = abs(x - prev_x)
            dy = abs(y - prev_y)
            dz = abs(z - prev_z)
            # Normal fluctuation is < 50 nT. Flag if any axis jumps by > 300 nT.
            if dx > 300 or dy > 300 or dz > 300:
                is_anomaly = True
                reasons.append(f"SPIKE: dx={dx}, dy={dy}, dz={dz}")

        # Timestamp delta check
        if prev_ts is not None:
            dt = ts - prev_ts
            if dt < 500 or dt > 2000:
                is_anomaly = True
                reasons.append(f"dt={dt}us")

        if is_anomaly:
            anomaly_count += 1
            t_rel = (time.time() - start_time)
            print(f"!!! ANOMALY #{anomaly_count} at sample {total_samples} (rel_t={t_rel:.2f}s): {', '.join(reasons)}")
            print(f"  Current : ts={ts}, x={x}, y={y}, z={z}, status={status_hex}")
            if prev_x is not None:
                print(f"  Previous: ts={prev_ts}, x={prev_x}, y={prev_y}, z={prev_z}")
            print("-" * 60)

        prev_x, prev_y, prev_z = x, y, z
        prev_ts = ts

    ser.close()
    elapsed = time.time() - start_time
    print(f"\nMonitoring complete: {total_samples} samples processed in {elapsed:.2f}s (~{total_samples/elapsed:.1f} Hz).")
    print(f"Total anomalies detected: {anomaly_count}")

if __name__ == '__main__':
    duration = 120
    if len(sys.argv) > 1:
        duration = int(sys.argv[1])
    monitor(duration)
