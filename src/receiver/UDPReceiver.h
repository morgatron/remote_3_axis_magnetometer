#ifndef UDP_RECEIVER_H
#define UDP_RECEIVER_H

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include "ITelemetryReceiver.h"
#include "TelemetryPacket.h"
#include "NodeTracker.h"

extern QueueHandle_t telemetryQueue;
extern NodeTracker nodeTracker;
extern volatile uint32_t udpRxCount;

class UDPReceiver : public ITelemetryReceiver {
public:
    explicit UDPReceiver(uint16_t port = 9876) : _port(port) {}

    void begin() override {
        _udp.begin(_port);
        Serial.printf("[UDP RECEIVER SUCCESS] Active listening on UDP Port %d\r\n", _port);
    }

    void poll() override {
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

                uint8_t mac[6] = { remoteIP[0], remoteIP[1], remoteIP[2], remoteIP[3], 0x00, 0x00 };
                memcpy(item.mac, mac, 6);

                if (TelemetryItem::parseCsvLine(line, item)) {
                    item.formatCsvLine();
                } else {
                    snprintf(item.node_id, sizeof(item.node_id), "IP_%d_%d_%d_%d", remoteIP[0], remoteIP[1], remoteIP[2], remoteIP[3]);
                    snprintf(item.line, sizeof(item.line), "%s\n", line);
                }

                nodeTracker.recordPacket(item.node_id, item.mac, item.rssi, item.x, item.y, item.z, item.temp, item.vbat, "WIFI_UDP");
                if (telemetryQueue) xQueueSend(telemetryQueue, &item, 0);
            }
            line = strtok(NULL, "\r\n");
        }
    }

    const char* getName() const override {
        return "UDP";
    }

private:
    WiFiUDP _udp;
    uint16_t _port;
};

#endif // UDP_RECEIVER_H
