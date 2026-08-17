#!/usr/bin/env bash
# ==============================================================================
# Magnetometer Central Server - Residential NAT & Remote Access Assistant
# Primary Recommended Route: Cloudflare Tunnels (Zero Router Config, Free SSL, CGNAT Bypass)
# ==============================================================================

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# Colors
GREEN='\033[0;32m'
BLUE='\033[0;34m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
RED='\033[0;31m'
BOLD='\033[1m'
NC='\033[0m'

function print_banner() {
    clear 2>/dev/null || true
    echo -e "${CYAN}${BOLD}"
    echo "======================================================================"
    echo "    Residential NAT & Remote Access Setup for Central Server"
    echo "======================================================================"
    echo -e "${NC}"
}

function ensure_cloudflared_installed() {
    if ! command -v cloudflared >/dev/null 2>&1; then
        echo -e "${YELLOW}cloudflared binary not found. Installing...${NC}"
        ARCH=$(uname -m)
        if [ "$ARCH" = "aarch64" ] || [ "$ARCH" = "arm64" ]; then
            CLOUDFLARED_URL="https://github.com/cloudflare/cloudflared/releases/latest/download/cloudflared-linux-arm64"
        elif [ "$ARCH" = "armv7l" ]; then
            CLOUDFLARED_URL="https://github.com/cloudflare/cloudflared/releases/latest/download/cloudflared-linux-arm"
        else
            CLOUDFLARED_URL="https://github.com/cloudflare/cloudflared/releases/latest/download/cloudflared-linux-amd64"
        fi
        
        echo "Downloading ${CLOUDFLARED_URL}..."
        sudo curl -L -s "$CLOUDFLARED_URL" -o /usr/local/bin/cloudflared
        sudo chmod +x /usr/local/bin/cloudflared
        echo -e "${GREEN}[+] cloudflared installed successfully to /usr/local/bin/cloudflared.${NC}\n"
    fi
}

function show_cloudflare_account_guide() {
    print_banner
    echo -e "${BOLD}--- Step-by-Step: Creating a Free Cloudflare Account & Domain ---${NC}\n"
    echo -e "${BOLD}Step 1: Sign up for Cloudflare (100% Free)${NC}"
    echo "  1. Visit https://dash.cloudflare.com/sign-up in your browser."
    echo "  2. Enter your email and choose a secure password."
    echo "  3. Select the Free plan ($0/month - includes unlimited tunnel bandwidth & free SSL)."

    echo -e "\n${BOLD}Step 2: Add your Domain to Cloudflare${NC}"
    echo "  - If you already own a domain (e.g. from Namecheap, Porkbun, GoDaddy):"
    echo "    1. In Cloudflare Dashboard, click 'Add a domain' (e.g. yoursite.org)."
    echo "    2. Select the 'Free' tier."
    echo "    3. Cloudflare will provide two nameservers (e.g., ns1.cloudflare.com, ns2.cloudflare.com)."
    echo "    4. Log in to your domain registrar and change the Custom Nameservers to Cloudflare's."
    echo "  - If you don't have a domain:"
    echo "    You can purchase one directly at wholesale cost ($9-$10/yr) inside Cloudflare Registrar,"
    echo "    or use a free dynamic subdomain/temporary trycloudflare link."

    echo -e "\n${BOLD}Step 3: Create a Cloudflare Tunnel in the Web Dashboard (Zero Trust)${NC}"
    echo "  1. In Cloudflare Dashboard, navigate to 'Zero Trust' on the left sidebar."
    echo "  2. Go to 'Networks' -> 'Tunnels' -> Click 'Create a tunnel'."
    echo "  3. Select 'Cloudflared' connector -> Name it (e.g., 'magnetometer-server')."
    echo "  4. Cloudflare will display an installation command with a Token:"
    echo -e "     ${CYAN}cloudflared service install <TUNNEL_TOKEN>${NC}"
    echo "  5. Copy that token and paste it into this script (Option 1 -> Token Setup)!"
    echo "  6. In Cloudflare Dashboard, set Public Hostname:"
    echo "     - Subdomain: mag (or data)"
    echo "     - Domain: yoursite.org"
    echo "     - Service Type: HTTP"
    echo "     - URL: localhost:8000"
    echo -e "  7. Click 'Save Tunnel' - Your server is now accessible worldwide at ${CYAN}https://mag.yoursite.org${NC}!"

    echo -e "\nPress Enter to return to menu..."
    read -r
}

