#ifndef BLE_RECEIVER_H
#define BLE_RECEIVER_H

#include <Arduino.h>
#include <NimBLEDevice.h>
#include "TelemetryPacket.h"
#include "NodeTracker.h"

extern QueueHandle_t telemetryQueue;
extern NodeTracker nodeTracker;
extern volatile uint32_t bleRxCount;

inline bool isValidSensorPacket(const SensorBinaryPacket &pkt) {
    if (pkt.device_id[0] < 32 || pkt.device_id[0] > 126) {
        // Serial.printf("[FAIL ID0] 0x%02X\n", (uint8_t)pkt.device_id[0]);
        return false;
    }
    bool hasNull = false;
    for (size_t i = 0; i < sizeof(pkt.device_id); i++) {
        if (pkt.device_id[i] == '\0') {
            hasNull = true;
            break;
        }
        if (pkt.device_id[i] < 32 || pkt.device_id[i] > 126) {
            // Serial.printf("[FAIL CHAR] idx=%d 0x%02X\n", (int)i, (uint8_t)pkt.device_id[i]);
            return false;
        }
    }
    if (!hasNull) return false;
    if (isnan(pkt.x_nT) || isnan(pkt.y_nT) || isnan(pkt.z_nT)) return false;
    if (fabsf(pkt.x_nT) > 10000000.0f || fabsf(pkt.y_nT) > 10000000.0f || fabsf(pkt.z_nT) > 10000000.0f) return false;
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
        if (mlen >= sizeof(SensorBinaryPacket) && mlen <= sizeof(SensorBinaryPacket) + 4) {
            mptr += (mlen - sizeof(SensorBinaryPacket));
            mlen = sizeof(SensorBinaryPacket);
        }

        if (mlen == sizeof(SensorBinaryPacket)) {
            SensorBinaryPacket pkt;
            memcpy(&pkt, mptr, sizeof(pkt));
            if (!isValidSensorPacket(pkt)) return;
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
        if (mlen >= sizeof(SensorBinaryPacket) && mlen <= sizeof(SensorBinaryPacket) + 4) {
            mptr += (mlen - sizeof(SensorBinaryPacket));
            mlen = sizeof(SensorBinaryPacket);
        }

        if (mlen == sizeof(SensorBinaryPacket)) {
            SensorBinaryPacket pkt;
            memcpy(&pkt, mptr, sizeof(pkt));
            if (!isValidSensorPacket(pkt)) return;
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
        pScan->setWindow(99);
        pScan->setMaxResults(0); // Continuous scan without caching limit

        pScan->start(0, false); // Start continuous scanning
        Serial.println(F("[BLE RECEIVER SUCCESS] Bluetooth LE Continuous Scanning Active (Coded PHY Enabled)"));
    }
};

#endif // BLE_RECEIVER_H
