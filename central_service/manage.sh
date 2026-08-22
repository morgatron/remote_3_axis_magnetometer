#!/usr/bin/env bash
# ==============================================================================
# Magnetometer Central Service Control & Operations Manager (`manage.sh`)
# ==============================================================================

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# Try loading .env
if [ -f .env ]; then
    set -a
    source .env
    set +a
fi

DB_FILE="${DB_FILE:-magnetometer.db}"
PORT="${PORT:-8000}"
HOST="${HOST:-0.0.0.0}"
API_KEY="${API_KEY:-}"

# Colors
GREEN='\033[0;32m'
BLUE='\033[0;34m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
RED='\033[0;31m'
BOLD='\033[1m'
NC='\033[0m'

function print_header() {
    echo -e "${CYAN}${BOLD}"
    echo "======================================================================"
    echo "       Magnetometer Central Server Operations Manager"
    echo "======================================================================"
    echo -e "${NC}"
}

function get_systemd_mode() {
    if command -v systemctl >/dev/null 2>&1; then
        if systemctl --user is-active magnetometer-server.service >/dev/null 2>&1 || [ -f "${HOME}/.config/systemd/user/magnetometer-server.service" ]; then
            echo "user"
            return
        elif systemctl is-active magnetometer-server.service >/dev/null 2>&1 || [ -f "/etc/systemd/system/magnetometer-server.service" ]; then
            echo "system"
            return
        fi
    fi
    echo "none"
}

function show_status() {
    print_header
    echo -e "${BOLD}--- Services Status ---${NC}"

    SYSTEMD_MODE=$(get_systemd_mode)

    if [ "$SYSTEMD_MODE" = "user" ]; then
        SERVER_ACTIVE=$(systemctl --user is-active magnetometer-server.service 2>/dev/null || echo "inactive")
        GATEWAY_ACTIVE=$(systemctl --user is-active magnetometer-gateway.service 2>/dev/null || echo "inactive")
        
        if [ "$SERVER_ACTIVE" = "active" ]; then
            echo -e "  magnetometer-server:  ${GREEN}[ACTIVE / RUNNING]${NC} (systemd --user)"
        else
            echo -e "  magnetometer-server:  ${RED}[INACTIVE / STOPPED]${NC} (systemd --user)"
        fi

        if [ "$GATEWAY_ACTIVE" = "active" ]; then
            echo -e "  magnetometer-gateway: ${GREEN}[ACTIVE / RUNNING]${NC} (systemd --user)"
        else
            echo -e "  magnetometer-gateway: ${YELLOW}[INACTIVE / STOPPED]${NC} (systemd --user)"
        fi
    elif [ "$SYSTEMD_MODE" = "system" ]; then
        SERVER_ACTIVE=$(systemctl is-active magnetometer-server.service 2>/dev/null || echo "inactive")
        GATEWAY_ACTIVE=$(systemctl is-active magnetometer-gateway.service 2>/dev/null || echo "inactive")
        
        if [ "$SERVER_ACTIVE" = "active" ]; then
            echo -e "  magnetometer-server:  ${GREEN}[ACTIVE / RUNNING]${NC} (systemd system)"
        else
            echo -e "  magnetometer-server:  ${RED}[INACTIVE / STOPPED]${NC} (systemd system)"
        fi

        if [ "$GATEWAY_ACTIVE" = "active" ]; then
            echo -e "  magnetometer-gateway: ${GREEN}[ACTIVE / RUNNING]${NC} (systemd system)"
        else
            echo -e "  magnetometer-gateway: ${YELLOW}[INACTIVE / STOPPED]${NC} (systemd system)"
        fi
    else
        # Process check
        SERVER_PIDS=$(pgrep -f "server.py" || true)
        GATEWAY_PIDS=$(pgrep -f "gateway.py" || true)

        if [ -n "$SERVER_PIDS" ]; then
            echo -e "  magnetometer-server:  ${GREEN}[RUNNING (PID: $SERVER_PIDS)]${NC}"
        else
            echo -e "  magnetometer-server:  ${RED}[STOPPED]${NC}"
        fi

        if [ -n "$GATEWAY_PIDS" ]; then
            echo -e "  magnetometer-gateway: ${GREEN}[RUNNING (PID: $GATEWAY_PIDS)]${NC}"
        else
            echo -e "  magnetometer-gateway: ${YELLOW}[STOPPED]${NC}"
        fi
    fi

    # API Health Check
    echo -e "\n${BOLD}--- HTTP API Health Check ---${NC}"
    HEALTH_RESP=$(curl -s --connect-timeout 2 "http://localhost:${PORT}/health" 2>/dev/null || echo "")
    if [ -n "$HEALTH_RESP" ]; then
        echo -e "  Health Endpoint:      ${GREEN}OK${NC} (${HEALTH_RESP})"
    else
        echo -e "  Health Endpoint:      ${RED}UNREACHABLE${NC} (Server may be starting or down)"
    fi

    # Database Metrics
    echo -e "\n${BOLD}--- Database Metrics (${DB_FILE}) ---${NC}"
    if [ -f "$DB_FILE" ]; then
        DB_SIZE=$(du -h "$DB_FILE" | awk '{print $1}')
        echo -e "  Database File Size:   ${CYAN}${DB_SIZE}${NC}"
        
        # SQLite Query metrics if sqlite3 is installed
        if command -v sqlite3 >/dev/null 2>&1; then
            RECORD_COUNT=$(sqlite3 "$DB_FILE" "SELECT count(*) FROM telemetry;" 2>/dev/null || echo "0")
            NODE_COUNT=$(sqlite3 "$DB_FILE" "SELECT count(*) FROM nodes;" 2>/dev/null || echo "0")
            LATEST_RECORD=$(sqlite3 "$DB_FILE" "SELECT timestamp, node_id, x, y, z FROM telemetry ORDER BY id DESC LIMIT 1;" 2>/dev/null || echo "")
            
            echo -e "  Total Ingested:       ${CYAN}${RECORD_COUNT} records${NC}"
            echo -e "  Registered Nodes:     ${CYAN}${NODE_COUNT} nodes${NC}"
            if [ -n "$LATEST_RECORD" ]; then
                echo -e "  Latest Reading:       ${GREEN}${LATEST_RECORD}${NC}"
            fi
        fi
    else
        echo -e "  Database File:        ${YELLOW}Not yet created (will initialize on first write/startup)${NC}"
    fi

    # Local Network Addresses
    echo -e "\n${BOLD}--- Network Access ---${NC}"
    LAN_IPS=$(ip -4 addr show | grep -oP '(?<=inet\s)\d+(\.\d+){3}' | grep -v '127.0.0.1' || hostname -I || echo "localhost")
    for ip in $LAN_IPS; do
        echo -e "  Web Dashboard:        ${CYAN}http://${ip}:${PORT}/${NC}"
    done
    echo -e "  UDP Stream Ingestion: Port ${UDP_PORT:-9876} (UDP)"
    echo "======================================================================"
}