function setup_cloudflare_tunnel() {
    print_banner
    echo -e "${BOLD}--- Option 1: Cloudflare Tunnels (Primary Recommended Route) ---${NC}\n"
    echo "Why Cloudflare Tunnels?"
    echo "  - Outbound connection to Cloudflare edge (Zero ports opened on home router)."
    echo "  - 100% works behind CGNAT, cellular hotspots, and dynamic residential IPs."
    echo "  - Remote sensor nodes (ESP32/Raspberry Pi) post directly via HTTPS with ZERO VPN installed."
    echo "  - Free automatic SSL/TLS certificate with global DDoS protection.\n"

    echo "Select an action:"
    echo "  1) Quick Instant Test Link (trycloudflare - Temporary HTTPS URL, no account needed)"
    echo "  2) Install Tunnel via Cloudflare Dashboard Token (Recommended for 24/7 Service)"
    echo "  3) CLI-based Tunnel Login & Configuration"
    echo "  4) View Step-by-Step Guide: How to create a Cloudflare account & domain"
    echo "  5) Return to Main Menu"
    echo ""
    read -p "Choice [1-5]: " cf_choice

    case "$cf_choice" in
        1)
            ensure_cloudflared_installed
            echo -e "\n${CYAN}Starting Quick Temporary Tunnel for http://localhost:8000 ...${NC}"
            echo "Watch the terminal output for the generated https://<random>.trycloudflare.com URL."
            echo "Press Ctrl+C to terminate the test tunnel."
            cloudflared tunnel --url http://localhost:8000
            ;;
        2)
            ensure_cloudflared_installed
            echo -e "\n${BOLD}Paste your Cloudflare Tunnel Token below${NC}"
            echo "(From Cloudflare Dashboard -> Zero Trust -> Networks -> Tunnels):"
            read -p "Token: " cf_token
            if [ -n "$cf_token" ]; then
                echo "Installing cloudflared systemd service..."
                sudo cloudflared service install "$cf_token"
                sudo systemctl daemon-reload
                sudo systemctl enable --now cloudflared
                echo -e "\n${GREEN}[+] Cloudflare Tunnel systemd service installed and started successfully!${NC}"
                echo -e "    Check status with: ${CYAN}sudo systemctl status cloudflared${NC}"
            else
                echo -e "${RED}[!] Error: No token provided.${NC}"
            fi
            read -p "Press Enter to continue..."
            ;;
        3)
            ensure_cloudflared_installed
            echo -e "\n${BOLD}CLI-Based Tunnel Setup:${NC}"
            echo "1. Authenticating with Cloudflare..."
            cloudflared tunnel login
            read -p "Enter a name for this tunnel (e.g. magnetometer): " t_name
            t_name="${t_name:-magnetometer}"
            cloudflared tunnel create "$t_name"
            read -p "Enter your full public hostname (e.g. mag.yourdomain.com): " host_name
            if [ -n "$host_name" ]; then
                cloudflared tunnel route dns "$t_name" "$host_name"
                echo "Creating ~/.cloudflared/config.yml ..."
                mkdir -p ~/.cloudflared
                cat <<EOF > ~/.cloudflared/config.yml
tunnel: $t_name
credentials-file: $HOME/.cloudflared/${t_name}.json

ingress:
  - hostname: $host_name
    service: http://localhost:8000
  - service: http_status:404
EOF
                echo "Installing systemd service..."
                sudo cloudflared service install
                sudo systemctl restart cloudflared
                echo -e "${GREEN}[+] Tunnel is live at https://${host_name}!${NC}"
            fi
            read -p "Press Enter to continue..."
            ;;
        4)
            show_cloudflare_account_guide
            ;;
        *)
            ;;
    esac
}

function setup_tailscale() {
    print_banner
    echo -e "${BOLD}--- Option 2: Tailscale Mesh VPN (Alternative for Private Multi-Device VPN) ---${NC}\n"
    echo "Note: Tailscale requires installing the Tailscale client app on all connected devices."
    echo "For field microcontrollers (ESP32) or open public web access, Cloudflare Tunnels (Option 1) is preferred.\n"

    if ! command -v tailscale >/dev/null 2>&1; then
        echo -e "${YELLOW}Tailscale is not installed.${NC}"
        read -p "Would you like to install Tailscale? (y/N): " choice
        if [[ "$choice" =~ ^[Yy]$ ]]; then
            curl -fsSL https://tailscale.com/install.sh | sh
            echo -e "${GREEN}[+] Tailscale installed successfully.${NC}"
        else
            return
        fi
    fi

    echo -e "\nStarting Tailscale..."
    sudo tailscale up

    TAILSCALE_IP=$(tailscale ip -4 2>/dev/null || echo "")
    if [ -n "$TAILSCALE_IP" ]; then
        echo -e "\n${GREEN}[+] Tailscale is active!${NC}"
        echo -e "  Tailscale Private IP:   ${CYAN}${TAILSCALE_IP}${NC}"
        echo -e "  Remote Dashboard URL:   ${CYAN}http://${TAILSCALE_IP}:8000/${NC}"
    fi

    echo -e "\nPress Enter to return to menu..."
    read -r
}

