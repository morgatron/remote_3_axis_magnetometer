#!/usr/bin/env bash
# ==============================================================================
# Magnetometer Central Server & Gateway - One-Click Automated Installer
# Target platforms: Raspberry Pi 4 (Raspberry Pi OS / Debian), Old Laptops (Ubuntu/Debian)
# ==============================================================================

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CURRENT_USER="${SUDO_USER:-$USER}"
IS_ROOT=false
if [ "$EUID" -eq 0 ]; then
    IS_ROOT=true
fi

# Visual Formatting
GREEN='\033[0;32m'
BLUE='\033[0;34m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
RED='\033[0;31m'
BOLD='\033[1m'
NC='\033[0m' # No Color

echo -e "${CYAN}${BOLD}"
echo "======================================================================"
echo "    Magnetometer Central Server & Gateway Setup Installer"
echo "======================================================================"
echo -e "${NC}"

# Detect Hardware / Environment
IS_RPI=false
IS_LAPTOP=false

if [ -f /proc/device-tree/model ] && grep -qi "Raspberry Pi" /proc/device-tree/model 2>/dev/null; then
    IS_RPI=true
    RPI_MODEL=$(tr -d '\0' < /proc/device-tree/model)
    echo -e "${GREEN}[+] Detected Hardware:${NC} ${RPI_MODEL}"
elif ls /sys/class/power_supply/BAT* 1>/dev/null 2>&1 || [ -d /proc/acpi/button/lid ]; then
    IS_LAPTOP=true
    echo -e "${GREEN}[+] Detected Hardware:${NC} Laptop System"
else
    echo -e "${GREEN}[+] Detected Hardware:${NC} Linux Host / Server"
fi

# Step 1: System Packages
echo -e "\n${BLUE}[1/5] Checking and installing system dependencies...${NC}"

if command -v apt-get >/dev/null 2>&1; then
    SUDO_CMD=""
    if [ "$IS_ROOT" = false ]; then
        SUDO_CMD="sudo"
    fi
    echo "Updating package lists (apt)..."
    $SUDO_CMD apt-get update -y -q
    $SUDO_CMD apt-get install -y -q python3 python3-pip python3-venv sqlite3 curl iproute2 iw
elif command -v dnf >/dev/null 2>&1; then
    sudo dnf install -y python3 python3-pip sqlite curl iproute iw
elif command -v pacman >/dev/null 2>&1; then
    sudo pacman -Sy --noconfirm python python-pip sqlite curl iproute2 iw
else
    echo -e "${YELLOW}[!] Notice: Non-Debian/RHEL package manager detected. Ensure Python 3.9+, venv, and sqlite3 are installed.${NC}"
fi

# Step 2: Python Virtual Environment
echo -e "\n${BLUE}[2/5] Setting up Python virtual environment...${NC}"
cd "$SCRIPT_DIR"

# Ensure venv is created and owned by the actual user, not root
if [ ! -d ".venv" ]; then
    echo "Creating Python virtual environment in ${SCRIPT_DIR}/.venv (User: ${CURRENT_USER})..."
    if [ "$IS_ROOT" = true ] && [ -n "$SUDO_USER" ]; then
        sudo -u "$SUDO_USER" python3 -m venv .venv
    else
        python3 -m venv .venv
    fi
fi

echo "Installing / updating Python dependencies..."
if [ "$IS_ROOT" = true ] && [ -n "$SUDO_USER" ]; then
    sudo -u "$SUDO_USER" "${SCRIPT_DIR}/.venv/bin/pip" install --upgrade pip -q
    sudo -u "$SUDO_USER" "${SCRIPT_DIR}/.venv/bin/pip" install -r "${SCRIPT_DIR}/requirements.txt" -q
else
    "${SCRIPT_DIR}/.venv/bin/pip" install --upgrade pip -q
    "${SCRIPT_DIR}/.venv/bin/pip" install -r "${SCRIPT_DIR}/requirements.txt" -q
fi

# Explicitly ensure executable bits and user ownership
chmod -R u+rwX,go+rX "${SCRIPT_DIR}/.venv"
chmod +x "${SCRIPT_DIR}/.venv/bin/"* 2>/dev/null || true
if [ "$IS_ROOT" = true ] && [ -n "$SUDO_USER" ]; then
    chown -R "${SUDO_USER}:${SUDO_USER}" "${SCRIPT_DIR}"
fi

