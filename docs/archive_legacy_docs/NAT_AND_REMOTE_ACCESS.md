# Running Behind a Residential NAT & Remote Access Guide

In a residential home setting, your server (Raspberry Pi 4, laptop, or mini-PC) sits behind a home Wi-Fi router. The router assigns a private local IP address (e.g. `192.168.1.100`) using Network Address Translation (NAT) or Carrier-Grade NAT (CGNAT).

This guide explains how to make your central data server securely accessible to remote field sensor nodes (ESP32 / Pi gateways) and web browsers worldwide **without opening router ports, with zero client-side VPN software required**.

---

## 1. Executive Summary: Why Cloudflare Tunnels is the Recommended Route

For a distributed sensor network with field nodes and multi-user web monitoring, **Cloudflare Tunnels (`cloudflared`)** is the primary recommended solution:

```
[ Field Sensor Node ] ---- HTTPS POST (Open Internet) ----+
(ESP32 / 4G Gateway)                                      |
                                                          v
[ Web Dashboard User ] --- HTTPS GET (Browser / Phone) -> [ Cloudflare Edge Network ]
                                                          |
                                           (Encrypted Outbound Tunnel)
                                                          v
                                                  [ cloudflared daemon ]
                                                  [ Central Data Server ] (Pi 4 / Laptop)
                                                  (Behind Residential NAT / CGNAT)
```

### Key Advantages:
1. **Zero Client-Side Software**: Remote field nodes (ESP32 microcontrollers, field Raspberry Pis, cellular relays) and web users connect via standard HTTPS URLs (e.g., `https://mag.yourdomain.com`). They do **not** need VPN apps, WireGuard, or client certificates.
2. **Zero Router Configuration**: The server establishes an outbound encrypted QUIC/HTTP2 tunnel to Cloudflare. You do **not** need to open or forward ports on your home router.
3. **100% CGNAT & Dynamic IP Immune**: Works behind residential ISPs, Carrier-Grade NAT (`100.64.0.0/10`), 4G/5G home internet, and dynamic residential public IPs.
4. **Free Automatic SSL/TLS**: Cloudflare provisions and auto-renews valid TLS certificates.
5. **DDoS Protection & Authentication**: Built-in rate limiting, optional Cloudflare Zero Trust authentication (Google/email PIN), and API key ingestion security.

---

## 2. Cloudflare Account & Domain Setup (Step-by-Step)

Setting up Cloudflare Tunnels is 100% free (including unlimited tunnel bandwidth).

