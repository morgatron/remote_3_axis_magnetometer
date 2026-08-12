#ifndef BLE_RECEIVER_H
#define BLE_RECEIVER_H

#include <Arduino.h>
#include <NimBLEDevice.h>
#include "../../include/TelemetryPacket.h"
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

#if defined(NIMBLE_CPP_VERSION) && NIMBLE_CPP_VERSION >= 20000
// NimBLE 2.x Callback Implementation
class BLEReceiverCallbacks: public NimBLEScanCallbacks {
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

        // 1. Check for SensorBatchPacket (10-sample Extended Advertising burst)
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

                    // De-duplicate duplicate wireless scan results of the exact same batch payload
                    if (!nodeTracker.recordBatchSeen(item.node_id, item.mac, item.rssi, batch.start_ts_ms, batch.sample_count, item.vbat)) {
                        Serial.print("DEBUG: batch already seen"); Serial.println(batch.start_ts_ms);
                        return; // Batch already processed! Discard duplicate scan.
                    }
                    Serial.print("DEBUG: new batch!"); Serial.println(batch.start_ts_ms);

                    for (uint8_t i = 0; i < batch.sample_count; i++) {
                        uint32_t sample_ts = batch.start_ts_ms + (i * batch.sample_interval_ms);
                        item.timestamp_us = (uint64_t)sample_ts * 1000ULL;
                        item.x = (float)batch.samples[i].x_nT;
                        item.y = (float)batch.samples[i].y_nT;
                        item.z = (float)batch.samples[i].z_nT;

                        bleRxCount = bleRxCount + 1;
                        snprintf(item.line, sizeof(item.line), "%s,%llu,%.2f,%.2f,%.2f,%06X,%.1f,%.2f,%d\n",
                                 item.node_id, (unsigned long long)item.timestamp_us,
                                 item.x, item.y, item.z, (unsigned int)(item.status & 0xFFFFFF),
                                 item.temp, item.vbat, item.rssi);
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
                    item.timestamp_us = (uint64_t)pkt.timestamp_ms * 1000ULL;
                    item.x = pkt.x_nT; item.y = pkt.y_nT; item.z = pkt.z_nT;
                    item.status = pkt.status; item.temp = 0.0f; item.vbat = 0.0f;

                    bool isNewSample = nodeTracker.recordPacket(item.node_id, item.mac, item.rssi, item.x, item.y, item.z, item.temp, item.vbat, "BLE", pkt.timestamp_ms);
                    if (isNewSample) {
                        bleRxCount = bleRxCount + 1;
                        snprintf(item.line, sizeof(item.line), "%s,%llu,%.2f,%.2f,%.2f,%06X,%.1f,%.2f,%d\n",
                                 item.node_id, (unsigned long long)item.timestamp_us,
                                 item.x, item.y, item.z, (unsigned int)(item.status & 0xFFFFFF),
                                 item.temp, item.vbat, item.rssi);

                        if (telemetryQueue) xQueueSend(telemetryQueue, &item, 0);
                    }
                    break;
                }
            }
        }
    }
};
#else
// NimBLE 1.x Callback Implementation
class BLEReceiverCallbacks: public NimBLEAdvertisedDeviceCallbacks {
    void onResult(NimBLEAdvertisedDevice* advertisedDevice) override {
        std::string name = advertisedDevice->getName();
        std::string manuData = advertisedDevice->getManufacturerData();
        int rssi = advertisedDevice->getRSSI();
        NimBLEAddress addr = advertisedDevice->getAddress();
        const uint8_t* nativeAddr = (const uint8_t*)addr.getNative();
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

        if (mlen >= sizeof(SensorBinaryPacket)) {
            for (size_t offset = 0; offset <= mlen - sizeof(SensorBinaryPacket); offset++) {
                SensorBinaryPacket pkt;
                memcpy(&pkt, mptr + offset, sizeof(pkt));
                if (isValidSensorPacket(pkt)) {
                    if (pkt.device_id[0] != '\0') {
                        strncpy(item.node_id, pkt.device_id, sizeof(item.node_id) - 1);
                    }
                    item.timestamp_us = (uint64_t)pkt.timestamp_ms * 1000ULL;
                    item.x = pkt.x_nT; item.y = pkt.y_nT; item.z = pkt.z_nT;
                    item.status = pkt.status; item.temp = 0.0f; item.vbat = 0.0f;

                    bool isNewSample = nodeTracker.recordPacket(item.node_id, item.mac, item.rssi, item.x, item.y, item.z, item.temp, item.vbat, "BLE", pkt.timestamp_ms);
                    if (isNewSample) {
                        bleRxCount = bleRxCount + 1;
                        snprintf(item.line, sizeof(item.line), "%s,%llu,%.2f,%.2f,%.2f,%06X,%.1f,%.2f,%d\n",
                                 item.node_id, (unsigned long long)item.timestamp_us,
                                 item.x, item.y, item.z, (unsigned int)(item.status & 0xFFFFFF),
                                 item.temp, item.vbat, item.rssi);

                        if (telemetryQueue) xQueueSend(telemetryQueue, &item, 0);
                    }
                    break;
                }
            }
        } else if (manuData.length() > 10) {
            char rawStr[192];
            int cpyLen = (manuData.length() < sizeof(rawStr) - 1) ? manuData.length() : (sizeof(rawStr) - 1);
            memcpy(rawStr, manuData.data(), cpyLen);
            rawStr[cpyLen] = '\0';

            char devBuf[32] = {0};
            unsigned long long ts = 0;
            float x = 0, y = 0, z = 0;
            unsigned int st = 0;
            if (sscanf(rawStr, "%31[^,],%llu,%f,%f,%f,%x", devBuf, &ts, &x, &y, &z, &st) >= 5) {
                strncpy(item.node_id, devBuf, sizeof(item.node_id) - 1);
                item.timestamp_us = ts;
                item.x = x; item.y = y; item.z = z; item.status = st;

                snprintf(item.line, sizeof(item.line), "%s,%llu,%.2f,%.2f,%.2f,%06X,%.1f,%.2f,%d\n",
                         item.node_id, (unsigned long long)item.timestamp_us,
                         item.x, item.y, item.z, (unsigned int)(item.status & 0xFFFFFF),
                         0.0f, 0.0f, item.rssi);

                nodeTracker.recordPacket(item.node_id, item.mac, item.rssi, item.x, item.y, item.z, 0, 0, "BLE");
                if (telemetryQueue) xQueueSend(telemetryQueue, &item, 0);
            }
        }
    }
};
#endif

class BLEReceiver {
public:
    static void begin() {
        NimBLEDevice::init("ESP32_Receiver_Node");
        NimBLEScan* pScan = NimBLEDevice::getScan();
#if defined(NIMBLE_CPP_VERSION) && NIMBLE_CPP_VERSION >= 20000
        pScan->setScanCallbacks(new BLEReceiverCallbacks(), true);
#else
        pScan->setAdvertisedDeviceCallbacks(new BLEReceiverCallbacks(), true);
#endif
        pScan->setDuplicateFilter(false);
        pScan->setActiveScan(true);
        pScan->setInterval(100);
        pScan->setWindow(97);
        pScan->start(0, false); // Start continuous scanning
        Serial.println(F("[BLE RECEIVER SUCCESS] Bluetooth LE Continuous Scanning Active (Coded PHY Enabled)"));
    }
};

#endif // BLE_RECEIVER_H
