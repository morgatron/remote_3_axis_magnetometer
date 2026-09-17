#ifndef NODE_TRACKER_H
#define NODE_TRACKER_H

#include <Arduino.h>

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
    int32_t  last_batch_sample0_x;
    int32_t  last_batch_sample0_y;
    int32_t  last_batch_sample0_z;
    float last_x;
    float last_y;
    float last_z;
    float temp;
    float vbat;
    char protocol[12]; // "ESP-NOW", "BLE", "WIFI_UDP"
    bool active;
    uint8_t consecutive_misses;
    uint32_t observed_period_ms;
};

class NodeTracker {
public:
    NodeTracker();

    bool recordPacket(const char* device_id, const uint8_t* mac, int rssi,
                      float x, float y, float z, float temp, float vbat, const char* protocol, uint32_t sample_ts_ms = 0);

    bool recordBatchSeen(const char* device_id, const uint8_t* mac, int rssi, uint32_t start_ts_ms, uint8_t sample_count, float vbat = 0.0f, const char* protocol = "BLE", int32_t s0_x = 0, int32_t s0_y = 0, int32_t s0_z = 0);

    void recordMiss(const char* device_id);
    void printNodeTable(Stream &out);
    int getNodeCount() const;
    int getActiveNodeCount() const;
    int getLastRssi() const;
    const char* getLastNodeId() const;
    bool getNextExpectedWake(uint32_t now, uint32_t leadTimeMs, uint32_t &outWakeMs, uint32_t &outTargetMs, char* outNodeId, size_t maxLen);

private:
    RemoteNodeInfo _nodes[MAX_TRACKED_NODES];
    int _nodeCount;

    int findNodeIndex(const char* device_id, const uint8_t* mac);
    int findOldestNodeIndex();
};

#endif // NODE_TRACKER_H
