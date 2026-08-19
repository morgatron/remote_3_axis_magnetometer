#ifndef BLE_RECEIVER_H
#define BLE_RECEIVER_H

#include <Arduino.h>
#include <NimBLEDevice.h>
#include "ITelemetryReceiver.h"
#include "TelemetryPacket.h"
#include "NodeTracker.h"

extern QueueHandle_t telemetryQueue;
extern NodeTracker nodeTracker;
extern volatile uint32_t bleRxCount;

inline bool isKnownSensorNodeId(const char *id) {
    if (!id || id[0] == '\0') return false;
    if (strncmp(id, "NODE_", 5) == 0) return true;
    if (strncmp(id, "MOCK_", 5) == 0) return true;
    if (strncmp(id, "MAG_", 4) == 0) return true;
    return false;
}

inline bool isValidSensorPacket(const SensorBinaryPacket &pkt) {
    if (!isKnownSensorNodeId(pkt.device_id)) return false;
    if (isnan(pkt.x_nT) || isnan(pkt.y_nT) || isnan(pkt.z_nT)) return false;
    if (fabsf(pkt.x_nT) > 10000000.0f || fabsf(pkt.y_nT) > 10000000.0f || fabsf(pkt.z_nT) > 10000000.0f) return false;
    return true;
}

inline bool isValidBatchPacket(const SensorBatchPacket &batch) {
    if (!isKnownSensorNodeId(batch.device_id)) return false;
    if (batch.sample_count == 0 || batch.sample_count > 10) return false;
    return true;
}

/**
 * @brief NimBLE Scan Callbacks for processing incoming Coded PHY extended advertising bursts.
 */
class BLEReceiverCallbacks : public NimBLEScanCallbacks {
    void onResult(const NimBLEAdvertisedDevice* advertisedDevice) override {
        std::string name = advertisedDevice->getName();
        std::string manuData = advertisedDevice->getManufacturerData();
        int rssi = advertisedDevice->getRSSI();
        NimBLEAddress addr = advertisedDevice->getAddress();
        const uint8_t* nativeAddr = (const uint8_t*)addr.getBase();
        
        uint8_t mac[6] = {0};
        if (nativeAddr) {
            for (int i = 0; i < 6; i++) mac[i] = nativeAddr[5 - i];
        }

        TelemetryItem item;
        memset(&item, 0, sizeof(item));
        memcpy(item.mac, mac, 6);
        item.rssi = rssi;
        strncpy(item.protocol, "BLE", sizeof(item.protocol) - 1);

        if (name.length() > 0) {
            strncpy(item.node_id, name.c_str(), sizeof(item.node_id) - 1);
        } else {
            snprintf(item.node_id, sizeof(item.node_id), "BLE_%02X%02X%02X", mac[3], mac[4], mac[5]);
        }

        size_t mlen = manuData.length();
        const char* mptr = manuData.data();

        // 1. Process 10-sample SensorBatchPacket (Extended Advertising Coded PHY burst)
        if (mlen >= sizeof(SensorBatchPacket)) {
            for (size_t offset = 0; offset <= mlen - sizeof(SensorBatchPacket); offset++) {
                SensorBatchPacket batch;
                memcpy(&batch, mptr + offset, sizeof(batch));
                if (isValidBatchPacket(batch)) {
                    if (batch.device_id[0] != '\0') {
                        strncpy(item.node_id, batch.device_id, sizeof(item.node_id) - 1);
                    }
                    item.status = batch.status;
                    item.temp = 0.0f;
                    item.vbat = (float)batch.vbat_mv / 1000.0f;

                    uint32_t now_ms = millis();
                    uint32_t latest_sample_ts_ms = (now_ms >= batch.latest_sample_age_ms) ? (now_ms - batch.latest_sample_age_ms) : 0;
                    uint32_t oldest_sample_offset = (batch.sample_count > 0) ? (batch.sample_count - 1) * batch.sample_interval_ms : 0;
                    uint32_t oldest_sample_ts_ms = (latest_sample_ts_ms >= oldest_sample_offset) ? (latest_sample_ts_ms - oldest_sample_offset) : 0;

                    if (!nodeTracker.recordBatchSeen(item.node_id, item.mac, item.rssi, oldest_sample_ts_ms, batch.sample_count, item.vbat)) {
                        return; // Already processed duplicate scan
                    }

                    for (uint8_t i = 0; i < batch.sample_count; i++) {
                        uint32_t offset_from_newest = (batch.sample_count - 1 - i) * batch.sample_interval_ms;
                        uint32_t sample_ts_ms = (latest_sample_ts_ms >= offset_from_newest) ? (latest_sample_ts_ms - offset_from_newest) : 0;
                        item.timestamp_us = (uint64_t)sample_ts_ms * 1000ULL;
                        item.x = (float)batch.samples[i].x_nT;
                        item.y = (float)batch.samples[i].y_nT;
                        item.z = (float)batch.samples[i].z_nT;

                        bleRxCount++;
                        item.formatCsvLine();
                        if (telemetryQueue) xQueueSend(telemetryQueue, &item, 0);
                    }
                    return;
                }
            }
        }

        // 2. Fallback check for single SensorBinaryPacket
        if (mlen >= sizeof(SensorBinaryPacket)) {
            for (size_t offset = 0; offset <= mlen - sizeof(SensorBinaryPacket); offset++) {
                SensorBinaryPacket pkt;
                memcpy(&pkt, mptr + offset, sizeof(pkt));
                if (isValidSensorPacket(pkt)) {
                    if (pkt.device_id[0] != '\0') {
                        strncpy(item.node_id, pkt.device_id, sizeof(item.node_id) - 1);
                    }
                    uint32_t now_ms = millis();
                    uint32_t sample_ts_ms = (now_ms >= pkt.packet_age_ms) ? (now_ms - pkt.packet_age_ms) : 0;
                    item.timestamp_us = (uint64_t)sample_ts_ms * 1000ULL;
                    item.x = pkt.x_nT; item.y = pkt.y_nT; item.z = pkt.z_nT;
                    item.status = pkt.status; item.temp = 0.0f; item.vbat = 0.0f;

                    bool isNewSample = nodeTracker.recordPacket(item.node_id, item.mac, item.rssi, item.x, item.y, item.z, item.temp, item.vbat, "BLE", sample_ts_ms);
                    if (isNewSample) {
                        bleRxCount++;
                        item.formatCsvLine();
                        if (telemetryQueue) xQueueSend(telemetryQueue, &item, 0);
                    }
                    break;
                }
            }
        }
    }
};

class BLEReceiver : public ITelemetryReceiver {
public:
    void begin() override {
        NimBLEDevice::init("MAG_GATEWAY_RECEIVER");
        NimBLEScan* pScan = NimBLEDevice::getScan();
        pScan->setScanCallbacks(new BLEReceiverCallbacks());
        pScan->setDuplicateFilter(false);
        pScan->setActiveScan(true);
        pScan->setInterval(100);
        pScan->setWindow(99);
        pScan->start(0, false);
        Serial.println(F("[BLE RECEIVER SUCCESS] Active LE Coded PHY scanning enabled."));
    }

    const char* getName() const override {
        return "BLE";
    }
};

#endif // BLE_RECEIVER_H
