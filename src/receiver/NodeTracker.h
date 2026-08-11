#ifndef NODE_TRACKER_H
#define NODE_TRACKER_H

#include <Arduino.h>
#include <WiFi.h>

#define MAX_TRACKED_NODES 64

struct RemoteNodeInfo {
    char node_id[32];
    uint8_t mac[6];
    uint32_t first_seen_ms;
    uint32_t last_seen_ms;
    uint32_t packet_count;
    int rssi;
    float last_x;
    float last_y;
    float last_z;
    float temp;
    float vbat;
    char protocol[12]; // "ESP-NOW", "BLE", "WIFI_UDP"
    bool active;
};

class NodeTracker {
public:
    NodeTracker() : _nodeCount(0) {
        memset(_nodes, 0, sizeof(_nodes));
    }

    void recordPacket(const char* device_id, const uint8_t* mac, int rssi, 
                      float x, float y, float z, float temp, float vbat, const char* protocol) {
        uint32_t now = millis();
        int idx = findNodeIndex(device_id, mac);
        
        if (idx < 0) {
            // Allocate new node entry
            if (_nodeCount < MAX_TRACKED_NODES) {
                idx = _nodeCount++;
            } else {
                // Evict oldest inactive node if table is full
                idx = findOldestNodeIndex();
            }
            
            memset(&_nodes[idx], 0, sizeof(RemoteNodeInfo));
            strncpy(_nodes[idx].node_id, device_id, sizeof(_nodes[idx].node_id) - 1);
            if (mac) {
                memcpy(_nodes[idx].mac, mac, 6);
            }
            _nodes[idx].first_seen_ms = now;
            _nodes[idx].packet_count = 0;
            _nodes[idx].active = true;
        }

        // Update node metrics
        RemoteNodeInfo &node = _nodes[idx];
        node.last_seen_ms = now;
        node.packet_count++;
        node.rssi = rssi;
        node.last_x = x;
        node.last_y = y;
        node.last_z = z;
        if (temp != 0.0f) node.temp = temp;
        if (vbat != 0.0f) node.vbat = vbat;
        strncpy(node.protocol, protocol, sizeof(node.protocol) - 1);
    }

    void printNodeTable(Stream &out) {
        out.println(F("\r\n=============================================================================================="));
        out.println(F("                               ACTIVE REMOTE SENSOR NODES TABLE                               "));
        out.println(F("=============================================================================================="));
        out.println(F("ID               MAC Address        Protocol   RSSI (dBm)  Packets   Last Seen (s)  Vbat (V)"));
        out.println(F("----------------------------------------------------------------------------------------------"));

        uint32_t now = millis();
        int activeCount = 0;

        for (int i = 0; i < _nodeCount; i++) {
            if (!_nodes[i].active) continue;
            activeCount++;
            float ageSec = (now - _nodes[i].last_seen_ms) / 1000.0f;
            
            char macStr[18];
            snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
                     _nodes[i].mac[0], _nodes[i].mac[1], _nodes[i].mac[2],
                     _nodes[i].mac[3], _nodes[i].mac[4], _nodes[i].mac[5]);

            char row[120];
            snprintf(row, sizeof(row), "%-16s %-18s %-10s %-11d %-9lu %-14.1f %-7.2f",
                     _nodes[i].node_id,
                     macStr,
                     _nodes[i].protocol,
                     _nodes[i].rssi,
                     (unsigned long)_nodes[i].packet_count,
                     ageSec,
                     _nodes[i].vbat);
            out.println(row);
        }

        if (activeCount == 0) {
            out.println(F("  (No remote sensor nodes detected yet)"));
        }
        out.println(F("=============================================================================================="));
        out.printf("Total Active Nodes: %d / %d\r\n\r\n", activeCount, MAX_TRACKED_NODES);
    }

    int getNodeCount() const { return _nodeCount; }

    int getLastRssi() const {
        if (_nodeCount == 0) return (WiFi.status() == WL_CONNECTED) ? WiFi.RSSI() : 0;
        int latestIdx = 0;
        uint32_t latestTime = 0;
        for (int i = 0; i < _nodeCount; i++) {
            if (_nodes[i].active && _nodes[i].last_seen_ms >= latestTime) {
                latestTime = _nodes[i].last_seen_ms;
                latestIdx = i;
            }
        }
        return _nodes[latestIdx].rssi;
    }

    const char* getLastNodeId() const {
        if (_nodeCount == 0) return "NONE";
        int latestIdx = 0;
        uint32_t latestTime = 0;
        for (int i = 0; i < _nodeCount; i++) {
            if (_nodes[i].active && _nodes[i].last_seen_ms >= latestTime) {
                latestTime = _nodes[i].last_seen_ms;
                latestIdx = i;
            }
        }
        return _nodes[latestIdx].node_id;
    }

private:
    RemoteNodeInfo _nodes[MAX_TRACKED_NODES];
    int _nodeCount;

    int findNodeIndex(const char* device_id, const uint8_t* mac) {
        for (int i = 0; i < _nodeCount; i++) {
            if (_nodes[i].active) {
                if (device_id && strlen(device_id) > 0 && strcmp(_nodes[i].node_id, device_id) == 0) {
                    return i;
                }
                if (mac && memcmp(_nodes[i].mac, mac, 6) == 0) {
                    return i;
                }
            }
        }
        return -1;
    }

    int findOldestNodeIndex() {
        int oldestIdx = 0;
        uint32_t oldestTime = _nodes[0].last_seen_ms;
        for (int i = 1; i < _nodeCount; i++) {
            if (_nodes[i].last_seen_ms < oldestTime) {
                oldestTime = _nodes[i].last_seen_ms;
                oldestIdx = i;
            }
        }
        return oldestIdx;
    }
};

extern NodeTracker nodeTracker;

#endif // NODE_TRACKER_H
