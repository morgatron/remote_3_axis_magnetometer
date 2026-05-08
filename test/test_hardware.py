import time
import serial
import pytest
import serial.tools.list_ports

# Configuration: Update this or pass via command line if needed
# For now, we'll try to auto-detect a ttyUSB or ttyACM port
def find_esp32_port():
    ports = serial.tools.list_ports.comports()
    for p in ports:
        if any(pattern in p.device for pattern in ["ttyUSB", "ttyACM", "COM"]):
            return p.device
    return None

PORT = find_esp32_port()
BAUD = 115200

@pytest.fixture(scope="module")
def ser():
    if not PORT:
        pytest.skip("No suitable serial port found for hardware test.")
    
    conn = serial.Serial(PORT, BAUD, timeout=1.0)
    time.sleep(2)  # Wait for ESP32 to reboot after connection
    conn.reset_input_buffer()
    yield conn
    conn.close()

def test_connection(ser):
    """Verify that we can open the serial port."""
    assert ser.is_open

def test_help_command(ser):
    """Verify that the HELP command returns a menu."""
    ser.write(b"HELP\n")
    response = ser.read_until(b"---").decode('utf-8', errors='ignore')
    # Read a bit more to get the full help
    response += ser.read(500).decode('utf-8', errors='ignore')
    assert "CLI Help" in response
    assert "STREAM" in response
    assert "STATUS" in response

def test_status_command(ser):
    """Verify that the STATUS command returns sensor info."""
    ser.write(b"STATUS\n")
    time.sleep(0.1)
    response = ser.read(1000).decode('utf-8', errors='ignore')
    assert "Sensor:" in response
    assert "Streaming:" in response

def test_streaming_toggle(ser):
    """Verify that we can start and stop the data stream."""
    # Ensure stream is off first
    ser.write(b"STREAM OFF\n")
    time.sleep(0.5)
    ser.reset_input_buffer()
    
    # Check for 1 second that no data arrives
    time.sleep(1.0)
    assert ser.in_waiting == 0, "Received data while stream was supposed to be OFF"
    
    # Turn stream ON
    ser.write(b"STREAM ON\n")
    time.sleep(0.5)
    assert ser.in_waiting > 0, "No data received after STREAM ON"

def test_data_format(ser):
    """Verify that the streaming data is in the correct CSV format."""
    ser.write(b"STREAM ON\n")
    time.sleep(0.5)
    ser.readline() # Discard potentially partial line
    
    # Check 5 consecutive lines
    for _ in range(5):
        line = ser.readline().decode('utf-8').strip()
        parts = line.split(',')
        assert len(parts) == 4, f"Invalid CSV format: {line}"
        
        ts, x, y, z = parts
        assert ts.isdigit(), f"Invalid timestamp: {ts}"
        # Values can be negative, so we check if they can be converted to int
        try:
            int(x); int(y); int(z)
        except ValueError:
            pytest.fail(f"Invalid magnetic values: {x}, {y}, {z}")

if __name__ == "__main__":
    if not PORT:
        print("Error: No serial port found. Connect your ESP32.")
    else:
        print(f"Starting hardware tests on {PORT}...")
        pytest.main([__file__])
