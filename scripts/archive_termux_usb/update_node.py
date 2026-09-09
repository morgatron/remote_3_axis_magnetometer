#!/usr/bin/env python3
"""
Node Metadata & Location Update Tool (`scripts/update_node.py`)

CLI utility to query and update sensor node metadata (station name, geographic coordinates,
elevation, installation notes, and baseline field offsets) on the Central Data Server.

Usage Examples:
  # 1. Update location and name via flags
  python3 scripts/update_node.py --node NODE_3A8 --name "North Ridge" --lat -33.8568 --lon 151.2153 --elev 42.5

  # 2. Update site notes only
  python3 scripts/update_node.py --node NODE_3A8 --notes "Mounted on wooden tripod, 10m from fence"

  # 3. List all registered nodes and their current coordinates
  python3 scripts/update_node.py --list

  # 4. Interactive update mode (lists nodes and prompts for values)
  python3 scripts/update_node.py -i

  # 5. Remote server target with API key
  python3 scripts/update_node.py --server http://192.168.1.100:8000 --api-key secret123 --node NODE_01 --lat -33.85
"""

import sys
import os
import argparse
import json

try:
    import requests
except ImportError:
    print("[ERROR] 'requests' package is required. Install via: pip install requests")
    sys.exit(1)


def get_default_server_url() -> str:
    return os.environ.get("CENTRAL_SERVER_URL", "http://localhost:8000").rstrip("/")


def get_default_api_key() -> str:
    return os.environ.get("API_KEY", "")


def get_headers(api_key: str) -> dict:
    headers = {"Content-Type": "application/json"}
    if api_key:
        headers["X-API-Key"] = api_key
    return headers


def fetch_nodes(server_url: str, api_key: str = "") -> list:
    """Fetches all registered nodes from the central server."""
    url = f"{server_url}/api/v1/nodes"
    try:
        res = requests.get(url, headers=get_headers(api_key), timeout=5.0)
        if res.status_code == 200:
            return res.json()
        elif res.status_code in (401, 403):
            print(f"[ERROR] Authentication failed (HTTP {res.status_code}). Provide a valid --api-key or export API_KEY.")
            sys.exit(1)
        else:
            print(f"[ERROR] Failed to fetch nodes from {url} (HTTP {res.status_code}): {res.text}")
            sys.exit(1)
    except requests.exceptions.ConnectionError:
        print(f"[ERROR] Could not connect to central server at {server_url}.")
        print("        Ensure central_service/server.py is running or specify --server <URL>.")
        sys.exit(1)
    except Exception as e:
        print(f"[ERROR] An unexpected error occurred: {e}")
        sys.exit(1)


def print_nodes_table(nodes: list):
    """Displays registered nodes in a clean formatted ASCII table."""
    if not nodes:
        print("\n[INFO] No sensor nodes registered on the server yet.")
        print("       Nodes appear automatically once they transmit their first telemetry sample.\n")
        return

    print("\n" + "=" * 95)
    print(f" {'NODE ID':<12} | {'STATION NAME':<18} | {'LATITUDE':<10} | {'LONGITUDE':<10} | {'ELEV (m)':<8} | {'LAST SEEN':<19}")
    print("=" * 95)
    for n in nodes:
        nid = n.get("node_id", "UNKNOWN")
        name = n.get("name") or "-"
        lat = f"{n['lat']:.5f}" if n.get("lat") is not None else "-"
        lon = f"{n['lon']:.5f}" if n.get("lon") is not None else "-"
        elev = f"{n['elevation_m']:.1f}" if n.get("elevation_m") is not None else "-"
        last_seen = (n.get("last_seen") or "-")[:19]
        print(f" {nid:<12} | {name:<18} | {lat:<10} | {lon:<10} | {elev:<8} | {last_seen:<19}")
    print("=" * 95 + "\n")


def update_node_metadata(server_url: str, payload: dict, api_key: str = "") -> bool:
    """Sends node metadata update payload to server."""
    url = f"{server_url}/api/v1/nodes/update"
    try:
        res = requests.post(url, json=payload, headers=get_headers(api_key), timeout=5.0)
        if res.status_code in (200, 201):
            return True
        elif res.status_code in (401, 403):
            print(f"[ERROR] Authentication failed (HTTP {res.status_code}). Provide a valid --api-key or export API_KEY.")
            return False
        else:
            print(f"[ERROR] Update failed (HTTP {res.status_code}): {res.text}")
            return False
    except Exception as e:
        print(f"[ERROR] Failed to send update to {url}: {e}")
        return False