function show_port_forwarding_guide() {
    print_banner
    echo -e "${BOLD}--- Option 3: Traditional Dynamic DNS (DDNS) + Router Port Forwarding ---${NC}\n"
    
    LOCAL_IP=$(ip -4 addr show | grep -oP '(?<=inet\s)\d+(\.\d+){3}' | grep -v '127.0.0.1' | head -n 1 || hostname -I | awk '{print $1}')
    
    echo "If your residential ISP does NOT use CGNAT, you can forward ports on your router:"
    echo -e "\n${BOLD}Step 1: Set DHCP Reservation / Static IP on your Server${NC}"
    echo "  Ensure this machine always gets the same LAN IP: ${CYAN}${LOCAL_IP}${NC}"
    
    echo -e "\n${BOLD}Step 2: Router Port Forwarding Rule${NC}"
    echo "  Log in to your home router admin interface (usually http://192.168.1.1 or http://192.168.0.1)."
    echo "  Add a Port Forwarding / Virtual Server rule:"
    echo -e "  - Service Name:   ${CYAN}Magnetometer Server${NC}"
    echo -e "  - External Port:  ${CYAN}8000${NC} (or 443 with HTTPS reverse proxy)"
    echo -e "  - Internal IP:    ${CYAN}${LOCAL_IP}${NC}"
    echo -e "  - Internal Port:  ${CYAN}8000${NC}"
    echo -e "  - Protocol:       ${CYAN}TCP${NC}"

    echo -e "\n${BOLD}Step 3: Dynamic DNS (DDNS)${NC}"
    echo "  Since residential home IPs change periodically, use a free DDNS provider (e.g. DuckDNS)."

    echo -e "\n${BOLD}Step 4: Security Mandatory Checklist${NC}"
    echo -e "  ${YELLOW}[!] When opening ports to the public internet:${NC}"
    echo "  - Set API_KEY=\"your_strong_secret\" in central_service/.env"
    echo "  - Use a reverse proxy (Caddy / Nginx) for TLS/HTTPS encryption."

    echo -e "\nPress Enter to return to menu..."
    read -r
}

function check_cgnat_and_diagnostics() {
    print_banner
    echo -e "${BOLD}--- Residential NAT & Network Diagnostics ---${NC}\n"
    
    echo "Querying public IP address..."
    PUBLIC_IP=$(curl -s --connect-timeout 5 https://ifconfig.me || curl -s --connect-timeout 5 https://api.ipify.org || echo "Unknown")
    echo -e "  Public (Egress) IP:     ${CYAN}${PUBLIC_IP}${NC}"

    LOCAL_IP=$(ip -4 addr show | grep -oP '(?<=inet\s)\d+(\.\d+){3}' | grep -v '127.0.0.1' | head -n 1 || hostname -I | awk '{print $1}')
    echo -e "  Local Network (LAN) IP: ${CYAN}${LOCAL_IP}${NC}"

    GATEWAY_IP=$(ip route | grep default | awk '{print $3}' | head -n 1 || echo "Unknown")
    echo -e "  Home Router Gateway:    ${CYAN}${GATEWAY_IP}${NC}"

    echo -e "\n${BOLD}NAT Analysis:${NC}"
    if [[ "$PUBLIC_IP" =~ ^100\.(6[4-9]|[7-9][0-9]|1[0-1][0-9]|12[0-7])\. ]]; then
        echo -e "  ${RED}[!] Carrier-Grade NAT (CGNAT) Detected!${NC}"
        echo -e "      Your ISP uses CGNAT (RFC 6598 range 100.64.0.0/10)."
        echo -e "      Standard router port forwarding will NOT work because your public IP is shared."
        echo -e "      ${GREEN}Solution: Use Cloudflare Tunnels (Option 1) to securely bypass CGNAT with 0 router config.${NC}"
    else
        echo -e "  ${GREEN}[+] Standard Residential NAT / Dynamic Public IP detected.${NC}"
        echo -e "      Cloudflare Tunnels (Option 1) is still recommended for free SSL and zero router port forwarding."
    fi

    echo -e "\nPress Enter to return to menu..."
    read -r
}

# Main Interactive Loop
while true; do
    print_banner
    echo -e "${BOLD}Select a remote access option:${NC}"
    echo -e "  1) ${GREEN}${BOLD}Cloudflare Tunnels (RECOMMENDED)${NC} - Outbound tunnel, free SSL, 0 client config, CGNAT bypass"
    echo -e "  2) Tailscale Mesh VPN - Private encrypted peer-to-peer WireGuard mesh"
    echo -e "  3) Traditional Router Port Forwarding & DDNS Guide"
    echo -e "  4) Run Network Diagnostics & CGNAT Check"
    echo -e "  5) Exit"
    echo ""
    read -p "Enter choice [1-5]: " main_choice

    case "$main_choice" in
        1)
            setup_cloudflare_tunnel
            ;;
        2)
            setup_tailscale
            ;;
        3)
            show_port_forwarding_guide
            ;;
        4)
            check_cgnat_and_diagnostics
            ;;
        5)
            echo "Exiting."
            exit 0
            ;;
        *)
            echo "Invalid choice. Press Enter to retry."
            read -r
            ;;
    esac
done
