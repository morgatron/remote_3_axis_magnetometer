#ifndef RELAY_EGRESS_H
#define RELAY_EGRESS_H

#include <Arduino.h>
#include <WiFiUdp.h>
#include "TelemetryPacket.h"
#include "ReceiverContext.h"

class RelayEgress {
public:
    static void begin();

private:
    static void dispatchSerialWiFi(const TelemetryItem &item, WiFiUDP &egressUdp, char *batchBuf, size_t &batchLen);
    static void initAdvPacket(GatewayAdvPacket &advPkt, const TelemetryItem &item);
    static void appendSampleToAdvPacket(GatewayAdvPacket &advPkt, const TelemetryItem &item);
    static void relayTask(void *pvParameters);
    static void flushWiFiBatch(WiFiUDP &udp, const char* buf, size_t len);
};

#endif // RELAY_EGRESS_H
