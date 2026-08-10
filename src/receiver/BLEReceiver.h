#ifndef BLE_RECEIVER_H
#define BLE_RECEIVER_H

#include <Arduino.h>
#include <NimBLEDevice.h>
#include "TelemetryPacket.h"
#include "NodeTracker.h"

extern QueueHandle_t telemetryQueue;
extern NodeTracker nodeTracker;
extern volatile uint32_t bleRxCount;

#if defined(NIMBLE_CPP_VERSION) && NIMBLE_CPP_VERSION >= 20000
// NimBLE 2.x Callback Implementation
class BLEReceiverCallbacks: public NimBLEScanCallbacks {
    void onResult(const NimBLEAdvertisedDevice* advertisedDevice) override {
        bleRxCount = bleRxCount + 1;
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

        if (manuData.length() == sizeof(SensorBinaryPacket)) {
            SensorBinaryPacket pkt;
            memcpy(&pkt, manuData.data(), sizeof(pkt));
            if (pkt.device_id[0] != '\0') {
                strncpy(item.node_id, pkt.device_id, sizeof(item.node_id) - 1);
            }
            item.timestamp_us = pkt.timestamp_us;
            item.x = pkt.x_nT; item.y = pkt.y_nT; item.z = pkt.z_nT;
            item.status = pkt.status; item.temp = pkt.temp; item.vbat = pkt.vbat_mv / 1000.0f;

            snprintf(item.line, sizeof(item.line), "%s,%llu,%.2f,%.2f,%.2f,%06X,%.1f,%.2f,%d\n",
                     item.node_id, (unsigned long long)item.timestamp_us,
                     item.x, item.y, item.z, (unsigned int)(item.status & 0xFFFFFF),
                     item.temp, item.vbat, item.rssi);

            nodeTracker.recordPacket(item.node_id, item.mac, item.rssi, item.x, item.y, item.z, item.temp, item.vbat, "BLE");
            if (telemetryQueue) xQueueSend(telemetryQueue, &item, 0);

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
        bleRxCount = bleRxCount + 1;
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

        if (manuData.length() == sizeof(SensorBinaryPacket)) {
            SensorBinaryPacket pkt;
            memcpy(&pkt, manuData.data(), sizeof(pkt));
            if (pkt.device_id[0] != '\0') {
                strncpy(item.node_id, pkt.device_id, sizeof(item.node_id) - 1);
            }
            item.timestamp_us = pkt.timestamp_us;
            item.x = pkt.x_nT; item.y = pkt.y_nT; item.z = pkt.z_nT;
            item.status = pkt.status; item.temp = pkt.temp; item.vbat = pkt.vbat_mv / 1000.0f;

            snprintf(item.line, sizeof(item.line), "%s,%llu,%.2f,%.2f,%.2f,%06X,%.1f,%.2f,%d\n",
                     item.node_id, (unsigned long long)item.timestamp_us,
                     item.x, item.y, item.z, (unsigned int)(item.status & 0xFFFFFF),
                     item.temp, item.vbat, item.rssi);

            nodeTracker.recordPacket(item.node_id, item.mac, item.rssi, item.x, item.y, item.z, item.temp, item.vbat, "BLE");
            if (telemetryQueue) xQueueSend(telemetryQueue, &item, 0);

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
        pScan->setScanCallbacks(new BLEReceiverCallbacks());
#else
        pScan->setAdvertisedDeviceCallbacks(new BLEReceiverCallbacks(), true);
#endif
        pScan->setActiveScan(true);
        pScan->setInterval(100);
        pScan->setWindow(99);
        pScan->setMaxResults(0); // Continuous scan without caching limit

        pScan->start(0, false); // Start continuous scanning
        Serial.println(F("[BLE RECEIVER SUCCESS] Bluetooth LE Continuous Scanning Active (Coded PHY Enabled)"));
    }
};

#endif // BLE_RECEIVER_H
