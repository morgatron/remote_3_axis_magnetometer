#ifndef LORA_RECEIVER_H
#define LORA_RECEIVER_H

#include <Arduino.h>
#include "ITelemetryReceiver.h"
#include "TelemetryPacket.h"
#include "NodeTracker.h"
#include "../../lib/LoRaStream/LoRaStream.h"

extern QueueHandle_t telemetryQueue;
extern NodeTracker nodeTracker;
extern volatile uint32_t loraRxCount;

class LoRaReceiver : public ITelemetryReceiver {
public:
    LoRaReceiver(int cs = 5, int irq = 4, int rst = 14, int busy = 15, SPIClass *spiBus = nullptr)
        : _cs(cs), _irq(irq), _rst(rst), _busy(busy), _spiBus(spiBus) {}

    void begin() override {
        bool ok = loraStream.begin(_cs, _irq, _rst, _busy, _spiBus);
        if (ok) {
            loraStream.startReceive();
            Serial.println(F("[LORA RECEIVER SUCCESS] Sub-GHz SX1262 Continuous Reception Active (868/915 MHz)"));
        }
    }

    void poll() override {
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
            item.timestamp_us = (uint64_t)pkt.timestamp_ms * 1000ULL;
            item.x = pkt.x_nT; item.y = pkt.y_nT; item.z = pkt.z_nT;
            item.status = pkt.status; item.temp = 0.0f; item.vbat = 0.0f;

            item.formatCsvLine();
        } else {
            buf[len] = '\0';
            if (TelemetryItem::parseCsvLine((char*)buf, item)) {
                item.formatCsvLine();
            } else {
                strncpy(item.node_id, "LORA_RAW", sizeof(item.node_id) - 1);
                snprintf(item.line, sizeof(item.line), "%s\n", (char*)buf);
            }
        }

        nodeTracker.recordPacket(item.node_id, item.mac, item.rssi, item.x, item.y, item.z, item.temp, item.vbat, "LORA_SX1262");
        if (telemetryQueue) xQueueSend(telemetryQueue, &item, 0);
    }

    const char* getName() const override {
        return "LoRa";
    }

private:
    int _cs, _irq, _rst, _busy;
    SPIClass *_spiBus;
};

#endif // LORA_RECEIVER_H
