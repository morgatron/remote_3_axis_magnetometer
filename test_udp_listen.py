import socket
import time

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
sock.bind(("0.0.0.0", 9876))
sock.settimeout(3.0)

print("Listening for incoming UDP magnetometer packets on port 9876...")
received = 0
start = time.time()

while time.time() - start < 5:
    try:
        data, addr = sock.recvfrom(1024)
        line = data.decode('utf-8', errors='ignore').strip()
        received += 1
        if received <= 5 or received % 20 == 0:
            print(f"[UDP PACKET #{received} from {addr[0]}:{addr[1]}] {line}")
    except socket.timeout:
        print("Timeout waiting for UDP packets.")
        break

print(f"\nDone! Received {received} UDP packets over Wi-Fi.")
sock.close()