function start_services() {
    SYSTEMD_MODE=$(get_systemd_mode)
    if [ "$SYSTEMD_MODE" = "user" ]; then
        echo "Starting systemd user services..."
        systemctl --user start magnetometer-server.service magnetometer-gateway.service
        echo -e "${GREEN}[+] Services started via systemd --user.${NC}"
    elif [ "$SYSTEMD_MODE" = "system" ]; then
        echo "Starting systemd system services..."
        sudo systemctl start magnetometer-server.service magnetometer-gateway.service
        echo -e "${GREEN}[+] Services started via systemd.${NC}"
    else
        echo "Starting server in background..."
        nohup "${SCRIPT_DIR}/.venv/bin/python" "${SCRIPT_DIR}/server.py" > server.log 2>&1 &
        nohup "${SCRIPT_DIR}/.venv/bin/python" "${SCRIPT_DIR}/gateway.py" > gateway.log 2>&1 &
        echo -e "${GREEN}[+] Services launched in background.${NC}"
    fi
}

function stop_services() {
    SYSTEMD_MODE=$(get_systemd_mode)
    if [ "$SYSTEMD_MODE" = "user" ]; then
        echo "Stopping systemd user services..."
        systemctl --user stop magnetometer-server.service magnetometer-gateway.service
        echo -e "${GREEN}[+] Services stopped.${NC}"
    elif [ "$SYSTEMD_MODE" = "system" ]; then
        echo "Stopping systemd system services..."
        sudo systemctl stop magnetometer-server.service magnetometer-gateway.service
        echo -e "${GREEN}[+] Services stopped.${NC}"
    else
        echo "Stopping background processes..."
        pkill -f "server.py" || true
        pkill -f "gateway.py" || true
        echo -e "${GREEN}[+] Processes terminated.${NC}"
    fi
}

