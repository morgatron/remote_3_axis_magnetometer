#ifndef UDP_RECEIVER_H
#define UDP_RECEIVER_H

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include "TelemetryPacket.h"
#include "NodeTracker.h"

extern QueueHandle_t telemetryQueue;
extern NodeTracker nodeTracker;
extern volatile uint32_t udpRxCount;

class UDPReceiver {
public:
    static void begin(uint16_t port = 9876) {
        _port = port;
        _udp.begin(_port);
        Serial.printf("[UDP RECEIVER SUCCESS] Active listening on UDP Port %d\r\n", _port);
    }

    static void handlePackets() {
        int packetSize = _udp.parsePacket();
        if (packetSize <= 0) return;

        char buf[512];
        int len = _udp.read(buf, sizeof(buf) - 1);
        if (len <= 0) return;
        buf[len] = '\0';

        IPAddress remoteIP = _udp.remoteIP();
        int rssi = WiFi.RSSI();

        // Process line-by-line (handles batched UDP packets)
        char *line = strtok(buf, "\r\n");
        while (line != NULL) {
            if (strlen(line) > 10) {
                udpRxCount++;
                TelemetryItem item;
                memset(&item, 0, sizeof(item));
                item.rssi = rssi;
                strncpy(item.protocol, "WIFI_UDP", sizeof(item.protocol) - 1);

                // Default MAC from IP if direct MAC isn't attached
                uint8_t mac[6] = { remoteIP[0], remoteIP[1], remoteIP[2], remoteIP[3], 0x00, 0x00 };
                memcpy(item.mac, mac, 6);

                char devBuf[32] = {0};
                unsigned long long ts = 0;
                float x = 0, y = 0, z = 0;
                unsigned int st = 0;
                float temp = 0, vbat = 0;

                int scanned = sscanf(line, "%31[^,],%llu,%f,%f,%f,%x,%f,%f", devBuf, &ts, &x, &y, &z, &st, &temp, &vbat);
                if (scanned >= 6) {
                    strncpy(item.node_id, devBuf, sizeof(item.node_id) - 1);
                    item.timestamp_us = ts;
                    item.x = x; item.y = y; item.z = z; item.status = st;
                    item.temp = temp; item.vbat = vbat;

                    snprintf(item.line, sizeof(item.line), "%s,%llu,%.2f,%.2f,%.2f,%06X,%.1f,%.2f,%d\n",
                             item.node_id, (unsigned long long)item.timestamp_us,
                             item.x, item.y, item.z, (unsigned int)(item.status & 0xFFFFFF),
                             item.temp, item.vbat, item.rssi);
                } else {
                    snprintf(item.node_id, sizeof(item.node_id), "IP_%d_%d_%d_%d", remoteIP[0], remoteIP[1], remoteIP[2], remoteIP[3]);
                    snprintf(item.line, sizeof(item.line), "%s\n", line);
                }

                nodeTracker.recordPacket(item.node_id, item.mac, item.rssi, item.x, item.y, item.z, item.temp, item.vbat, "WIFI_UDP");

                if (telemetryQueue) {
                    xQueueSend(telemetryQueue, &item, 0);
                }
            }
            line = strtok(NULL, "\r\n");
        }
    }

private:
    static WiFiUDP _udp;
    static uint16_t _port;
};

#endif // UDP_RECEIVER_H
