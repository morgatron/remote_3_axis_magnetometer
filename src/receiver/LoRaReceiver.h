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
    LoRaReceiver(int cs = 8, int irq = 14, int rst = 12, int busy = 13, int sck = 9, int miso = 11, int mosi = 10, SPIClass *spiBus = nullptr)
        : _cs(cs), _irq(irq), _rst(rst), _busy(busy), _sck(sck), _miso(miso), _mosi(mosi), _spiBus(spiBus) {}

    void begin() override {
        bool ok = loraStream.begin(_cs, _irq, _rst, _busy, _sck, _miso, _mosi, _spiBus);
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
        // Semtech SX1262 True Signal Power formula: When SNR < 0, true RSSI = RSSI(noise floor) + SNR
        int trueRssi = (snr < 0.0f) ? (int)round((float)rssi + snr) : rssi;
        item.rssi = trueRssi;
        strncpy(item.protocol, "LORA_SX1262", sizeof(item.protocol) - 1);

        if (len == sizeof(SensorBatchPacket)) {
            SensorBatchPacket batch;
            memcpy(&batch, buf, sizeof(batch));
            if (batch.device_id[0] != '\0') {
                strncpy(item.node_id, batch.device_id, sizeof(item.node_id) - 1);
            } else {
                strncpy(item.node_id, "LORA_NODE", sizeof(item.node_id) - 1);
            }
            item.status = batch.status;
            item.temp = 0.0f;
            item.vbat = (float)batch.vbat_mv / 1000.0f;

            // Deterministic Time-on-Air for 139-byte batch packet at SF7 / 125 kHz is ~230 ms
            constexpr uint32_t LORA_BATCH_TOA_MS = 230;
            uint32_t now_ms = millis();
            uint32_t total_delay_ms = batch.latest_sample_age_ms + LORA_BATCH_TOA_MS;
            uint32_t latest_sample_ts_ms = (now_ms >= total_delay_ms) ? (now_ms - total_delay_ms) : 0;
            uint32_t oldest_sample_offset = (batch.sample_count > 0) ? (batch.sample_count - 1) * batch.sample_interval_ms : 0;
            uint32_t oldest_sample_ts_ms = (latest_sample_ts_ms >= oldest_sample_offset) ? (latest_sample_ts_ms - oldest_sample_offset) : 0;

            if (!nodeTracker.recordBatchSeen(item.node_id, item.mac, item.rssi, oldest_sample_ts_ms, batch.sample_count, item.vbat, "LORA_SX1262")) {
                return;
            }

            for (uint8_t i = 0; i < batch.sample_count; i++) {
                uint32_t offset_from_newest = (batch.sample_count - 1 - i) * batch.sample_interval_ms;
                uint32_t sample_ts_ms = (latest_sample_ts_ms >= offset_from_newest) ? (latest_sample_ts_ms - offset_from_newest) : 0;
                item.timestamp_us = (uint64_t)sample_ts_ms * 1000ULL;
                item.x = (float)batch.samples[i].x_nT;
                item.y = (float)batch.samples[i].y_nT;
                item.z = (float)batch.samples[i].z_nT;

                item.formatCsvLine();
                if (telemetryQueue) xQueueSend(telemetryQueue, &item, 0);
            }
            return;
        } else if (len == sizeof(SensorBinaryPacket)) {
            SensorBinaryPacket pkt;
            memcpy(&pkt, buf, sizeof(pkt));
            if (pkt.device_id[0] != '\0') {
                strncpy(item.node_id, pkt.device_id, sizeof(item.node_id) - 1);
            } else {
                strncpy(item.node_id, "LORA_NODE", sizeof(item.node_id) - 1);
            }
            // Subtract packet age and deterministic Time-on-Air (AU915 SF7/125kHz 26-byte payload ~58ms)
            constexpr uint32_t LORA_TOA_MS = 58;
            uint32_t now_ms = millis();
            uint32_t total_delay_ms = pkt.packet_age_ms + LORA_TOA_MS;
            uint32_t sample_ts_ms = (now_ms >= total_delay_ms) ? (now_ms - total_delay_ms) : 0;

            item.timestamp_us = (uint64_t)sample_ts_ms * 1000ULL;
            item.x = pkt.x_nT; item.y = pkt.y_nT; item.z = pkt.z_nT;
            item.status = pkt.status; item.temp = 0.0f; item.vbat = 0.0f;

            item.formatCsvLine();
            nodeTracker.recordPacket(item.node_id, item.mac, item.rssi, item.x, item.y, item.z, item.temp, item.vbat, "LORA_SX1262", sample_ts_ms);
            if (telemetryQueue) xQueueSend(telemetryQueue, &item, 0);
            return;
        } else {
            buf[len] = '\0';
            if (TelemetryItem::parseCsvLine((char*)buf, item)) {
                item.formatCsvLine();
            } else {
                strncpy(item.node_id, "LORA_RAW", sizeof(item.node_id) - 1);
                snprintf(item.line, sizeof(item.line), "%s\n", (char*)buf);
            }
            nodeTracker.recordPacket(item.node_id, item.mac, item.rssi, item.x, item.y, item.z, item.temp, item.vbat, "LORA_SX1262");
            if (telemetryQueue) xQueueSend(telemetryQueue, &item, 0);
        }
    }

    const char* getName() const override {
        return "LoRa";
    }

private:
    int _cs, _irq, _rst, _busy;
    int _sck, _miso, _mosi;
    SPIClass *_spiBus;
};

#endif // LORA_RECEIVER_H
