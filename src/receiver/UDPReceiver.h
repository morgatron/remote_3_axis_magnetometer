#ifndef UDP_RECEIVER_H
#define UDP_RECEIVER_H

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include "ITelemetryReceiver.h"
#include "TelemetryPacket.h"
#include "ReceiverContext.h"
#include "NodeTracker.h"
#include "IngestionPipeline.h"

class UDPReceiver : public ITelemetryReceiver {
public:
    explicit UDPReceiver(uint16_t port = 9876) : _port(port) {}

    void begin() override {
        if (egressModeConfig == MODE_EGRESS_SERIAL || egressModeConfig == MODE_EGRESS_BLE) {
            Serial.println(F("[UDP RECEIVER] Disabled in BLE/Serial mode (Wi-Fi radio OFF)."));
            return;
        }
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
                udpRxCount = udpRxCount + 1;
                uint8_t mac[6] = { remoteIP[0], remoteIP[1], remoteIP[2], remoteIP[3], 0x00, 0x00 };
                IngestionPipeline::ingestCsv(line, mac, rssi, "WIFI_UDP");
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
