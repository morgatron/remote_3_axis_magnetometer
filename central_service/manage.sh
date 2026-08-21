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

function export_data() {
    FORMAT="${1:-csv}"
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
    ACTION="${1:-list}"
    shift || true

    HEADER_AUTH=()
    if [ -n "$API_KEY" ]; then
        HEADER_AUTH=(-H "X-API-Key: ${API_KEY}")
    fi

    case "$ACTION" in
        list)
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
        delete)
            NODE_ID="$1"
            if [ -z "$NODE_ID" ]; then
                echo -e "${RED}[!] Error: Specify node ID to delete: $0 node delete <NODE_ID> [--purge]${NC}"
                exit 1
            fi
            PURGE_FLAG="false"
            if [ "$2" = "--purge" ] || [ "$2" = "-p" ]; then
                PURGE_FLAG="true"
            fi

            echo "Deleting node '${NODE_ID}' (Purge telemetry: ${PURGE_FLAG})..."
            RESPONSE=$(curl -s -w "\n%{http_code}" -X DELETE "http://localhost:${PORT}/api/v1/nodes/${NODE_ID}?purge_telemetry=${PURGE_FLAG}" \
                "${HEADER_AUTH[@]}")
            HTTP_BODY=$(echo "$RESPONSE" | head -n -1)
            HTTP_CODE=$(echo "$RESPONSE" | tail -n 1)

            if [ "$HTTP_CODE" -eq 200 ]; then
                echo -e "${GREEN}[+] Node '${NODE_ID}' deleted successfully!${NC}"
                echo -e "    Response: ${HTTP_BODY}"
            else
                echo -e "${RED}[!] Failed to delete node '${NODE_ID}' (HTTP ${HTTP_CODE})${NC}"
                echo -e "    Response: ${HTTP_BODY}"
                exit 1
            fi
            ;;
        prune)
            DAYS="30"
            PURGE_FLAG="false"
            while [ $# -gt 0 ]; do
                case "$1" in
                    --days|-d)
                        DAYS="$2"
                        shift 2
                        ;;
                    --purge|-p)
                        PURGE_FLAG="true"
                        shift
                        ;;
                    *)
                        if [[ "$1" =~ ^[0-9]+$ ]]; then
                            DAYS="$1"
                        fi
                        shift
                        ;;
                esac
            done

            echo "Pruning inactive nodes older than ${DAYS} days (Purge telemetry: ${PURGE_FLAG})..."
            RESPONSE=$(curl -s -w "\n%{http_code}" -X DELETE "http://localhost:${PORT}/api/v1/nodes/prune?days=${DAYS}&purge_telemetry=${PURGE_FLAG}" \
                "${HEADER_AUTH[@]}")
            HTTP_BODY=$(echo "$RESPONSE" | head -n -1)
            HTTP_CODE=$(echo "$RESPONSE" | tail -n 1)

            if [ "$HTTP_CODE" -eq 200 ]; then
                echo -e "${GREEN}[+] Pruning complete!${NC}"
                echo -e "    Response: ${HTTP_BODY}"
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
        *)
            echo "Usage: $0 node {list|delete <NODE_ID> [--purge]|prune [--days <N>] [--purge]|update <args>}"
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
    *)
        echo "Usage: $0 {status|start|stop|restart|logs|backup|test|export [csv|parquet|npz]|node {list|delete|prune|update}}"
        exit 1
        ;;
esac