function restart_services() {
    SYSTEMD_MODE=$(get_systemd_mode)
    if [ "$SYSTEMD_MODE" = "user" ]; then
        echo "Restarting systemd user services..."
        systemctl --user restart magnetometer-server.service magnetometer-gateway.service
        echo -e "${GREEN}[+] Services restarted via systemd --user.${NC}"
    elif [ "$SYSTEMD_MODE" = "system" ]; then
        echo "Restarting systemd system services..."
        sudo systemctl restart magnetometer-server.service magnetometer-gateway.service
        echo -e "${GREEN}[+] Services restarted.${NC}"
    else
        stop_services
        sleep 1
        start_services
    fi
}

function tail_logs() {
    SYSTEMD_MODE=$(get_systemd_mode)
    if [ "$SYSTEMD_MODE" = "user" ]; then
        echo -e "${CYAN}Streaming systemd user logs (Ctrl+C to exit)...${NC}"
        journalctl --user -u magnetometer-server -u magnetometer-gateway -f -n 50
    elif [ "$SYSTEMD_MODE" = "system" ]; then
        echo -e "${CYAN}Streaming systemd live logs (Ctrl+C to exit)...${NC}"
        sudo journalctl -u magnetometer-server -u magnetometer-gateway -f -n 50
    elif [ -f server.log ]; then
        echo -e "${CYAN}Streaming server.log & gateway.log (Ctrl+C to exit)...${NC}"
        tail -f -n 50 server.log gateway.log 2>/dev/null
    else
        echo "No log files found. Use 'manage.sh start' or install systemd service."
    fi
}

function backup_db() {
    print_header
    mkdir -p "${SCRIPT_DIR}/backups"
    TIMESTAMP=$(date +%Y%m%d_%H%M%S)
    BACKUP_TARGET="${SCRIPT_DIR}/backups/magnetometer_backup_${TIMESTAMP}.db"

    if [ ! -f "$DB_FILE" ]; then
        echo -e "${RED}[!] Error: Database file ${DB_FILE} not found.${NC}"
        exit 1
    fi

    echo "Creating safe online SQLite backup..."
    if command -v sqlite3 >/dev/null 2>&1; then
        sqlite3 "$DB_FILE" ".backup '${BACKUP_TARGET}'"
        BACKUP_SIZE=$(du -h "$BACKUP_TARGET" | awk '{print $1}')
        echo -e "${GREEN}[+] Backup created successfully!${NC}"
        echo -e "    File: ${CYAN}${BACKUP_TARGET}${NC} (${BACKUP_SIZE})"
    else
        cp "$DB_FILE" "$BACKUP_TARGET"
        echo -e "${GREEN}[+] Copied database to ${BACKUP_TARGET}${NC}"
    fi
}

function test_server() {
    print_header
    echo "Sending test telemetry sample to central server..."
    HEADER_AUTH=()
    if [ -n "$API_KEY" ]; then
        HEADER_AUTH=(-H "X-API-Key: ${API_KEY}")
    fi

    TEST_PAYLOAD='{"node_id":"SELF_TEST_NODE","x":23415.2,"y":-4120.8,"z":48910.1,"units":"nT","temp":24.5,"vbat":3700,"rssi":-62}'

    RESPONSE=$(curl -s -w "\n%{http_code}" -X POST "http://localhost:${PORT}/api/v1/telemetry" \
        -H "Content-Type: application/json" \
        "${HEADER_AUTH[@]}" \
        -d "$TEST_PAYLOAD")

    HTTP_BODY=$(echo "$RESPONSE" | head -n -1)
    HTTP_CODE=$(echo "$RESPONSE" | tail -n 1)

    if [ "$HTTP_CODE" -eq 201 ] || [ "$HTTP_CODE" -eq 200 ]; then
        echo -e "${GREEN}[+] Self-Test SUCCESS! (HTTP ${HTTP_CODE})${NC}"
        echo -e "    Response: ${HTTP_BODY}"
    else
        echo -e "${RED}[!] Self-Test FAILED (HTTP ${HTTP_CODE})${NC}"
        echo -e "    Response: ${HTTP_BODY}"
        exit 1
    fi
}

