import serial
import time

ser = serial.Serial('/dev/ttyUSB0', 921600, timeout=1)
ser.reset_input_buffer()
time.sleep(1)

prev_x, prev_y, prev_z = None, None, None
spikes_found = 0
total_samples = 0

print("Monitoring raw stream for single-axis small spikes...")
start = time.time()
while time.time() - start < 10:
    line = ser.readline().decode('ascii', errors='ignore').strip()
    if not line or ',' not in line:
        continue
    parts = line.split(',')
    if len(parts) >= 6:
        try:
            ts = int(parts[1])
            x = int(parts[2])
            y = int(parts[3])
            z = int(parts[4])
        except ValueError:
            continue
            
        total_samples += 1
        if prev_x is not None:
            dx = abs(x - prev_x)
            dy = abs(y - prev_y)
            dz = abs(z - prev_z)
            
            if dx > 40 or dy > 40 or dz > 40:
                spikes_found += 1
                print(f"[SPIKE #{spikes_found}] Sample #{total_samples} | dx={dx}, dy={dy}, dz={dz} | Prev=({prev_x},{prev_y},{prev_z}) -> Curr=({x},{y},{z})")
        
        prev_x, prev_y, prev_z = x, y, z

print(f"\nDone! Processed {total_samples} samples, found {spikes_found} remaining small spikes.")
ser.close()