def delete_node_api(server_url: str, node_id: str, purge_telemetry: bool = False, api_key: str = "") -> bool:
    """Deletes a node from the server, optionally purging historical telemetry."""
    url = f"{server_url}/api/v1/nodes/{node_id}?purge_telemetry={'true' if purge_telemetry else 'false'}"
    try:
        res = requests.delete(url, headers=get_headers(api_key), timeout=5.0)
        if res.status_code == 200:
            data = res.json()
            tel_del = data.get("telemetry_deleted", 0)
            print(f"[SUCCESS] Node '{node_id}' deleted successfully.")
            if purge_telemetry:
                print(f"          Purged {tel_del} telemetry records.")
            return True
        elif res.status_code == 404:
            print(f"[ERROR] Node '{node_id}' not found on server.")
            return False
        elif res.status_code in (401, 403):
            print(f"[ERROR] Authentication failed (HTTP {res.status_code}). Provide a valid --api-key or export API_KEY.")
            return False
        else:
            print(f"[ERROR] Delete failed (HTTP {res.status_code}): {res.text}")
            return False
    except Exception as e:
        print(f"[ERROR] Failed to delete node: {e}")
        return False


def prune_inactive_nodes_api(server_url: str, days: int, purge_telemetry: bool = False, api_key: str = "") -> bool:
    """Prunes nodes inactive for more than N days."""
    url = f"{server_url}/api/v1/nodes/prune?days={days}&purge_telemetry={'true' if purge_telemetry else 'false'}"
    try:
        res = requests.delete(url, headers=get_headers(api_key), timeout=10.0)
        if res.status_code == 200:
            data = res.json()
            pruned_count = data.get("pruned_count", 0)
            pruned_nodes = data.get("pruned_nodes", [])
            tel_del = data.get("telemetry_deleted", 0)
            if pruned_count > 0:
                print(f"[SUCCESS] Pruned {pruned_count} inactive node(s) (> {days} days inactive): {', '.join(pruned_nodes)}")
                if purge_telemetry:
                    print(f"          Purged {tel_del} associated telemetry records.")
            else:
                print(f"[INFO] No nodes found inactive for > {days} days. Nothing to prune.")
            return True
        elif res.status_code in (401, 403):
            print(f"[ERROR] Authentication failed (HTTP {res.status_code}). Provide a valid --api-key or export API_KEY.")
            return False
        else:
            print(f"[ERROR] Pruning failed (HTTP {res.status_code}): {res.text}")
            return False
    except Exception as e:
        print(f"[ERROR] Failed to prune nodes: {e}")
        return False


def run_interactive(server_url: str, api_key: str):
    """Interactive wizard to select a node and prompt for metadata fields."""
    nodes = fetch_nodes(server_url, api_key)
    print_nodes_table(nodes)

    if not nodes:
        node_id = input("Enter new Node ID to register (e.g. NODE_3A8): ").strip()
        if not node_id:
            print("[ABORT] No node ID entered.")
            return
        curr = {}
    else:
        node_ids = [n["node_id"] for n in nodes]
        print("Available nodes: " + ", ".join(node_ids))
        node_id = input(f"Enter Node ID to update [{node_ids[0]}]: ").strip()
        if not node_id:
            node_id = node_ids[0]

        curr = next((n for n in nodes if n["node_id"] == node_id), {})

    print(f"\n--- Updating Node: {node_id} (Press Enter to keep existing value) ---")

    # Name
    curr_name = curr.get("name") or ""
    val = input(f" Station Name [{curr_name}]: ").strip()
    name = val if val else (curr_name if curr_name else None)

    # Latitude
    curr_lat = str(curr.get("lat")) if curr.get("lat") is not None else ""
    val = input(f" Latitude (-90.0 to 90.0) [{curr_lat}]: ").strip()
    lat = float(val) if val else (curr.get("lat") if curr_lat else None)

    # Longitude
    curr_lon = str(curr.get("lon")) if curr.get("lon") is not None else ""
    val = input(f" Longitude (-180.0 to 180.0) [{curr_lon}]: ").strip()
    lon = float(val) if val else (curr.get("lon") if curr_lon else None)

    # Elevation
    curr_elev = str(curr.get("elevation_m")) if curr.get("elevation_m") is not None else ""
    val = input(f" Elevation in meters [{curr_elev}]: ").strip()
    elev = float(val) if val else (curr.get("elevation_m") if curr_elev else None)

    # Notes
    curr_notes = curr.get("notes") or ""
    val = input(f" Deployment Notes [{curr_notes}]: ").strip()
    notes = val if val else (curr_notes if curr_notes else None)

    payload = {
        "node_id": node_id,
        "name": name,
        "lat": lat,
        "lon": lon,
        "elevation_m": elev,
        "notes": notes
    }

    # Remove None values
    payload = {k: v for k, v in payload.items() if v is not None}

    print("\nSending Update Payload:")
    print(json.dumps(payload, indent=2))

    if update_node_metadata(server_url, payload, api_key):
        print(f"\n[SUCCESS] Node '{node_id}' updated successfully on {server_url}!\n")


