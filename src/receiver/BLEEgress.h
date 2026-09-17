#ifndef BLE_EGRESS_H
#define BLE_EGRESS_H

#include <Arduino.h>
#include "TelemetryPacket.h"

enum DiagEvent {
    DIAG_EVENT_HEARTBEAT = 0,
    DIAG_EVENT_WINDOW_TIMEOUT = 1,
    DIAG_EVENT_LOST_SYNC_DISCOVERY = 2,
    DIAG_EVENT_PERIODIC_LOOKOUT = 3,
    DIAG_EVENT_SYNC_ACQUIRED = 4
};

class BLEEgress {
public:
    static void begin(const char* deviceName = "MAG_GATEWAY");
    static void broadcast(const GatewayAdvPacket &pkt, uint32_t durationMs = 200);
    static void broadcastDiagnostic(uint8_t eventCode, uint8_t stateCode, uint8_t missCount, const char* targetNode = nullptr, uint16_t metricVal = 0);
    static void poll();
    static bool isBroadcasting();
    static bool isConnected();
    static bool isInitialized();

private:
    static bool _initialized;
    static uint32_t _lastBroadcastMs;
};

#endif // BLE_EGRESS_H