function show_help() {
    print_header
    echo -e "${BOLD}Usage:${NC} $0 <command> [arguments...]"
    echo ""
    echo -e "${CYAN}${BOLD}--- Service Control ---${NC}"
    echo -e "  ${GREEN}status${NC}                  Show server/gateway status, PID, DB size, record counts"
    echo -e "  ${GREEN}start${NC}                   Start central server and serial/UDP gateway"
    echo -e "  ${GREEN}stop${NC}                    Stop running background services"
    echo -e "  ${GREEN}restart${NC}                 Restart central server and gateway"
    echo -e "  ${GREEN}logs${NC}                    Live stream server & gateway log output"
    echo ""
    echo -e "${CYAN}${BOLD}--- Station & Node Management ---${NC}"
    echo -e "  ${GREEN}nodes${NC} | ${GREEN}node list${NC}         List registered nodes, station names, GPS coords, records"
    echo -e "  ${GREEN}node update${NC} [args]      Update metadata (name, lat, lon, elev, notes) [wizard or flags]"
    echo -e "  ${GREEN}node delete${NC} [ID] [flags] Delete a node [interactive picker, candidate preview, confirmation]"
    echo -e "  ${GREEN}node prune${NC}  [flags]     Prune inactive nodes (> N days) [dry-run preview, telemetry safety]"
    echo ""
    echo -e "${CYAN}${BOLD}--- Data & Operations ---${NC}"
    echo -e "  ${GREEN}export${NC} [format]         Export telemetry (csv, parquet, npz, json)"
    echo -e "  ${GREEN}backup${NC}                  Create safe online SQLite snapshot in backups/"
    echo -e "  ${GREEN}test${NC}                    Send synthetic test telemetry sample to verify ingestion"
    echo -e "  ${GREEN}help${NC}                    Show this reference manual"
    echo ""
    echo -e "${YELLOW}${BOLD}Examples:${NC}"
    echo -e "  $0 status"
    echo -e "  $0 node list"
    echo -e "  $0 node update NODE_3A8 --lat -33.8568 --lon 151.2153"
    echo -e "  $0 node prune --days 30"
    echo -e "  $0 node delete TEST_NODE --purge"
    echo -e "  $0 export parquet"
    echo "======================================================================"
}

function show_node_help() {
    print_header
    echo -e "${BOLD}Node & Station Management Usage:${NC} $0 node <subcommand> [options]"
    echo ""
    echo -e "  ${GREEN}list${NC}                          Display all registered nodes, station names, GPS coordinates, and records"
    echo -e "  ${GREEN}update${NC} [NODE_ID] [flags]      Update station name, coordinates, elevation, baseline, or notes"
    echo -e "  ${GREEN}delete${NC} [NODE_ID] [--purge] [-y] Delete a single node (interactive picker if NODE_ID omitted)"
    echo -e "  ${GREEN}prune${NC}  [--days N] [--purge] [-y] Safely prune inactive nodes with dry-run candidate preview"
    echo ""
    echo -e "${YELLOW}${BOLD}Flags for delete & prune:${NC}"
    echo -e "  ${CYAN}--days <N>${NC}             Inactivity threshold in days (default: 30)"
    echo -e "  ${CYAN}--purge | -p${NC}           Permanently delete associated historical telemetry rows as well"
    echo -e "  ${CYAN}--yes | -y${NC}             Skip interactive confirmation prompt (for non-interactive scripts)"
    echo ""
    echo -e "${YELLOW}${BOLD}Data Safety Guarantees:${NC}"
    echo -e "  - By default, ${BOLD}historical telemetry is PRESERVED${NC} (only the station registration is removed)."
    echo -e "  - A ${BOLD}dry-run preview${NC} shows candidate nodes and sample counts before any changes occur."
    echo -e "  - Explicit confirmation is required unless ${CYAN}-y${NC} is specified."
    echo "======================================================================"
}