# Step 3: Environment Configuration (.env)
echo -e "\n${BLUE}[3/5] Checking configuration file (.env)...${NC}"
if [ ! -f "${SCRIPT_DIR}/.env" ]; then
    echo "Creating .env from .env.example ..."
    cp "${SCRIPT_DIR}/.env.example" "${SCRIPT_DIR}/.env"
    if [ "$IS_ROOT" = true ] && [ -n "$SUDO_USER" ]; then
        chown "${SUDO_USER}:${SUDO_USER}" "${SCRIPT_DIR}/.env"
    fi
    chmod 600 "${SCRIPT_DIR}/.env"
    echo -e "${GREEN}[+] Generated default .env (permissions set to 0600)${NC}"
else
    echo -e "${GREEN}[+] Existing .env configuration preserved.${NC}"
fi

# Step 4: Hardware Specific Optimizations
echo -e "\n${BLUE}[4/5] Applying platform optimizations...${NC}"

if [ "$IS_LAPTOP" = true ]; then
    echo -e "${YELLOW}[Laptop Mode Detected]${NC}"
    echo "Configuring laptop lid close behavior so the server keeps running with the screen closed..."
    
    # Configure systemd logind to ignore lid switch
    LOGIND_CONF="/etc/systemd/logind.conf"
    if [ -f "$LOGIND_CONF" ]; then
        SUDO_CMD=""
        if [ "$IS_ROOT" = false ]; then SUDO_CMD="sudo"; fi
        
        # Check if HandleLidSwitch is already ignored
        if ! grep -q "^HandleLidSwitch=ignore" "$LOGIND_CONF"; then
            echo "Setting HandleLidSwitch=ignore in ${LOGIND_CONF}..."
            $SUDO_CMD sed -i 's/^#\?HandleLidSwitch=.*/HandleLidSwitch=ignore/' "$LOGIND_CONF"
            $SUDO_CMD sed -i 's/^#\?HandleLidSwitchExternalPower=.*/HandleLidSwitchExternalPower=ignore/' "$LOGIND_CONF"
            $SUDO_CMD sed -i 's/^#\?HandleLidSwitchDocked=.*/HandleLidSwitchDocked=ignore/' "$LOGIND_CONF"
            $SUDO_CMD systemctl restart systemd-logind.service 2>/dev/null || true
            echo -e "${GREEN}[+] Laptop lid close suspend disabled successfully.${NC}"
        else
            echo -e "${GREEN}[+] Laptop lid close is already configured to ignore.${NC}"
        fi
    fi

    # Disable Wi-Fi power saving if wireless interface exists
    WLAN_IFACE=$(ip -o link show | awk -F': ' '{print $2}' | grep -E '^wl' | head -n 1 || true)
    if [ -n "$WLAN_IFACE" ]; then
        echo "Disabling power saving on Wi-Fi interface ($WLAN_IFACE) to maintain low latency..."
        if [ "$IS_ROOT" = false ]; then sudo iw dev "$WLAN_IFACE" set power_save off 2>/dev/null || true; else iw dev "$WLAN_IFACE" set power_save off 2>/dev/null || true; fi
    fi
fi

if [ "$IS_RPI" = true ]; then
    echo -e "${YELLOW}[Raspberry Pi Mode]${NC}"
    echo -e "${GREEN}[+] SQLite WAL (Write-Ahead Logging) is active for high write performance & SD card safety.${NC}"
fi

# Step 5: Systemd Service Installation (User-level service for SELinux & home directory safety)
echo -e "\n${BLUE}[5/5] Installing systemd auto-start services (user mode)...${NC}"

if command -v systemctl >/dev/null 2>&1; then
    USER_SYSTEMD_DIR="${HOME}/.config/systemd/user"
    mkdir -p "${USER_SYSTEMD_DIR}"

    SERVER_SERVICE_PATH="${USER_SYSTEMD_DIR}/magnetometer-server.service"
    GATEWAY_SERVICE_PATH="${USER_SYSTEMD_DIR}/magnetometer-gateway.service"

    echo "Generating ${SERVER_SERVICE_PATH}..."
    cat <<EOF > "${SERVER_SERVICE_PATH}"
[Unit]
Description=Magnetometer Central Data Server & Web Dashboard
After=network.target

[Service]
Type=simple
WorkingDirectory=${SCRIPT_DIR}
EnvironmentFile=-${SCRIPT_DIR}/.env
ExecStart=${SCRIPT_DIR}/.venv/bin/python ${SCRIPT_DIR}/server.py
Restart=always
RestartSec=3
LimitNOFILE=65536
StandardOutput=journal
StandardError=journal
SyslogIdentifier=magnetometer-server

[Install]
WantedBy=default.target
EOF

    echo "Generating ${GATEWAY_SERVICE_PATH}..."
    cat <<EOF > "${GATEWAY_SERVICE_PATH}"
[Unit]
Description=Magnetometer Edge Gateway (UDP/Serial Ingestion)
After=network.target magnetometer-server.service
Wants=magnetometer-server.service

