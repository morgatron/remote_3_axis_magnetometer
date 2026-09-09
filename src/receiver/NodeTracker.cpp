#include "NodeTracker.h"
#include <WiFi.h>

NodeTracker::NodeTracker() : _nodeCount(0) {
    memset(_nodes, 0, sizeof(_nodes));
}

bool NodeTracker::recordPacket(const char* device_id, const uint8_t* mac, int rssi,
                               float x, float y, float z, float temp, float vbat, const char* protocol, uint32_t sample_ts_ms) {
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
    node.packet_count = node.packet_count + 1;
    node.rssi = rssi;
    node.last_x = x;
    node.last_y = y;
    node.last_z = z;
    if (temp != 0.0f) node.temp = temp;
    if (vbat != 0.0f) node.vbat = vbat;
    strncpy(node.protocol, protocol, sizeof(node.protocol) - 1);
    return true;
}

bool NodeTracker::recordBatchSeen(const char* device_id, const uint8_t* mac, int rssi, uint32_t start_ts_ms, uint8_t sample_count, float vbat, const char* protocol, int32_t s0_x, int32_t s0_y, int32_t s0_z) {
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
        node.last_sample_ts_ms = 0;
        node.last_batch_start_ts_ms = 0;
    }

    // De-duplicate repeated wireless scans of the exact same batch burst (within 3s window with identical payload)
    if (node.last_seen_ms > 0 && (now - node.last_seen_ms < 3000) &&
        sample_count == node.last_batch_sample_count &&
        s0_x == node.last_batch_sample0_x &&
        s0_y == node.last_batch_sample0_y &&
        s0_z == node.last_batch_sample0_z) {
        node.last_seen_ms = now;
        node.rssi = rssi;
        return false; // Already processed this batch!
    }

    // Track real-world measured batch cadence for closed-loop rendezvous tracking
    if (node.last_seen_ms > 0) {
        uint32_t dt = now - node.last_seen_ms;
        if (dt >= 9500 && dt <= 10500) {
            if (node.observed_period_ms == 0) {
                node.observed_period_ms = dt;
            } else {
                node.observed_period_ms = (node.observed_period_ms * 7 + dt) / 8;
            }
        } else if (dt >= 19000 && dt <= 21000) {
            uint32_t singleDt = dt / 2;
            if (node.observed_period_ms > 0) {
                node.observed_period_ms = (node.observed_period_ms * 7 + singleDt) / 8;
            }
        }
    }

    // Clamp observed period strictly to nominal 10.0s range (+/- 200 ms)
    if (node.observed_period_ms < 9800 || node.observed_period_ms > 10200) {
        node.observed_period_ms = 10000;
    }

    node.last_batch_start_ts_ms = start_ts_ms;
    node.last_batch_sample_count = sample_count;
    node.last_batch_sample0_x = s0_x;
    node.last_batch_sample0_y = s0_y;
    node.last_batch_sample0_z = s0_z;
    node.last_sample_ts_ms = start_ts_ms + (sample_count > 0 ? (sample_count - 1) * 1000 : 0);
    node.last_seen_ms = now;
    node.packet_count += sample_count;
    node.rssi = rssi;
    if (vbat > 0.0f) node.vbat = vbat;
    strncpy(node.protocol, protocol, sizeof(node.protocol) - 1);
    return true; // New batch payload
}

void NodeTracker::printNodeTable(Stream &out) {
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

int NodeTracker::getNodeCount() const {
    return _nodeCount;
}

int NodeTracker::getLastRssi() const {
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

const char* NodeTracker::getLastNodeId() const {
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

bool NodeTracker::getNextExpectedWake(uint32_t now, uint32_t leadTimeMs, uint32_t &outWakeMs, uint32_t &outTargetMs, char* outNodeId, size_t maxLen) {
    int bestIdx = -1;
    uint32_t earliestWake = UINT32_MAX;
    uint32_t bestTarget = 0;

    for (int i = 0; i < _nodeCount; i++) {
        if (!_nodes[i].active || _nodes[i].last_seen_ms == 0) continue;

        uint32_t period = (_nodes[i].observed_period_ms > 0) ? _nodes[i].observed_period_ms : 10000;
        uint32_t target = _nodes[i].last_seen_ms + period;

        // Project target into the future so target > now + leadTimeMs
        while (target <= now + leadTimeMs) {
            target += period;
        }

        uint32_t wake = target - leadTimeMs;
        if (wake < earliestWake) {
            earliestWake = wake;
            bestTarget = target;
            bestIdx = i;
        }
    }

    if (bestIdx >= 0) {
        outWakeMs = earliestWake;
        outTargetMs = bestTarget;
        if (outNodeId && maxLen > 0) {
            strncpy(outNodeId, _nodes[bestIdx].node_id, maxLen - 1);
            outNodeId[maxLen - 1] = '\0';
        }
        return true;
    }
    return false;
}

int NodeTracker::findNodeIndex(const char* device_id, const uint8_t* mac) {
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

int NodeTracker::findOldestNodeIndex() {
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