function export_data() {
    FORMAT="${1:-csv}"
    
    if [ "$FORMAT" = "help" ] || [ "$FORMAT" = "--help" ] || [ "$FORMAT" = "-h" ]; then
        print_header
        echo -e "${BOLD}Data Exporter Usage:${NC} $0 export [csv|parquet|npz|json]"
        echo ""
        echo -e "  ${CYAN}csv${NC}     - Standard 6-column tabular time-series (Default)"
        echo -e "  ${CYAN}parquet${NC} - Compressed Apache Parquet for high-speed Pandas / Polars analysis"
        echo -e "  ${CYAN}npz${NC}     - Compressed NumPy archive with timestamps and 3-axis array vectors"
        echo -e "  ${CYAN}json${NC}    - Structured JSON list of telemetry readings"
        echo "======================================================================"
        return
    fi

    TIMESTAMP=$(date +%Y%m%d_%H%M%S)
    OUTFILE="magnetometer_export_${TIMESTAMP}.${FORMAT}"

    echo "Exporting database telemetry in '${FORMAT}' format..."
    curl -s "http://localhost:${PORT}/api/v1/data?format=${FORMAT}" -o "$OUTFILE"
    
    if [ -f "$OUTFILE" ] && [ -s "$OUTFILE" ]; then
        SIZE=$(du -h "$OUTFILE" | awk '{print $1}')
        echo -e "${GREEN}[+] Export complete: ${CYAN}${OUTFILE}${NC} (${SIZE})"
    else
        echo -e "${RED}[!] Export failed or database is empty.${NC}"
    fi
}

