#ifndef ESP_NOW_RECEIVER_H
#define ESP_NOW_RECEIVER_H

#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include "TelemetryPacket.h"
#include "NodeTracker.h"

extern QueueHandle_t telemetryQueue;
extern NodeTracker nodeTracker;
extern volatile uint32_t espnowRxCount;

class ESPNowReceiver {
public:
    static void begin(uint8_t channel = 1) {
        // Set WiFi channel for ESP-NOW listener without resetting WiFi mode if already in WIFI_AP_STA or WIFI_STA
        if (WiFi.getMode() == WIFI_OFF) {
            WiFi.mode(WIFI_STA);
        }
        
        esp_wifi_set_promiscuous(true);
        esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
        esp_wifi_set_promiscuous(false);

        if (esp_now_init() != ESP_OK) {
            Serial.println(F("[ESP-NOW ERROR] Failed to initialize ESP-NOW protocol!"));
            return;
        }

        #if defined(ESP_IDF_VERSION) && ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
        esp_now_register_recv_cb(OnDataRecv);
        #else
        esp_now_register_recv_cb(OnDataRecv);
        #endif

        Serial.printf("[ESP-NOW SUCCESS] Listening on WiFi Channel %d\r\n", channel);
    }

private:
    #if defined(ESP_IDF_VERSION) && ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
    static void OnDataRecv(const esp_now_recv_info_t * recv_info, const uint8_t *incomingData, int len) {
        const uint8_t *mac = recv_info->src_addr;
        int rssi = (recv_info->rx_ctrl) ? recv_info->rx_ctrl->rssi : 0;
        processPacket(mac, incomingData, len, rssi);
    }
    #else
    static void OnDataRecv(const uint8_t * mac, const uint8_t *incomingData, int len) {
        int rssi = 0; // Default RSSI if unavailable in legacy core
        processPacket(mac, incomingData, len, rssi);
    }
    #endif

    static void processPacket(const uint8_t *mac, const uint8_t *incomingData, int len, int rssi) {
        if (len <= 0 || incomingData == nullptr) return;
        espnowRxCount = espnowRxCount + 1;

        TelemetryItem item;
        memset(&item, 0, sizeof(item));
        if (mac) memcpy(item.mac, mac, 6);
        item.rssi = rssi;
        strncpy(item.protocol, "ESP-NOW", sizeof(item.protocol) - 1);

        if (len == sizeof(SensorBinaryPacket)) {
            // Binary Struct Packet
            SensorBinaryPacket pkt;
            memcpy(&pkt, incomingData, sizeof(pkt));

            if (pkt.device_id[0] != '\0') {
                strncpy(item.node_id, pkt.device_id, sizeof(item.node_id) - 1);
            } else {
                snprintf(item.node_id, sizeof(item.node_id), "NODE_%02X%02X%02X", mac[3], mac[4], mac[5]);
            }

            item.timestamp_us = pkt.timestamp_us;
            item.x = pkt.x_nT;
            item.y = pkt.y_nT;
            item.z = pkt.z_nT;
            item.status = pkt.status;
            item.temp = pkt.temp;
            item.vbat = pkt.vbat_mv / 1000.0f;

            snprintf(item.line, sizeof(item.line), "%s,%llu,%.2f,%.2f,%.2f,%06X,%.1f,%.2f,%d\n",
                     item.node_id, (unsigned long long)item.timestamp_us,
                     item.x, item.y, item.z, (unsigned int)(item.status & 0xFFFFFF),
                     item.temp, item.vbat, item.rssi);

        } else {
            // ASCII String CSV Packet
            char rawStr[192];
            int cpyLen = (len < (int)sizeof(rawStr) - 1) ? len : ((int)sizeof(rawStr) - 1);
            memcpy(rawStr, incomingData, cpyLen);
            rawStr[cpyLen] = '\0';

            // Check if string has CSV format: device_id,timestamp_us,x_nT,y_nT,z_nT,status
            char devBuf[32] = {0};
            unsigned long long ts = 0;
            float x = 0, y = 0, z = 0;
            unsigned int st = 0;
            float temp = 0, vbat = 0;
            int scanned = sscanf(rawStr, "%31[^,],%llu,%f,%f,%f,%x,%f,%f", devBuf, &ts, &x, &y, &z, &st, &temp, &vbat);

            if (scanned >= 6) {
                strncpy(item.node_id, devBuf, sizeof(item.node_id) - 1);
                item.timestamp_us = ts;
                item.x = x; item.y = y; item.z = z;
                item.status = st;
                item.temp = temp; item.vbat = vbat;

                snprintf(item.line, sizeof(item.line), "%s,%llu,%.2f,%.2f,%.2f,%06X,%.1f,%.2f,%d\n",
                         item.node_id, (unsigned long long)item.timestamp_us,
                         item.x, item.y, item.z, (unsigned int)(item.status & 0xFFFFFF),
                         item.temp, item.vbat, item.rssi);
            } else {
                // Raw line fallback
                snprintf(item.node_id, sizeof(item.node_id), "NODE_%02X%02X%02X", mac[3], mac[4], mac[5]);
                snprintf(item.line, sizeof(item.line), "%s", rawStr);
                if (item.line[strlen(item.line)-1] != '\n') {
                    strcat(item.line, "\n");
                }
            }
        }

        // Record in Node Tracker table
        nodeTracker.recordPacket(item.node_id, item.mac, item.rssi, item.x, item.y, item.z, item.temp, item.vbat, "ESP-NOW");

        // Push into Queue for Egress Relay Task
        if (telemetryQueue) {
            xQueueSendFromISR(telemetryQueue, &item, NULL);
        }
    }
};

#endif // ESP_NOW_RECEIVER_H
