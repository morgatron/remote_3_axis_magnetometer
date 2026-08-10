#ifndef RELAY_EGRESS_H
#define RELAY_EGRESS_H

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <HTTPClient.h>
#include "TelemetryPacket.h"

enum EgressMode {
    MODE_EGRESS_SERIAL = 0,
    MODE_EGRESS_WIFI = 1,
    MODE_EGRESS_BOTH = 2
};

extern QueueHandle_t telemetryQueue;
extern uint8_t egressModeConfig;
extern String wifiSSID;
extern String wifiPass;
extern String targetServerIP;
extern uint16_t targetServerPort;
extern bool wifiRelayConnected;
extern volatile uint32_t relayedPacketCount;

class RelayEgress {
public:
    static void begin() {
        xTaskCreatePinnedToCore(
            relayTask,
            "RelayEgressTask",
            8192,
            NULL,
            2,
            NULL,
            1
        );
        Serial.println(F("[RELAY EGRESS SUCCESS] FreeRTOS Egress Relay Task Spawned on Core 1"));
    }

private:
    static void relayTask(void *pvParameters) {
        TelemetryItem item;
        WiFiUDP egressUdp;
        static char batchBuf[2048];
        static size_t batchLen = 0;
        static uint32_t lastBatchFlushMs = 0;

        for (;;) {
            if (xQueueReceive(telemetryQueue, &item, pdMS_TO_TICKS(50)) == pdTRUE) {
                relayedPacketCount++;

                // 1. Serial Egress (USB CDC output to host PC / gateway.py)
                if (egressModeConfig == MODE_EGRESS_SERIAL || egressModeConfig == MODE_EGRESS_BOTH) {
                    Serial.print(item.line);
                }

                // 2. WiFi Egress (Forward to Central Server or UDP listener over WiFi network)
                if ((egressModeConfig == MODE_EGRESS_WIFI || egressModeConfig == MODE_EGRESS_BOTH) && wifiRelayConnected) {
                    size_t lineLen = strlen(item.line);
                    if (batchLen + lineLen >= sizeof(batchBuf) - 1) {
                        flushWiFiBatch(egressUdp, batchBuf, batchLen);
                        batchLen = 0;
                    }
                    memcpy(batchBuf + batchLen, item.line, lineLen);
                    batchLen += lineLen;
                    batchBuf[batchLen] = '\0';
                }
            }

            // Flush pending WiFi batch every 500ms
            if (batchLen > 0 && (millis() - lastBatchFlushMs >= 500)) {
                if (wifiRelayConnected) {
                    flushWiFiBatch(egressUdp, batchBuf, batchLen);
                }
                batchLen = 0;
                lastBatchFlushMs = millis();
            }
        }
    }

    static void flushWiFiBatch(WiFiUDP &udp, const char* buf, size_t len) {
        if (len == 0 || targetServerIP.length() == 0) return;

        IPAddress addr;
        if (addr.fromString(targetServerIP)) {
            udp.beginPacket(addr, targetServerPort);
            udp.write((const uint8_t*)buf, len);
            udp.endPacket();
        }
    }
};

#endif // RELAY_EGRESS_H