function manage_nodes() {
    ACTION="${1:-}"
    shift || true

    HEADER_AUTH=()
    if [ -n "$API_KEY" ]; then
        HEADER_AUTH=(-H "X-API-Key: ${API_KEY}")
    fi

    # Interactive menu if no subcommand provided and interactive terminal
    if [ -z "$ACTION" ]; then
        if [ -t 0 ]; then
            print_header
            echo -e "${BOLD}--- Node & Station Management ---${NC}"
            echo "  [1] List all registered sensor nodes"
            echo "  [2] Update station metadata / GPS location"
            echo "  [3] Delete a single node"
            echo "  [4] Prune inactive nodes (> N days)"
            echo "  [q] Quit"
            echo ""
            read -r -p "Select an option [1-4/q]: " CHOICE
            case "$CHOICE" in
                1) ACTION="list" ;;
                2) ACTION="update" ;;
                3) ACTION="delete" ;;
                4) ACTION="prune" ;;
                q|Q|"") echo "Cancelled."; return 0 ;;
                *) echo "Invalid option."; return 1 ;;
            esac
        else
            ACTION="list"
        fi
    fi

    case "$ACTION" in
        list|ls)
            print_header
            echo -e "${BOLD}--- Registered Sensor Nodes ---${NC}"
            if [ -f "${SCRIPT_DIR}/../scripts/update_node.py" ]; then
                python3 "${SCRIPT_DIR}/../scripts/update_node.py" --server "http://localhost:${PORT}" --list
            elif command -v sqlite3 >/dev/null 2>&1 && [ -f "$DB_FILE" ]; then
                sqlite3 -header -column "$DB_FILE" "SELECT node_id, name, lat, lon, elevation_m, last_seen, sensor_model FROM nodes ORDER BY node_id;"
            else
                curl -s "http://localhost:${PORT}/api/v1/nodes"
            fi
            ;;

        delete|del|rm)
            NODE_ID=""
            PURGE_FLAG="false"
            PURGE_EXPLICIT=0
            AUTO_YES=0

            while [ $# -gt 0 ]; do
                case "$1" in
                    --purge|-p)
                        PURGE_FLAG="true"
                        PURGE_EXPLICIT=1
                        shift
                        ;;
                    --yes|-y)
                        AUTO_YES=1
                        shift
                        ;;
                    --help|-h)
                        show_node_help
                        return 0
                        ;;
                    *)
                        if [ -z "$NODE_ID" ]; then
                            NODE_ID="$1"
                        fi
                        shift
                        ;;
                esac
            done

            # If NODE_ID is missing and running in interactive terminal, prompt with a numbered list
            if [ -z "$NODE_ID" ]; then
                if [ -t 0 ]; then
                    print_header
                    echo -e "${BOLD}Fetching registered nodes...${NC}"
                    NODES_JSON=$(curl -s "${HEADER_AUTH[@]}" "http://localhost:${PORT}/api/v1/nodes" 2>/dev/null || echo "[]")
                    
                    PICKED_ID=$(python3 -c "
import sys, json
try:
    nodes = json.loads('''$NODES_JSON''')
    if not nodes:
        print('NO_NODES', file=sys.stderr)
        sys.exit(0)
    print('\nAvailable Nodes to Delete:', file=sys.stderr)
    for idx, n in enumerate(nodes, 1):
        name = n.get('name') or n['node_id']
        seen = (n.get('last_seen') or 'Never')[:19]
        rec = n.get('record_count', 0)
        print(f'  [{idx}] {n[\"node_id\"]} ({name}) - Last seen: {seen} ({rec:,} telemetry records)', file=sys.stderr)
    print('  [q] Cancel / Quit\n', file=sys.stderr)
    sys.exit(0)
except Exception as e:
    sys.exit(1)
" 2>&1)
                    echo "$PICKED_ID"
                    if echo "$PICKED_ID" | grep -q "NO_NODES"; then
                        echo -e "${YELLOW}[INFO] No sensor nodes registered on the server.${NC}"
                        return 0
                    fi

                    read -r -p "Enter number or Node ID to delete [q to cancel]: " USER_PICK
                    if [ "$USER_PICK" = "q" ] || [ "$USER_PICK" = "Q" ] || [ -z "$USER_PICK" ]; then
                        echo -e "${YELLOW}[ABORT] Deletion cancelled.${NC}"
                        return 0
                    fi

                    # Resolve numeric choice or string
                    RESOLVED_ID=$(python3 -c "
import json
try:
    nodes = json.loads('''$NODES_JSON''')
    pick = '$USER_PICK'.strip()
    if pick.isdigit() and 1 <= int(pick) <= len(nodes):
        print(nodes[int(pick)-1]['node_id'])
    else:
        print(pick)
except Exception:
    print('$USER_PICK')
")
                    NODE_ID="$RESOLVED_ID"
                else
                    echo -e "${RED}[!] Error: Specify node ID to delete: $0 node delete <NODE_ID> [--purge]${NC}"
                    exit 1
                fi
            fi

            # Fetch details for the selected node
            NODE_INFO=$(curl -s "${HEADER_AUTH[@]}" "http://localhost:${PORT}/api/v1/nodes" 2>/dev/null || echo "[]")
            DETAILS=$(python3 -c "
import json
try:
    nodes = json.loads('''$NODE_INFO''')
    match = next((n for n in nodes if n['node_id'] == '$NODE_ID'), None)
    if match:
        name = match.get('name') or match['node_id']
        seen = (match.get('last_seen') or 'Never')[:19]
        rec = match.get('record_count', 0)
        print(f'{name}|{seen}|{rec}')
    else:
        print('NOT_FOUND')
except Exception:
    print('ERROR')
")

            if [ "$DETAILS" = "NOT_FOUND" ]; then
                echo -e "${RED}[!] Node '${NODE_ID}' not found on server.${NC}"
                exit 1
            fi

            STATION_NAME=$(echo "$DETAILS" | cut -d'|' -f1)
            LAST_SEEN=$(echo "$DETAILS" | cut -d'|' -f2)
            REC_COUNT=$(echo "$DETAILS" | cut -d'|' -f3)

            echo ""
            echo -e "${BOLD}Target Node Details:${NC}"
            echo -e "  Node ID:             ${CYAN}${NODE_ID}${NC}"
            echo -e "  Station Name:        ${CYAN}${STATION_NAME}${NC}"
            echo -e "  Last Seen:           ${CYAN}${LAST_SEEN}${NC}"
            echo -e "  Telemetry Records:   ${YELLOW}${REC_COUNT}${NC}"
            echo ""

            # Telemetry Safety Prompt if not explicitly set
            if [ "$PURGE_EXPLICIT" -eq 0 ] && [ "$AUTO_YES" -eq 0 ] && [ -t 0 ]; then
                echo -e "${BOLD}Telemetry Data Preservation:${NC}"
                echo -e "  ${GREEN}[1] Keep historical telemetry${NC} in database (removes node registration only) [DEFAULT - SAFE]"
                echo -e "  ${RED}[2] Permanently delete ALL ${REC_COUNT} telemetry records${NC} from database"
                echo ""
                read -r -p "Select option [1/2, default 1]: " TEL_CHOICE
                if [ "$TEL_CHOICE" = "2" ]; then
                    PURGE_FLAG="true"
                    echo -e "${RED}[!] Telemetry purge ENABLED.${NC}"
                else
                    PURGE_FLAG="false"
                    echo -e "${GREEN}[+] Telemetry records will be preserved.${NC}"
                fi
                echo ""
            fi

            # Final Confirmation Prompt
            if [ "$AUTO_YES" -eq 0 ] && [ -t 0 ]; then
                read -r -p "Are you sure you want to delete node '${NODE_ID}'? [y/N]: " CONFIRM
                if [[ ! "$CONFIRM" =~ ^[yY]([eE][sS])?$ ]]; then
                    echo -e "${YELLOW}[ABORT] Deletion cancelled. No changes made.${NC}"
                    return 0
                fi
            fi

            echo "Deleting node '${NODE_ID}' (Purge telemetry: ${PURGE_FLAG})..."
            RESPONSE=$(curl -s -w "\n%{http_code}" -X DELETE "http://localhost:${PORT}/api/v1/nodes/${NODE_ID}?purge_telemetry=${PURGE_FLAG}" \
                "${HEADER_AUTH[@]}")
            HTTP_BODY=$(echo "$RESPONSE" | head -n -1)
            HTTP_CODE=$(echo "$RESPONSE" | tail -n 1)

            if [ "$HTTP_CODE" -eq 200 ]; then
                echo -e "${GREEN}[+] Node '${NODE_ID}' deleted successfully!${NC}"
                echo -e "    Details: ${HTTP_BODY}"
            else
                echo -e "${RED}[!] Failed to delete node '${NODE_ID}' (HTTP ${HTTP_CODE})${NC}"
                echo -e "    Response: ${HTTP_BODY}"
                exit 1
            fi
            ;;

        prune)
            DAYS=""
            PURGE_FLAG="false"
            PURGE_EXPLICIT=0
            AUTO_YES=0

            while [ $# -gt 0 ]; do
                case "$1" in
                    --days|-d)
                        DAYS="$2"
                        shift 2
                        ;;
                    --purge|-p)
                        PURGE_FLAG="true"
                        PURGE_EXPLICIT=1
                        shift
                        ;;
                    --yes|-y)
                        AUTO_YES=1
                        shift
                        ;;
                    --help|-h)
                        show_node_help
                        return 0
                        ;;
                    *)
                        if [[ "$1" =~ ^[0-9]+$ ]] && [ -z "$DAYS" ]; then
                            DAYS="$1"
                        fi
                        shift
                        ;;
                esac
            done

            # Interactive days prompt if omitted
            if [ -z "$DAYS" ]; then
                if [ -t 0 ]; then
                    read -r -p "Enter inactivity threshold in days [default 30]: " USER_DAYS
                    DAYS="${USER_DAYS:-30}"
                else
                    DAYS="30"
                fi
            fi

            print_header
            echo -e "${BOLD}Scanning for candidate nodes inactive for > ${DAYS} days...${NC}"
            
            # --- DRY RUN / CANDIDATE PREVIEW ---
            CANDIDATES_JSON=$(curl -s "${HEADER_AUTH[@]}" "http://localhost:${PORT}/api/v1/nodes/prune/candidates?days=${DAYS}" 2>/dev/null || echo '{"candidate_count":0,"candidates":[]}')
            
            PREVIEW_OUTPUT=$(python3 -c "
import sys, json
try:
    data = json.loads('''$CANDIDATES_JSON''')
    count = data.get('candidate_count', 0)
    candidates = data.get('candidates', [])
    total_rec = data.get('total_records', 0)
    
    if count == 0:
        print('NO_CANDIDATES')
        sys.exit(0)

    print(f'FOUND|{count}|{total_rec}')
    print('=' * 95)
    print(f' {\"NODE ID\":<14} | {\"STATION NAME\":<20} | {\"LAST SEEN\":<20} | {\"TELEMETRY RECORDS\":<18}')
    print('=' * 95)
    for c in candidates:
        nid = c.get('node_id', 'UNKNOWN')
        name = c.get('name') or '-'
        seen = (c.get('last_seen') or 'Never')[:19]
        rec = f'{c.get(\"record_count\", 0):,} records'
        print(f' {nid:<14} | {name:<20} | {seen:<20} | {rec:<18}')
    print('=' * 95)
except Exception as e:
    print(f'ERROR|{e}')
    sys.exit(1)
")

            if echo "$PREVIEW_OUTPUT" | grep -q "NO_CANDIDATES"; then
                echo -e "\n${GREEN}[INFO] No nodes have been inactive for > ${DAYS} days. Nothing to prune.${NC}\n"
                return 0
            fi

            SUMMARY_LINE=$(echo "$PREVIEW_OUTPUT" | head -n 1)
            TABLE_LINES=$(echo "$PREVIEW_OUTPUT" | tail -n +2)
            CAND_COUNT=$(echo "$SUMMARY_LINE" | cut -d'|' -f2)
            TOTAL_RECORDS=$(echo "$SUMMARY_LINE" | cut -d'|' -f3)

            echo ""
            echo "$TABLE_LINES"
            echo -e "${YELLOW}${BOLD}Candidates:${NC} ${CYAN}${CAND_COUNT} node(s)${NC} | Total Historical Telemetry: ${YELLOW}${TOTAL_RECORDS} records${NC}"
            echo ""

            # Telemetry Preservation Prompt if not explicitly set via flag
            if [ "$PURGE_EXPLICIT" -eq 0 ] && [ "$AUTO_YES" -eq 0 ] && [ -t 0 ]; then
                echo -e "${BOLD}Telemetry Data Preservation Options:${NC}"
                echo -e "  ${GREEN}[1] KEEP historical telemetry${NC} in database (removes node registrations only) [DEFAULT - SAFE]"
                echo -e "  ${RED}[2] Permanently PURGE all ${TOTAL_RECORDS} telemetry records${NC} for these nodes"
                echo ""
                read -r -p "Select option [1/2, default 1]: " TEL_CHOICE
                if [ "$TEL_CHOICE" = "2" ]; then
                    PURGE_FLAG="true"
                    echo -e "${RED}[!] Telemetry purge ENABLED.${NC}"
                else
                    PURGE_FLAG="false"
                    echo -e "${GREEN}[+] Telemetry records will be preserved.${NC}"
                fi
                echo ""
            fi

            # Final Confirmation Prompt
            if [ "$AUTO_YES" -eq 0 ] && [ -t 0 ]; then
                read -r -p "Are you sure you want to prune these ${CAND_COUNT} inactive node(s)? [y/N]: " CONFIRM
                if [[ ! "$CONFIRM" =~ ^[yY]([eE][sS])?$ ]]; then
                    echo -e "${YELLOW}[ABORT] Pruning cancelled. No database changes were made.${NC}"
                    return 0
                fi
            fi

            echo ""
            echo "Executing prune for nodes inactive > ${DAYS} days (Purge telemetry: ${PURGE_FLAG})..."
            RESPONSE=$(curl -s -w "\n%{http_code}" -X DELETE "http://localhost:${PORT}/api/v1/nodes/prune?days=${DAYS}&purge_telemetry=${PURGE_FLAG}" \
                "${HEADER_AUTH[@]}")
            HTTP_BODY=$(echo "$RESPONSE" | head -n -1)
            HTTP_CODE=$(echo "$RESPONSE" | tail -n 1)

            if [ "$HTTP_CODE" -eq 200 ]; then
                echo -e "${GREEN}[+] Pruning complete!${NC}"
                echo -e "    Summary: ${HTTP_BODY}"
            else
                echo -e "${RED}[!] Failed to prune inactive nodes (HTTP ${HTTP_CODE})${NC}"
                echo -e "    Response: ${HTTP_BODY}"
                exit 1
            fi
            ;;

        update)
            if [ -f "${SCRIPT_DIR}/../scripts/update_node.py" ]; then
                python3 "${SCRIPT_DIR}/../scripts/update_node.py" --server "http://localhost:${PORT}" "$@"
            else
                echo -e "${RED}[!] Error: scripts/update_node.py not found.${NC}"
                exit 1
            fi
            ;;

        help|--help|-h)
            show_node_help
            ;;

        *)
            echo -e "${RED}[!] Unknown node subcommand: '$ACTION'${NC}"
            echo ""
            show_node_help
            exit 1
            ;;
    esac
}

# Command Router
case "${1:-status}" in
    status)
        show_status
        ;;
    start)
        start_services
        ;;
    stop)
        stop_services
        ;;
    restart)
        restart_services
        ;;
    logs)
        tail_logs
        ;;
    backup)
        backup_db
        ;;
    test)
        test_server
        ;;
    export)
        export_data "${2:-csv}"
        ;;
    nodes|node)
        shift
        manage_nodes "$@"
        ;;
    prune)
        shift
        manage_nodes prune "$@"
        ;;
    help|--help|-h)
        show_help
        ;;
    *)
        echo -e "${RED}[!] Unknown command: '$1'${NC}"
        echo ""
        show_help
        exit 1
        ;;
esac
