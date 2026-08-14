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
    uint32_t last_sample_ts_ms;
    uint32_t last_batch_start_ts_ms;
    uint8_t  last_batch_sample_count;
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

    bool recordPacket(const char* device_id, const uint8_t* mac, int rssi,
                      float x, float y, float z, float temp, float vbat, const char* protocol, uint32_t sample_ts_ms = 0) {
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

        RemoteNodeInfo &node = _nodes[idx];

        // De-duplicate repeated wireless beacon scans of the exact same sample
        if (sample_ts_ms > 0 && sample_ts_ms == node.last_sample_ts_ms) {
            node.last_seen_ms = now;
            node.rssi = rssi;
            return false;
        }
        if (sample_ts_ms > 0) {
            node.last_sample_ts_ms = sample_ts_ms;
        }

        // Update node metrics
        node.last_seen_ms = now;
        node.packet_count++;
        node.rssi = rssi;
        node.last_x = x;
        node.last_y = y;
        node.last_z = z;
        if (temp != 0.0f) node.temp = temp;
        if (vbat != 0.0f) node.vbat = vbat;
        strncpy(node.protocol, protocol, sizeof(node.protocol) - 1);
        return true;
    }

    bool recordBatchSeen(const char* device_id, const uint8_t* mac, int rssi, uint32_t start_ts_ms, uint8_t sample_count, float vbat = 0.0f) {
        uint32_t now = millis();
        int idx = findNodeIndex(device_id, mac);

        if (idx < 0) {
            if (_nodeCount < MAX_TRACKED_NODES) {
                idx = _nodeCount++;
            } else {
                idx = findOldestNodeIndex();
            }
            memset(&_nodes[idx], 0, sizeof(RemoteNodeInfo));
            strncpy(_nodes[idx].node_id, device_id, sizeof(_nodes[idx].node_id) - 1);
            if (mac) memcpy(_nodes[idx].mac, mac, 6);
            _nodes[idx].first_seen_ms = now;
            _nodes[idx].active = true;
        }

        RemoteNodeInfo &node = _nodes[idx];

        // Handle sensor node reboot (timestamp reset/backward jump)
        if (node.last_sample_ts_ms > 0 && start_ts_ms < node.last_batch_start_ts_ms) {
            // Any backward jump in timestamp indicates a sensor node reboot/reset!
            node.last_sample_ts_ms = 0;
            node.last_batch_start_ts_ms = 0;
        }

        // De-duplicate repeated wireless scans of the exact same batch burst
        if (start_ts_ms > 0 && start_ts_ms == node.last_batch_start_ts_ms && sample_count == node.last_batch_sample_count) {
            node.last_seen_ms = now;
            node.rssi = rssi;
            return false; // Already processed this batch!
        }
        //Serial.print("new batch numsamples, time: "); Serial.print(sample_count); Serial.print(" "); Serial.println(start_ts_ms);
        //Serial.print("last batch numsamples, time: "); Serial.print(node.last_batch_sample_count); Serial.print(" "); Serial.println(node.last_batch_start_ts_ms);
        //Serial.print("last sample ts, last seen ts: "); Serial.print(node.last_sample_ts_ms); Serial.print(" "); Serial.println(node.last_batch_start_ts_ms);

        node.last_batch_start_ts_ms = start_ts_ms;
        node.last_batch_sample_count = sample_count;
        node.last_sample_ts_ms = start_ts_ms + (sample_count > 0 ? (sample_count - 1) * 1000 : 0);
        node.last_seen_ms = now;
        node.packet_count += sample_count;
        node.rssi = rssi;
        if (vbat > 0.0f) node.vbat = vbat;
        strncpy(node.protocol, "BLE", sizeof(node.protocol) - 1);
        return true; // New batch payload
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