### Step 1: Create a Free Cloudflare Account
1. Go to [dash.cloudflare.com/sign-up](https://dash.cloudflare.com/sign-up).
2. Enter your email and choose a password.
3. Choose the **Free Plan** ($0/month).

### Step 2: Add or Purchase a Domain
You need a domain name to route traffic (e.g., `yourdomain.com` or `sensor-station.org`):

- **Option A: Use an Existing Domain (Recommended if you already own one)**:
  1. In Cloudflare Dashboard, click **Add a Domain**.
  2. Select the **Free Plan**.
  3. Cloudflare will scan your DNS and provide two nameservers (e.g., `bob.ns.cloudflare.com`, `lisa.ns.cloudflare.com`).
  4. Log in to your domain registrar (Namecheap, GoDaddy, Porkbun, Google/Squarespace, etc.) and update the domain's Custom Nameservers to Cloudflare's.
  5. Wait a few minutes for verification.
- **Option B: Purchase a New Domain via Cloudflare Registrar**:
  - In Cloudflare Dashboard, go to **Domain Registration** -> **Register Domains**.
  - Domains cost at-cost wholesale pricing ($9–$10/year for `.com`, `.org`, etc.) with zero markup and free WHOIS privacy.
- **Option C: Zero-Account Instant Test Link (`trycloudflare`)**:
  - If you just want to test immediately without signing up for an account or domain, see **Section 3 (Mode C)** below.

---

## 3. Creating & Running the Cloudflare Tunnel

There are two primary ways to set up the tunnel:

### Mode A: Web Dashboard & Token Setup (Easiest — 1 Copy-Paste Command)

1. In the Cloudflare Dashboard, navigate to **Zero Trust** (or go to [one.dash.cloudflare.com](https://one.dash.cloudflare.com/)).
2. Under **Networks**, click **Tunnels**.
3. Click **Create a Tunnel** -> Select **Cloudflared** connector -> Click **Next**.
4. Name your tunnel (e.g. `magnetometer-central`).
5. Cloudflare will show an installation command with your unique **Tunnel Token**, for example:
   ```bash
   sudo cloudflared service install eyJhIjoi...
   ```
6. On your server machine (Pi or Laptop), either:
   - Run `./remote_access.sh` -> Select **Option 1 -> Token Setup** and paste the token, or
   - Run the displayed `sudo cloudflared service install <TOKEN>` command directly.
7. Click **Next** in the Cloudflare Dashboard.
8. Configure the **Public Hostname**:
   - **Subdomain**: `mag` (or `data`)
   - **Domain**: `yourdomain.com`
   - **Path**: *(leave empty)*
   - **Type**: `HTTP`
   - **URL**: `localhost:8000`
9. Click **Save Tunnel**.

**Done!** Your server is now accessible worldwide at `https://mag.yourdomain.com`.

---

### Mode B: CLI Setup (Command Line Only)

If you prefer configuring via SSH terminal:

1. Run the interactive assistant:
   ```bash
   cd central_service
   ./remote_access.sh
   # Select Option 1 -> (3) CLI-based Tunnel Setup
   ```
2. Or perform manually:
   ```bash
   # 1. Authenticate with Cloudflare
   cloudflared tunnel login

   # 2. Create named tunnel
   cloudflared tunnel create magnetometer

   # 3. Route your DNS hostname
   cloudflared tunnel route dns magnetometer mag.yourdomain.com

   # 4. Create config file ~/.cloudflared/config.yml
   cat <<EOF > ~/.cloudflared/config.yml
   tunnel: magnetometer
   credentials-file: /home/$USER/.cloudflared/magnetometer.json

   ingress:
     - hostname: mag.yourdomain.com
       service: http://localhost:8000
     - service: http_status:404
   EOF

   # 5. Install as systemd service
   sudo cloudflared service install
   sudo systemctl start cloudflared
   ```

---

### Mode C: Instant 1-Click Test Link (No Account or Domain Needed)

For quick field testing or temporary live sharing:

```bash
cd central_service
./remote_access.sh
# Select Option 1 -> (1) Quick Instant Test Link
```
Or directly:
```bash
cloudflared tunnel --url http://localhost:8000
```
This generates a temporary public HTTPS link (e.g. `https://random-words.trycloudflare.com`) in seconds.

---

## 4. Securing Ingestion & Remote Sensor Node Configuration

> [!TIP]
> For a deep dive on key generation, restricted file permissions (`chmod 600`), and ESP32 hardware flash encryption, see the dedicated [**Secrets Management & System Security Guide**](SECRETS_AND_SECURITY.md).

### Step 1: Enable Server API Key Authentication
To prevent unauthorized parties on the open internet from submitting fraudulent telemetry, set an `API_KEY` in `central_service/.env`:

```ini
# central_service/.env
API_KEY="your_secure_random_token_here_12345"
```
Restart the server to apply:
```bash
./manage.sh restart
```

- When `API_KEY` is set, all write requests (`POST /api/v1/telemetry`, `POST /api/v1/telemetry/batch`, `POST /api/v1/nodes/update`) require the `X-API-Key: your_secure_random_token_here_12345` header.
- Read-only endpoints (`GET /`, `GET /health`, `GET /api/v1/data`) remain open for browser viewing.

### Step 2: Configure Remote Gateways / ESP32 Nodes
On your remote field gateways:
```bash
export CENTRAL_SERVER_URL="https://mag.yourdomain.com"
export API_KEY="your_secure_random_token_here_12345"
python gateway.py
```
For standalone ESP32 WiFi nodes sending HTTP POSTs directly:
- Destination URL: `https://mag.yourdomain.com/api/v1/telemetry`
- Header: `X-API-Key: your_secure_random_token_here_12345`

### Step 3: (Optional) Cloudflare Zero Trust Access for Web GUI
If you wish to restrict the browser Web GUI to authorized users:
1. In Cloudflare Zero Trust Dashboard, go to **Access** -> **Applications** -> **Add an application** -> **Self-hosted**.
2. Set Domain to `mag.yourdomain.com`.
3. Add a Policy requiring your email or a Google/GitHub login.
4. Add a Bypass rule or Service Token for path `/api/v1/telemetry*` so field sensor nodes can continue sending data without user login.

---

## 5. Alternative Options Comparison

| Criteria | Cloudflare Tunnels (Recommended) | Tailscale Mesh VPN | Router Port Forwarding |
| :--- | :--- | :--- | :--- |
| **Primary Use Case** | Distributed sensor nodes & public/team web access | Private admin access between personal machines | Traditional self-hosting (non-CGNAT) |
| **Client Software Needed?** | **No** (Standard HTTPS for all clients) | **Yes** (Tailscale client on every device) | **No** |
| **ESP32 Microcontroller Compatible?** | **Yes** (Standard HTTP/HTTPS POST) | **No** (ESP32 cannot run Tailscale) | **Yes** |
| **Bypasses CGNAT / NAT?** | **Yes (100%)** | **Yes (100%)** | **No** |
| **Requires Open Router Ports?** | **No (0 ports opened)** | **No** | **Yes** (Port 8000/443 forwarded) |
| **SSL/HTTPS Encryption** | Automatic Cloudflare TLS | Automatic Tailscale TLS | Requires manual Caddy/Certbot |
| **Setup Time** | 3 Minutes | 2 Minutes | 15 Minutes |

---

## 6. Summary Quick Reference

To configure remote access at any time:
```bash
cd central_service
./remote_access.sh
```
Follow the interactive prompt to install the Cloudflare Tunnel service and begin streaming telemetry from anywhere in the world.
