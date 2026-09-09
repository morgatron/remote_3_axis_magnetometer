#include "ReceiverCLI.h"
#include <WiFi.h>
#include "BLEEgress.h"
#include "PowerManager.h"

#include "ReceiverContext.h"
#include "NodeTracker.h"

ReceiverCLI::ReceiverCLI(SaveCallback saveCb) : _saveCallback(saveCb) {
    _inputBuffer.reserve(128);
}

void ReceiverCLI::begin() {
    printHelp();
}

void ReceiverCLI::process() {
    while (Serial.available()) {
        char c = Serial.read();
        if (c == '\r' || c == '\n') {
            if (_inputBuffer.length() > 0) {
                _inputBuffer.trim();
                handleCommand(_inputBuffer);
                _inputBuffer = "";
            }
        } else {
            _inputBuffer += c;
        }
    }
}

void ReceiverCLI::printHelp() {
    Serial.println(F("\r\n================================================================="));
    Serial.println(F("           ESP32 MULTI-PROTOCOL RECEIVER & RELAY CLI             "));
    Serial.println(F("================================================================="));
    Serial.println(F(" Commands:"));
    Serial.println(F("   HELP / STATUS       - Show system status, configuration, & stats"));
    Serial.println(F("   NODES               - Display active remote sensor node table"));
    Serial.println(F("   DEBUG <ON|OFF>      - Enable/Disable rendezvous diagnostic logging"));
    Serial.println(F("   DFS <ON|OFF>        - Enable/Disable Dynamic Frequency Scaling (40MHz sleep)"));
    Serial.println(F("   MODE <SERIAL|WIFI|BOTH|BLE> - Set egress forwarding mode"));
    Serial.println(F("   WIFI <ssid> <pass>  - Set egress router WiFi credentials"));
    Serial.println(F("   TARGET <ip> [port]  - Set target server IP & port for WiFi egress"));
    Serial.println(F("   CHANNEL <1-13>      - Set ESP-NOW WiFi radio channel"));
    Serial.println(F("   TESTADV [1-10]      - Broadcast test BLE egress advertisement packet"));
    Serial.println(F("   SAVE                - Save settings to Flash NVS"));
    Serial.println(F("   REBOOT              - Reboot receiver MCU"));
    Serial.println(F("=================================================================\r\n"));
}

void ReceiverCLI::printStatus() {
    Serial.println(F("\r\n=========================================="));
    Serial.println(F("          RECEIVER NODE STATUS            "));
    Serial.println(F("=========================================="));
    Serial.printf(" Uptime:               %.1f sec\r\n", millis() / 1000.0f);
    Serial.printf(" Egress Mode:          %s\r\n", 
        (egressModeConfig == 0) ? "SERIAL (USB CDC)" : 
        (egressModeConfig == 1) ? "WIFI" : 
        (egressModeConfig == 2) ? "BOTH (Serial + WiFi + BLE)" : "BLE (1Mbps Extended Advertising Broadcast, Wi-Fi OFF)");
    if (egressModeConfig == 2 || egressModeConfig == 3) {
        Serial.println(F(" BLE Egress Link:      BROADCASTING (1M Extended Advertising, Connectionless)"));
    }
    Serial.printf(" Debug Logs:           %s\r\n", g_debugScheduler ? "ENABLED" : "DISABLED");
    Serial.printf(" Dynamic Clock (DFS):  %s (Current: %d MHz)\r\n", 
                  g_dfsEnabled ? "ENABLED (40MHz sleep / 80MHz burst)" : "DISABLED (Static 80MHz)", 
                  getCpuFrequencyMhz());
    Serial.printf(" WiFi Router:          %s (%s, Local IP: %s)\r\n", wifiSSID.c_str(), wifiRelayConnected ? "CONNECTED" : "DISCONNECTED", WiFi.localIP().toString().c_str());
    Serial.printf(" Target Server IP:     %s:%d\r\n", targetServerIP.c_str(), targetServerPort);
    Serial.printf(" ESP-NOW Channel:      %d\r\n", espNowChannel);
    Serial.println(F("------------------------------------------"));
    Serial.printf(" ESP-NOW RX Packets:   %lu\r\n", (unsigned long)espnowRxCount);
    Serial.printf(" BLE RX Packets:       %lu\r\n", (unsigned long)bleRxCount);
    Serial.printf(" UDP RX Packets:       %lu\r\n", (unsigned long)udpRxCount);
    Serial.printf(" LoRa RX Packets:      %lu\r\n", (unsigned long)loraRxCount);
    Serial.printf(" Total Relayed:        %lu\r\n", (unsigned long)relayedPacketCount);
    Serial.printf(" Active Sensors:       %d\r\n", nodeTracker.getNodeCount());
    Serial.println(F("==========================================\r\n"));
}

