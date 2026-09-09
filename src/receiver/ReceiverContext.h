#ifndef RECEIVER_CONTEXT_H
#define RECEIVER_CONTEXT_H

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

enum EgressMode {
    MODE_EGRESS_SERIAL = 0,
    MODE_EGRESS_WIFI = 1,
    MODE_EGRESS_BOTH = 2,
    MODE_EGRESS_BLE = 3
};

class NodeTracker;

extern QueueHandle_t telemetryQueue;
extern NodeTracker nodeTracker;
extern bool g_debugScheduler;
extern bool g_dfsEnabled;

extern volatile uint32_t espnowRxCount;
extern volatile uint32_t bleRxCount;
extern volatile uint32_t udpRxCount;
extern volatile uint32_t loraRxCount;
extern volatile uint32_t relayedPacketCount;

extern uint8_t egressModeConfig;
extern String wifiSSID;
extern String wifiPass;
extern String targetServerIP;
extern uint16_t targetServerPort;
extern uint8_t espNowChannel;
extern bool wifiRelayConnected;
extern String apSSID;
extern uint32_t lastOledActivityMs;
extern bool oledScreenActive;

#endif // RECEIVER_CONTEXT_H
