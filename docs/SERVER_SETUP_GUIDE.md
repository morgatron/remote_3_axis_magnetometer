# Central Server Deployment & Hardware Setup Guide

This guide provides instructions for deploying the **Magnetometer Central Data Server & Gateway** on low-cost residential hardware, specifically:
- **Raspberry Pi 4** (Raspberry Pi OS / Debian 64-bit / 32-bit)
- **Old Laptop / Mini-PC** (Ubuntu, Debian, Linux Mint, WSL2, or macOS)

---

## 1. Quick Start: One-Command Automated Setup

On your target machine (Raspberry Pi or Laptop), navigate to the `central_service` directory and run the installer:

```bash
cd central_service
sudo ./install.sh
```

### What `install.sh` Does Automatically:
1. **Installs System Packages**: Installs `python3`, `python3-venv`, `python3-pip`, `sqlite3`, `curl`, and network diagnostics.
2. **Creates Python Environment**: Creates a dedicated `.venv` and installs required packages (`fastapi`, `uvicorn`, `pydantic`, `numpy`, `pandas`, `requests`, `pyarrow`, `pyserial`).
3. **Generates `.env`**: Sets up default configuration for server port (8000), UDP port (9876), database path, and optional `API_KEY`.
4. **Installs Systemd Services**:
   - `magnetometer-server.service` (central server & web GUI)
   - `magnetometer-gateway.service` (UDP / Serial sensor stream forwarder)
   - Configures automatic restart on crashes and auto-start on boot.
5. **Applies Hardware Optimizations**:
   - **Laptops**: Disables sleep on lid close and disables Wi-Fi power-saving sleep.
   - **Raspberry Pi**: Ensures SQLite WAL mode is active for safe, low-wear SD card operation.
6. **Displays Access URLs**: Prints local IP addresses and dashboard links (`http://192.168.x.x:8000`).

---

## 2. Daily Operations & Management (`manage.sh`)

Use `./manage.sh` to control, monitor, and maintain the server without remembering systemd commands:

```bash
# Check server health, memory, database size, total records, and active nodes
./manage.sh status

# View live colorized logs from server and gateway
./manage.sh logs

# Restart or stop services
./manage.sh restart
./manage.sh stop
./manage.sh start

# Create an instant, safe SQLite online backup (stored in backups/)
./manage.sh backup

# Quick CLI telemetry export to CSV, Parquet, or NumPy NPZ
./manage.sh export csv
./manage.sh export parquet

# Send a synthetic self-test sample to verify the ingestion pipeline
./manage.sh test
```

---

## 3. Hardware Target 1: Old Laptop

An old laptop makes a server because it has a **built-in uninterruptible power supply (battery)**, screen/keyboard for recovery, and low idle power consumption (10–15 W).

### Key Laptop Optimizations:

#### A. Keep Running When Lid is Closed
By default, Linux suspends when the lid closes. `install.sh` sets this automatically, but you can also configure it manually:
1. Edit `/etc/systemd/logind.conf`:
   ```ini
   HandleLidSwitch=ignore
   HandleLidSwitchExternalPower=ignore
   HandleLidSwitchDocked=ignore
   ```
2. Apply changes:
   ```bash
   sudo systemctl restart systemd-logind.service
   ```

#### B. Disable Wi-Fi Power Saving (Low Latency UDP)
Wi-Fi cards often enter power-saving sleep after seconds of idle time, causing latency spikes or dropped UDP telemetry packets.
```bash
# Identify your wireless interface (e.g., wlan0, wlp2s0)
ip link

# Disable power saving
sudo iw dev wlan0 set power_save off
```
To make this persistent across reboots, add the command to `/etc/rc.local` or NetworkManager config.

#### C. Dim or Turn Off Laptop Screen
To conserve power and reduce heat with the lid open or closed:
```bash
# Turn off backlight on Linux console
sudo setterm --blank 1 --powerdown 1
```

---

## 4. Hardware Target 2: Raspberry Pi 4

The Raspberry Pi 4 (2 GB, 4 GB, or 8 GB) is a low-power (3–5 W) headless server platform.

### Key Raspberry Pi Optimizations:

#### A. Headless Initial Setup (Raspberry Pi Imager)
1. In **Raspberry Pi Imager**, select **Raspberry Pi OS (64-bit Lite)**.
2. Click the gear icon (Advanced Options) before writing to MicroSD:
   - Set hostname (e.g. `magnetometer-pi`).
   - Enable SSH (public-key or password).
   - Pre-configure your 2.4 GHz / 5 GHz Wi-Fi SSID & Password.
3. Power on the Pi. It connects to Wi-Fi automatically.
4. SSH into the Pi: `ssh <user>@magnetometer-pi.local`.

#### B. Protecting the MicroSD Card from Wear
1. **SQLite WAL Mode (Default)**: The central server runs SQLite with `PRAGMA journal_mode=WAL;` and batch commits, minimizing random write cycles.
2. **Periodic Backups**: Run `./manage.sh backup` via a nightly cron job:
   ```cron
   0 3 * * * /home/pi/remote_3_axis_magnetometer/central_service/manage.sh backup > /dev/null 2>&1
   ```
3. **Optional Log2Ram**: Install `log2ram` to write system logs to RAM instead of the SD card:
   ```bash
   echo "deb [signed-by=/usr/share/keyrings/azlux-archive-keyring.gpg] http://packages.azlux.fr/debian/ bookworm main" | sudo tee /etc/apt/sources.list.d/azlux.list
   sudo apt-get update && sudo apt-get install log2ram
   ```
4. **USB Boot (Recommended for Long-Term Deployments)**: Booting the Pi 4 directly from a cheap USB 3.0 SSD / NVMe enclosure eliminates SD card corruption risk entirely.

#### C. Static Local IP (DHCP Reservation)
To ensure the Pi always has the same IP on your home router:
- Log in to your home router (e.g., `http://192.168.1.1`).
- Go to **DHCP Settings / Address Reservation**.
- Bind the MAC address of the Raspberry Pi to a static IP (e.g., `192.168.1.100`).

---

## 5. Alternative Deployment: Docker Compose

If you prefer containerized deployment:

```bash
cd central_service

# Start central server in background
docker compose up -d

# Check status and logs
docker compose ps
docker compose logs -f

# Start both server and UDP edge gateway (host network mode)
docker compose --profile all up -d
```

Persistent SQLite database data is stored in the local `./data` directory on the host.

---

## 6. Accessing the Server Behind a Residential NAT

Residential ISPs place home connections behind NAT or Carrier-Grade NAT (CGNAT). To view your live dashboard or stream data from remote field sensor nodes:

👉 Run the interactive setup tool:
```bash
cd central_service
./remote_access.sh
```

Select **Option 1 (Cloudflare Tunnels)** to set up a zero-config outbound tunnel. This creates a secure `https://mag.yourdomain.com` endpoint with free SSL that allows any remote field node or web browser to connect **with zero client-side VPN software required**.

See the complete [**Residential NAT & Remote Access Guide**](NAT_AND_REMOTE_ACCESS.md) and the [**Secrets Management & System Security Guide**](SECRETS_AND_SECURITY.md) for full step-by-step setup instructions.