void ReceiverCLI::handleCommand(const String &cmd) {
    String upper = cmd;
    upper.toUpperCase();

    if (upper == "HELP") {
        printHelp();
    } else if (upper == "STATUS") {
        printStatus();
    } else if (upper == "NODES") {
        nodeTracker.printNodeTable(Serial);
    } else if (upper == "DEBUG ON") {
        g_debugScheduler = true;
        Serial.println(F("[CLI] BLE Rendezvous debug logging ENABLED."));
    } else if (upper == "DEBUG OFF") {
        g_debugScheduler = false;
        Serial.println(F("[CLI] BLE Rendezvous debug logging DISABLED."));
    } else if (upper == "DEBUG") {
        g_debugScheduler = !g_debugScheduler;
        Serial.printf("[CLI] BLE Rendezvous debug logging %s.\r\n", g_debugScheduler ? "ENABLED" : "DISABLED");
    } else if (upper == "DFS ON") {
        PowerManager::setDfsEnabled(true);
        Serial.println(F("[CLI] Dynamic Frequency Scaling (DFS) ENABLED (40MHz sleep, 80MHz burst)."));
    } else if (upper == "DFS OFF") {
        PowerManager::setDfsEnabled(false);
        Serial.println(F("[CLI] Dynamic Frequency Scaling (DFS) DISABLED (Static 80MHz)."));
    } else if (upper == "DFS") {
        PowerManager::setDfsEnabled(!PowerManager::isDfsEnabled());
        Serial.printf("[CLI] Dynamic Frequency Scaling (DFS) %s.\r\n", PowerManager::isDfsEnabled() ? "ENABLED" : "DISABLED");
    } else if (upper.startsWith("TESTADV")) {
        int n = cmd.substring(7).toInt();
        if (n <= 0) n = 18;
        if (n > 18) n = 18;
        GatewayAdvPacket pkt;
        memset(&pkt, 0, sizeof(pkt));
        pkt.company_id = 0xFFFF;
        pkt.magic[0] = 'M';
        pkt.magic[1] = 'G';
        static uint8_t tSeq = 0;
        pkt.packet_seq = ++tSeq;
        strncpy(pkt.node_id, "TESTNODE", sizeof(pkt.node_id) - 1);
        pkt.timestamp_us = (uint64_t)millis() * 1000ULL;
        pkt.sample_interval_ms = 1000;
        pkt.sample_count = n;
        pkt.status = 0x004D4F;
        pkt.vbat_mv = 3900;
        pkt.rssi = -55;
        for (int i = 0; i < n; i++) {
            pkt.samples[i].x_nT = 1000.0f + i * 10.0f;
            pkt.samples[i].y_nT = -2000.0f + i * 10.0f;
            pkt.samples[i].z_nT = 3000.0f + i * 10.0f;
        }
        Serial.printf("[TEST] Calling BLEEgress::broadcast with %d samples...\r\n", n);
        BLEEgress::broadcast(pkt);
    } else if (upper.startsWith("MODE ")) {
        String modeStr = upper.substring(5);
        modeStr.trim();
        if (modeStr == "SERIAL") {
            egressModeConfig = 0;
            WiFi.mode(WIFI_OFF);
            Serial.println(F("[CLI] Egress Mode set to: SERIAL (Wi-Fi OFF)"));
        } else if (modeStr == "WIFI") {
            egressModeConfig = 1;
            Serial.println(F("[CLI] Egress Mode set to: WIFI"));
        } else if (modeStr == "BOTH") {
            egressModeConfig = 2;
            BLEEgress::begin("MAG_GATEWAY");
            Serial.println(F("[CLI] Egress Mode set to: BOTH (Serial + WiFi + BLE)"));
        } else if (modeStr == "BLE") {
            egressModeConfig = 3;
            WiFi.mode(WIFI_OFF);
            BLEEgress::begin("MAG_GATEWAY");
            Serial.println(F("[CLI] Egress Mode set to: BLE (1Mbps GATT Relay, Wi-Fi OFF ~80mA saved)"));
        } else {
            Serial.println(F("[CLI ERROR] Invalid mode. Use SERIAL, WIFI, BOTH, or BLE."));
        }
        if (_saveCallback) _saveCallback();
    } else if (upper.startsWith("WIFI ")) {
        String args = cmd.substring(5);
        args.trim();
        String upperArgs = args;
        upperArgs.toUpperCase();
        if (upperArgs == "CLEAR" || upperArgs == "OFF") {
            wifiSSID = "";
            wifiPass = "";
            if (_saveCallback) _saveCallback();
            Serial.println(F("[CLI] External router WiFi credentials cleared. Operating in standalone SoftAP mode."));
        } else if (upperArgs == "STATUS") {
            Serial.printf("[CLI] SoftAP Active SSID: '%s' (IP: 192.168.4.1)\r\n", apSSID.c_str());
            Serial.printf("[CLI] External Router STA Connected: %s\r\n", wifiRelayConnected ? "YES" : "NO");
            if (wifiRelayConnected) {
                Serial.printf("  STA IP: %s\r\n", WiFi.localIP().toString().c_str());
            }
        } else {
            int firstQuote = args.indexOf('"');
            int secondQuote = args.indexOf('"', firstQuote + 1);
            if (firstQuote >= 0 && secondQuote > firstQuote) {
                wifiSSID = args.substring(firstQuote + 1, secondQuote);
                wifiPass = args.substring(secondQuote + 1);
                wifiPass.trim();
            } else {
                int lastSpace = args.lastIndexOf(' ');
                if (lastSpace > 0) {
                    wifiSSID = args.substring(0, lastSpace);
                    wifiPass = args.substring(lastSpace + 1);
                    wifiSSID.trim(); wifiPass.trim();
                } else {
                    wifiSSID = args;
                    wifiPass = "";
                }
            }
            if (wifiSSID.length() > 0) {
                Serial.printf("[CLI] Configured External WiFi SSID: '%s'\r\n", wifiSSID.c_str());
                if (_saveCallback) _saveCallback();
                Serial.println(F("[CLI] Rebooting to apply WiFi connection..."));
                delay(500);
                ESP.restart();
            } else {
                Serial.println(F("[CLI ERROR] Usage: WIFI \"<ssid>\" <password> or WIFI CLEAR"));
            }
        }
    } else if (upper.startsWith("TARGET ")) {
        String args = cmd.substring(7);
        args.trim();
        int spaceIdx = args.indexOf(' ');
        if (spaceIdx > 0) {
            targetServerIP = args.substring(0, spaceIdx);
            targetServerPort = args.substring(spaceIdx + 1).toInt();
        } else {
            targetServerIP = args;
        }
        Serial.printf("[CLI] Configured Target Server: %s:%d\r\n", targetServerIP.c_str(), targetServerPort);
        if (_saveCallback) _saveCallback();
    } else if (upper.startsWith("CHANNEL ")) {
        int chan = upper.substring(8).toInt();
        if (chan >= 1 && chan <= 13) {
            espNowChannel = chan;
            Serial.printf("[CLI] ESP-NOW Channel set to %d\r\n", espNowChannel);
            if (_saveCallback) _saveCallback();
        } else {
            Serial.println(F("[CLI ERROR] Invalid channel. Choose 1 - 13."));
        }
    } else if (upper == "SAVE") {
        if (_saveCallback) _saveCallback();
        Serial.println(F("[CLI] Configuration saved to NVS Flash memory."));
    } else if (upper == "REBOOT") {
        Serial.println(F("[CLI] Rebooting Receiver MCU..."));
        delay(300);
        ESP.restart();
    } else {
        Serial.printf("[CLI ERROR] Unknown command: '%s'. Type HELP for command list.\r\n", cmd.c_str());
    }
}
