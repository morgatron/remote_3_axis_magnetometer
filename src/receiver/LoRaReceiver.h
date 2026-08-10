#ifndef LORA_RECEIVER_H
#define LORA_RECEIVER_H

#include <Arduino.h>
#include "TelemetryPacket.h"
#include "NodeTracker.h"
#include "../../lib/LoRaStream/LoRaStream.h"

extern QueueHandle_t telemetryQueue;
extern NodeTracker nodeTracker;
extern volatile uint32_t loraRxCount;

class LoRaReceiver {
public:
    static bool begin(int cs = 5, int irq = 4, int rst = 14, int busy = 15, SPIClass *spiBus = nullptr) {
        bool ok = loraStream.begin(cs, irq, rst, busy, spiBus);
        if (ok) {
            loraStream.startReceive();
            Serial.println(F("[LORA RECEIVER SUCCESS] Sub-GHz SX1262 Continuous Reception Active (868/915 MHz)"));
        }
        return ok;
    }

    static void handlePackets() {
        if (!loraStream.isInitialized()) return;

        uint8_t buf[256];
        int rssi = 0;
        float snr = 0.0f;
        size_t len = loraStream.readPacket(buf, sizeof(buf) - 1, rssi, snr);
        if (len == 0) return;

        loraRxCount++;
        TelemetryItem item;
        memset(&item, 0, sizeof(item));
        item.rssi = rssi;
        strncpy(item.protocol, "LORA_SX1262", sizeof(item.protocol) - 1);

        if (len == sizeof(SensorBinaryPacket)) {
            SensorBinaryPacket pkt;
            memcpy(&pkt, buf, sizeof(pkt));
            if (pkt.device_id[0] != '\0') {
                strncpy(item.node_id, pkt.device_id, sizeof(item.node_id) - 1);
            } else {
                strncpy(item.node_id, "LORA_NODE", sizeof(item.node_id) - 1);
            }
            item.timestamp_us = pkt.timestamp_us;
            item.x = pkt.x_nT; item.y = pkt.y_nT; item.z = pkt.z_nT;
            item.status = pkt.status; item.temp = pkt.temp; item.vbat = pkt.vbat_mv / 1000.0f;

            snprintf(item.line, sizeof(item.line), "%s,%llu,%.2f,%.2f,%.2f,%06X,%.1f,%.2f,%d\n",
                     item.node_id, (unsigned long long)item.timestamp_us,
                     item.x, item.y, item.z, (unsigned int)(item.status & 0xFFFFFF),
                     item.temp, item.vbat, item.rssi);

        } else {
            buf[len] = '\0';
            char devBuf[32] = {0};
            unsigned long long ts = 0;
            float x = 0, y = 0, z = 0;
            unsigned int st = 0;
            float temp = 0, vbat = 0;

            if (sscanf((char*)buf, "%31[^,],%llu,%f,%f,%f,%x,%f,%f", devBuf, &ts, &x, &y, &z, &st, &temp, &vbat) >= 6) {
                strncpy(item.node_id, devBuf, sizeof(item.node_id) - 1);
                item.timestamp_us = ts;
                item.x = x; item.y = y; item.z = z; item.status = st;
                item.temp = temp; item.vbat = vbat;

                snprintf(item.line, sizeof(item.line), "%s,%llu,%.2f,%.2f,%.2f,%06X,%.1f,%.2f,%d\n",
                         item.node_id, (unsigned long long)item.timestamp_us,
                         item.x, item.y, item.z, (unsigned int)(item.status & 0xFFFFFF),
                         item.temp, item.vbat, item.rssi);
            } else {
                strncpy(item.node_id, "LORA_RAW", sizeof(item.node_id) - 1);
                snprintf(item.line, sizeof(item.line), "%s\n", (char*)buf);
            }
        }

        nodeTracker.recordPacket(item.node_id, item.mac, item.rssi, item.x, item.y, item.z, item.temp, item.vbat, "LORA_SX1262");

        if (telemetryQueue) {
            xQueueSend(telemetryQueue, &item, 0);
        }
    }
};

#endif // LORA_RECEIVER_H
