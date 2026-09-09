#ifndef BLE_EGRESS_H
#define BLE_EGRESS_H

#include <Arduino.h>
#include "TelemetryPacket.h"

class BLEEgress {
public:
    static void begin(const char* deviceName = "MAG_GATEWAY");
    static void broadcast(const GatewayAdvPacket &pkt, uint32_t durationMs = 1000);
    static void poll();
    static bool isBroadcasting();
    static bool isConnected();
    static bool isInitialized();

private:
    static bool _initialized;
    static uint32_t _lastBroadcastMs;
};

#endif // BLE_EGRESS_H
