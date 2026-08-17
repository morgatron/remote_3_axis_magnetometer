# Magnetometer Central Service

A lightweight, future-proof time-series ingestion server, WebSockets live streaming hub, and real-time Web GUI dashboard for distributed 3-axis magnetometer sensor networks.

Designed to run seamlessly on **Raspberry Pi 4**, **Old Laptops (Linux/Ubuntu)**, or **Docker** behind residential home Wi-Fi networks (NAT/CGNAT).

---

## ⚡ Quick Start (1-Line Installer)

On your target machine (Raspberry Pi or Laptop):

```bash
cd central_service
sudo ./install.sh
```

The installer will:
1. Install Python packages & SQLite.
2. Configure `.env` settings.
3. Install and start `magnetometer-server` and `magnetometer-gateway` as auto-restarting **systemd background services**.
4. Apply hardware tweaks (disables laptop lid-close sleep and Wi-Fi power savings).
5. Print your local LAN access URLs (`http://192.168.x.x:8000`).

---

## 🛠️ Management CLI (`./manage.sh`)

Manage the server with intuitive commands:

```bash
./manage.sh status     # Show health, memory, database file size, and record count
./manage.sh logs       # Live stream colorized logs
./manage.sh restart    # Restart services
./manage.sh stop       # Stop services
./manage.sh backup     # Safe online SQLite backup into backups/
./manage.sh export csv # Quick CLI telemetry export to CSV (or parquet, npz)
./manage.sh test       # Inject synthetic test reading to verify the pipeline
```

---

## 🌐 Remote Access & Residential NAT / CGNAT (`./remote_access.sh`)

If your server sits behind a home Wi-Fi router with dynamic IPs or Carrier-Grade NAT (CGNAT), run the interactive assistant:

```bash
./remote_access.sh
```

- **Option 1 (Cloudflare Tunnels - Recommended)**: Outbound tunnel to Cloudflare Edge. Zero ports opened on router, free automatic SSL/HTTPS, and remote sensor nodes (ESP32/Pi) stream data with **zero VPN or client software required**.
- **Option 2 (Tailscale Mesh VPN)**: Private encrypted WireGuard mesh for administrative access between personal machines.
- **Option 3 (Port Forwarding & DDNS Guide)**: Traditional port forwarding walkthrough for non-CGNAT ISPs.
- **Option 4 (NAT / CGNAT Diagnostics)**: Check if your ISP uses CGNAT (`100.64.0.0/10`).

---

## 🐳 Docker Deployment

```bash
# Start server in background
docker compose up -d

# Start server + UDP edge gateway
docker compose --profile all up -d
```

---

## 📚 Detailed Documentation

- [Server Setup Guide (Raspberry Pi 4 & Laptop Optimization)](file:///home/morgan/Gropbox/SMACT2026/remote_3_axis_magnetometer/docs/SERVER_SETUP_GUIDE.md)
- [Residential NAT & Remote Access Guide](file:///home/morgan/Gropbox/SMACT2026/remote_3_axis_magnetometer/docs/NAT_AND_REMOTE_ACCESS.md)
- [Secrets Management & System Security Guide](file:///home/morgan/Gropbox/SMACT2026/remote_3_axis_magnetometer/docs/SECRETS_AND_SECURITY.md)
- [Central Server Architecture & API Specification](file:///home/morgan/Gropbox/SMACT2026/remote_3_axis_magnetometer/central_service/AI.md)
