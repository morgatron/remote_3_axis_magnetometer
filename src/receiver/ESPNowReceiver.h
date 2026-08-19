#ifndef ESP_NOW_RECEIVER_H
#define ESP_NOW_RECEIVER_H

#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include "ITelemetryReceiver.h"
#include "TelemetryPacket.h"
#include "NodeTracker.h"

extern QueueHandle_t telemetryQueue;
extern NodeTracker nodeTracker;
extern volatile uint32_t espnowRxCount;

class ESPNowReceiver : public ITelemetryReceiver {
public:
    explicit ESPNowReceiver(uint8_t channel = 1) : _channel(channel) {}

    void begin() override {
        if (WiFi.getMode() == WIFI_OFF) {
            WiFi.mode(WIFI_STA);
        }

        if (esp_now_init() != ESP_OK) {
            Serial.println(F("[ESP-NOW ERROR] Failed to initialize ESP-NOW protocol!"));
            return;
        }

        esp_now_register_recv_cb(OnDataRecv);
        Serial.printf("[ESP-NOW SUCCESS] Listening on WiFi Channel %d\r\n", _channel);
    }

    const char* getName() const override {
        return "ESP-NOW";
    }

private:
    uint8_t _channel;

#if defined(ESP_IDF_VERSION) && ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
    static void OnDataRecv(const esp_now_recv_info_t * recv_info, const uint8_t *incomingData, int len) {
        const uint8_t *mac = recv_info->src_addr;
        int rssi = (recv_info->rx_ctrl) ? recv_info->rx_ctrl->rssi : 0;
        processPacket(mac, incomingData, len, rssi);
    }
#else
    static void OnDataRecv(const uint8_t * mac, const uint8_t *incomingData, int len) {
        int rssi = 0;
        processPacket(mac, incomingData, len, rssi);
    }
#endif

    static void processPacket(const uint8_t *mac, const uint8_t *incomingData, int len, int rssi) {
        if (len <= 0 || incomingData == nullptr) return;
        espnowRxCount++;

        TelemetryItem item;
        memset(&item, 0, sizeof(item));
        if (mac) memcpy(item.mac, mac, 6);
        item.rssi = rssi;
        strncpy(item.protocol, "ESP-NOW", sizeof(item.protocol) - 1);

        if (len == sizeof(SensorBinaryPacket)) {
            SensorBinaryPacket pkt;
            memcpy(&pkt, incomingData, sizeof(pkt));

            if (pkt.device_id[0] != '\0') {
                strncpy(item.node_id, pkt.device_id, sizeof(item.node_id) - 1);
            } else {
                snprintf(item.node_id, sizeof(item.node_id), "NODE_%02X%02X%02X", mac[3], mac[4], mac[5]);
            }

            uint32_t now_ms = millis();
            uint32_t sample_ts_ms = (now_ms >= pkt.packet_age_ms) ? (now_ms - pkt.packet_age_ms) : 0;
            item.timestamp_us = (uint64_t)sample_ts_ms * 1000ULL;
            item.x = pkt.x_nT; item.y = pkt.y_nT; item.z = pkt.z_nT;
            item.status = pkt.status;

            bool isNewSample = nodeTracker.recordPacket(item.node_id, item.mac, item.rssi, item.x, item.y, item.z, item.temp, item.vbat, "ESP-NOW", sample_ts_ms);
            if (isNewSample) {
                item.formatCsvLine();
                if (telemetryQueue) xQueueSend(telemetryQueue, &item, 0);
            }
        } else {
            char rawStr[192];
            int cpyLen = min(len, (int)sizeof(rawStr) - 1);
            memcpy(rawStr, incomingData, cpyLen);
            rawStr[cpyLen] = '\0';

            if (TelemetryItem::parseCsvLine(rawStr, item)) {
                bool isNewSample = nodeTracker.recordPacket(item.node_id, item.mac, item.rssi, item.x, item.y, item.z, item.temp, item.vbat, "ESP-NOW");
                if (isNewSample) {
                    item.formatCsvLine();
                    if (telemetryQueue) xQueueSend(telemetryQueue, &item, 0);
                }
            }
        }
    }
};

#endif // ESP_NOW_RECEIVER_H