def main():
    parser = argparse.ArgumentParser(
        description="Update node metadata, geographic location, and notes on the Central Data Server."
    )
    parser.add_argument("-n", "--node", help="Node ID to update (e.g. NODE_3A8)")
    parser.add_argument("--name", help="Human-readable station name")
    parser.add_argument("--lat", type=float, help="Latitude in decimal degrees (-90.0 to 90.0)")
    parser.add_argument("--lon", type=float, help="Longitude in decimal degrees (-180.0 to 180.0)")
    parser.add_argument("--elev", "--elevation", type=float, dest="elevation", help="Elevation in meters")
    parser.add_argument("--notes", help="Installation / survey site notes")
    parser.add_argument("--baseline-x", type=float, help="Baseline magnetic X offset (nT)")
    parser.add_argument("--baseline-y", type=float, help="Baseline magnetic Y offset (nT)")
    parser.add_argument("--baseline-z", type=float, help="Baseline magnetic Z offset (nT)")
    parser.add_argument("-l", "--list", action="store_true", help="List all registered nodes and their current metadata")
    parser.add_argument("-i", "--interactive", action="store_true", help="Run interactive prompt wizard")
    parser.add_argument("--delete", metavar="NODE_ID", help="Delete a specific node from the server")
    parser.add_argument("--prune-inactive", type=int, metavar="DAYS", help="Prune nodes inactive for more than N days (e.g. --prune-inactive 30)")
    parser.add_argument("--purge-telemetry", "--purge", action="store_true", help="Also purge historical telemetry records when deleting/pruning")
    parser.add_argument("-s", "--server", default=get_default_server_url(), help=f"Central Server URL (default: {get_default_server_url()})")
    parser.add_argument("-k", "--api-key", default=get_default_api_key(), help="API Key for authenticated central servers")

    args = parser.parse_args()

    # Prune inactive nodes mode
    if args.prune_inactive is not None:
        if args.prune_inactive < 1:
            print("[ERROR] --prune-inactive DAYS must be at least 1 day.")
            sys.exit(1)
        if not prune_inactive_nodes_api(args.server, args.prune_inactive, args.purge_telemetry, args.api_key):
            sys.exit(1)
        return

    # Delete single node mode
    if args.delete:
        if not delete_node_api(args.server, args.delete, args.purge_telemetry, args.api_key):
            sys.exit(1)
        return

    # List mode
    if args.list:
        nodes = fetch_nodes(args.server, args.api_key)
        print_nodes_table(nodes)
        return

    # Interactive mode if requested or if no arguments provided
    if args.interactive or (not args.node and len(sys.argv) == 1):
        run_interactive(args.server, args.api_key)
        return

    # Direct argument mode
    if not args.node:
        print("[ERROR] --node / -n <NODE_ID> is required. Use --list to see available nodes or -i for interactive mode.")
        sys.exit(1)

    if args.lat is not None and (args.lat < -90.0 or args.lat > 90.0):
        print(f"[ERROR] Latitude {args.lat} is out of valid range (-90.0 to 90.0).")
        sys.exit(1)

    if args.lon is not None and (args.lon < -180.0 or args.lon > 180.0):
        print(f"[ERROR] Longitude {args.lon} is out of valid range (-180.0 to 180.0).")
        sys.exit(1)

    payload = {"node_id": args.node}
    if args.name is not None:
        payload["name"] = args.name
    if args.lat is not None:
        payload["lat"] = args.lat
    if args.lon is not None:
        payload["lon"] = args.lon
    if args.elevation is not None:
        payload["elevation_m"] = args.elevation
    if args.notes is not None:
        payload["notes"] = args.notes
    if args.baseline_x is not None:
        payload["baseline_x"] = args.baseline_x
    if args.baseline_y is not None:
        payload["baseline_y"] = args.baseline_y
    if args.baseline_z is not None:
        payload["baseline_z"] = args.baseline_z

    if len(payload) == 1:
        print(f"[WARNING] No fields specified to update for node '{args.node}'.")
        print("          Provide --name, --lat, --lon, --elev, or --notes.")
        sys.exit(0)

    print(f"Updating metadata for node '{args.node}' on {args.server}...")
    if update_node_metadata(args.server, payload, args.api_key):
        print(f"[SUCCESS] Node '{args.node}' updated successfully.")
        print(json.dumps(payload, indent=2))
    else:
        sys.exit(1)


if __name__ == "__main__":
    main()
