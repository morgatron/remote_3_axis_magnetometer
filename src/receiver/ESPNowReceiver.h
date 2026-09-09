#ifndef ESP_NOW_RECEIVER_H
#define ESP_NOW_RECEIVER_H

#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include "ITelemetryReceiver.h"
#include "TelemetryPacket.h"
#include "ReceiverContext.h"
#include "NodeTracker.h"
#include "IngestionPipeline.h"

class ESPNowReceiver : public ITelemetryReceiver {
public:
    explicit ESPNowReceiver(uint8_t channel = 1) : _channel(channel) {}

    void begin() override {
        if (egressModeConfig == MODE_EGRESS_SERIAL || egressModeConfig == MODE_EGRESS_BLE) {
            Serial.println(F("[ESP-NOW] Disabled in BLE/Serial mode (Wi-Fi radio OFF)."));
            return;
        }

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
        espnowRxCount = espnowRxCount + 1;

        if (len == sizeof(SensorBatchPacket)) {
            SensorBatchPacket batch;
            memcpy(&batch, incomingData, sizeof(batch));
            IngestionPipeline::ingestBatch(batch, mac, rssi, "ESP-NOW");
        } else if (len == sizeof(SensorBinaryPacket)) {
            SensorBinaryPacket pkt;
            memcpy(&pkt, incomingData, sizeof(pkt));
            IngestionPipeline::ingestSingle(pkt, mac, rssi, "ESP-NOW");
        } else {
            char rawStr[192];
            int cpyLen = min(len, (int)sizeof(rawStr) - 1);
            memcpy(rawStr, incomingData, cpyLen);
            rawStr[cpyLen] = '\0';
            IngestionPipeline::ingestCsv(rawStr, mac, rssi, "ESP-NOW");
        }
    }
};

#endif // ESP_NOW_RECEIVER_H