[Service]
Type=simple
WorkingDirectory=${SCRIPT_DIR}
EnvironmentFile=-${SCRIPT_DIR}/.env
ExecStart=${SCRIPT_DIR}/.venv/bin/python ${SCRIPT_DIR}/gateway.py
Restart=always
RestartSec=3
LimitNOFILE=65536
StandardOutput=journal
StandardError=journal
SyslogIdentifier=magnetometer-gateway

[Install]
WantedBy=default.target
EOF

    # Ensure all files and executables are runnable by current user
    chmod -R u+rwX,go+rX "${SCRIPT_DIR}/.venv"
    chmod +x "${SCRIPT_DIR}/.venv/bin/"* 2>/dev/null || true
    chmod +x "${SCRIPT_DIR}/server.py" "${SCRIPT_DIR}/gateway.py" 2>/dev/null || true

    # Clean up old system-level service if present to avoid dual-running/conflicts
    if [ -f "/etc/systemd/system/magnetometer-server.service" ]; then
        echo "Cleaning up legacy system-level services in /etc/systemd/system/..."
        if [ "$IS_ROOT" = false ]; then
            sudo systemctl disable --now magnetometer-server.service magnetometer-gateway.service 2>/dev/null || true
            sudo rm -f /etc/systemd/system/magnetometer-server.service /etc/systemd/system/magnetometer-gateway.service 2>/dev/null || true
            sudo systemctl daemon-reload 2>/dev/null || true
        else
            systemctl disable --now magnetometer-server.service magnetometer-gateway.service 2>/dev/null || true
            rm -f /etc/systemd/system/magnetometer-server.service /etc/systemd/system/magnetometer-gateway.service 2>/dev/null || true
            systemctl daemon-reload 2>/dev/null || true
        fi
    fi

    # Enable lingering so user services run even when user is not logged in
    echo "Enabling user lingering so services run on boot without active login..."
    loginctl enable-linger "${CURRENT_USER}" 2>/dev/null || sudo loginctl enable-linger "${CURRENT_USER}" 2>/dev/null || true

    echo "Reloading systemd user daemon and enabling services..."
    systemctl --user daemon-reload
    systemctl --user enable --now magnetometer-server.service
    systemctl --user enable --now magnetometer-gateway.service
    echo -e "${GREEN}[+] User services enabled and started successfully.${NC}"
else
    echo -e "${YELLOW}[!] Notice: systemd not found. You can run manually via: ./manage.sh start${NC}"
fi

# Make scripts executable
chmod +x "${SCRIPT_DIR}/manage.sh" "${SCRIPT_DIR}/remote_access.sh" "${SCRIPT_DIR}/install.sh" 2>/dev/null || true

# Discover LAN IP Addresses
LAN_IPS=$(ip -4 addr show | grep -oP '(?<=inet\s)\d+(\.\d+){3}' | grep -v '127.0.0.1' || hostname -I || echo "localhost")
PRIMARY_IP=$(echo "$LAN_IPS" | awk '{print $1}')

echo -e "\n${GREEN}${BOLD}======================================================================${NC}"
echo -e "${GREEN}${BOLD}       INSTALLATION & SETUP COMPLETE! SERVER IS LIVE!                ${NC}"
echo -e "${GREEN}${BOLD}======================================================================${NC}"
echo -e "\n${BOLD}Local Access URLs:${NC}"
echo -e "  - Web Dashboard:     ${CYAN}http://${PRIMARY_IP}:8000/${NC} (or http://localhost:8000/)"
echo -e "  - REST API & Docs:   ${CYAN}http://${PRIMARY_IP}:8000/docs${NC}"
echo -e "  - Health Check:      ${CYAN}http://${PRIMARY_IP}:8000/health${NC}"
echo -e "  - UDP Stream Ingest: ${CYAN}Port 9876 (UDP)${NC}"

echo -e "\n${BOLD}Management Commands:${NC}"
echo -e "  - Check Status:      ${YELLOW}./manage.sh status${NC}"
echo -e "  - View Live Logs:    ${YELLOW}./manage.sh logs${NC}"
echo -e "  - Restart Server:    ${YELLOW}./manage.sh restart${NC}"
echo -e "  - Database Backup:   ${YELLOW}./manage.sh backup${NC}"

echo -e "\n${BOLD}Running Behind a Residential NAT / Remote Access:${NC}"
echo -e "  - Run the NAT remote access helper: ${CYAN}./remote_access.sh${NC}"
echo -e "    (Sets up Cloudflare Tunnels for zero-config global HTTPS with 0 router port-forwarding!)"
echo -e "======================================================================\n"
