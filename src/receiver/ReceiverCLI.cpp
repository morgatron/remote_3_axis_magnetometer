#include "ReceiverCLI.h"
#include <WiFi.h>
#include "board_config.h"
#include "BLEEgress.h"
#include "PowerManager.h"
#include "WiFiManager.h"
#include "HttpBatchEgress.h"

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
        } else if (c == '\b' || c == 0x7F) {
            if (_inputBuffer.length() > 0) {
                _inputBuffer.remove(_inputBuffer.length() - 1);
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
    Serial.println(F("   MODE <SERIAL|WIFI|BOTH|BLE|BLE_SERIAL> - Set egress forwarding mode"));
    Serial.println(F("   MAC                 - Print Wi-Fi Station MAC address"));
    Serial.println(F("   WIFI <ssid> [pass]  - Set standard WPA2-PSK WiFi credentials"));
    Serial.println(F("   EAP <ssid> <user> <pass> [id] - Set WPA2-Enterprise (802.1X PEAP)"));
    Serial.println(F("   EAP USER <username> - Update Enterprise username (keeps password, sets ID to None)"));
    Serial.println(F("   EAP ID <id|NONE>    - Update Enterprise outer identity"));
    Serial.println(F("   EAP SSID <ssid>     - Switch Enterprise SSID (e.g. ANU-Secure, eduroam)"));
    Serial.println(F("   EAP CLEAR           - Clear Enterprise credentials"));
    Serial.println(F("   SERVER <url>        - Set Central Server URL (e.g. http://10.28.x.x:8000/api/v1/telemetry/batch)"));
    Serial.println(F("   APIKEY <key>        - Set Central Server X-API-Key token"));
    Serial.println(F("   NTP                 - Show current NTP sync status and UTC time"));
    Serial.println(F("   TESTPOST            - Send immediate synthetic test batch via HTTP POST"));
    Serial.println(F("   TARGET <ip> [port]  - Set fallback UDP broadcast target IP & port"));
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
    Serial.printf(" Receiver Vbat:        %.2f V (%u mV)\r\n", sampleBatteryVoltage(true), sampleBatteryMilliVolts());
    Serial.printf(" Egress Mode:          %s\r\n", 
        (egressModeConfig == 0) ? "SERIAL (USB CDC)" : 
        (egressModeConfig == 1) ? "WIFI" : 
        (egressModeConfig == 2) ? "BOTH (Serial + WiFi + BLE)" : "BLE + SERIAL (USB CDC + 1Mbps Extended Adv, Wi-Fi OFF)");
    if (egressModeConfig == 2 || egressModeConfig == 3) {
        Serial.println(F(" BLE Egress Link:      BROADCASTING (1M Extended Advertising, Connectionless)"));
    }
    Serial.printf(" Debug Logs:           %s\r\n", g_debugScheduler ? "ENABLED" : "DISABLED");
    Serial.printf(" Dynamic Clock (DFS):  %s (Current: %d MHz)\r\n", 
                  g_dfsEnabled ? "ENABLED (40MHz sleep / 80MHz burst)" : "DISABLED (Static 80MHz)", 
                  getCpuFrequencyMhz());
    Serial.printf(" Wi-Fi Station MAC:    %s\r\n", WiFiManager::getMacAddress().c_str());
    Serial.printf(" Wi-Fi Network:        '%s' (%s, Mode: %s, RSSI: %d dBm)\r\n",
                  WiFiManager::getSsid().c_str(),
                  WiFiManager::isConnected() ? "CONNECTED" : "DISCONNECTED",
                  WiFiManager::isEapConfigured() ? "WPA2-Enterprise (802.1X PEAP)" : "WPA2-PSK",
                  WiFiManager::getRssi());
    if (WiFiManager::isEapConfigured()) {
        Serial.printf(" EAP User:             %s (Password: %d chars)\r\n", 
                      WiFiManager::getEapUsername().c_str(), WiFiManager::getEapPasswordLength());
        if (WiFiManager::getEapIdentity().length() > 0) {
            Serial.printf(" EAP Outer Identity:   %s\r\n", WiFiManager::getEapIdentity().c_str());
        } else {
            Serial.println(F(" EAP Outer Identity:   None (Anonymous/Omitted)"));
        }
    }
    Serial.printf(" Station IP:           %s\r\n", WiFiManager::getIpAddress().c_str());
    Serial.printf(" NTP Clock Sync:       %s (UTC: %s)\r\n",
                  WiFiManager::isNtpSynced() ? "SYNCHRONIZED" : "NOT SYNCED",
                  WiFiManager::getUtcIsoString().c_str());
    Serial.printf(" Central Server URL:   %s\r\n",
                  HttpBatchEgress::hasServerUrl() ? HttpBatchEgress::getServerUrl().c_str() : "NOT CONFIGURED");
    if (HttpBatchEgress::getApiKey().length() > 0) {
        Serial.println(F(" Central API Key:      CONFIGURED"));
    }
    Serial.printf(" UDP Fallback Target:  %s:%d\r\n", targetServerIP.c_str(), targetServerPort);
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
        pkt.temp_c_x100 = 2450; // 24.50 C
        pkt.rssi = -55;
        pkt.gw_vbat_mv = getBatteryMilliVolts();
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
        } else if (modeStr == "BLE" || modeStr == "BLE_SERIAL" || modeStr == "SERIAL_BLE" || modeStr == "BLE+SERIAL") {
            egressModeConfig = 3;
            WiFi.mode(WIFI_OFF);
            BLEEgress::begin("MAG_GATEWAY");
            Serial.println(F("[CLI] Egress Mode set to: BLE + SERIAL (1Mbps Extended Adv + USB CDC, Wi-Fi OFF ~80mA saved)"));
        } else {
            Serial.println(F("[CLI ERROR] Invalid mode. Use SERIAL, WIFI, BOTH, BLE, or BLE_SERIAL."));
        }
        if (_saveCallback) _saveCallback();
    } else if (upper == "MAC") {
        Serial.printf("[CLI] Wi-Fi Station MAC: %s\r\n", WiFiManager::getMacAddress().c_str());
    } else if (upper == "NTP") {
        bool synced = WiFiManager::isNtpSynced();
        Serial.printf("[CLI] NTP Status: %s\r\n", synced ? "SYNCHRONIZED" : "NOT SYNCHRONIZED");
        if (synced) {
            Serial.printf("  Current UTC ISO: %s\r\n", WiFiManager::getUtcIsoString().c_str());
        }
    } else if (upper == "TESTPOST") {
        HttpBatchEgress::testPost();
    } else if (upper.startsWith("SERVER ")) {
        String url = cmd.substring(7);
        url.trim();
        HttpBatchEgress::setServerUrl(url);
        Serial.printf("[CLI] Central Server URL configured: %s\r\n", url.c_str());
    } else if (upper.startsWith("APIKEY ")) {
        String key = cmd.substring(7);
        key.trim();
        HttpBatchEgress::setApiKey(key);
        Serial.println(F("[CLI] Central Server X-API-Key token configured."));
    } else if (upper.startsWith("EAP ")) {
        String args = cmd.substring(4);
        args.trim();
        String upperArgs = args;
        upperArgs.toUpperCase();
        if (upperArgs == "CLEAR" || upperArgs == "OFF") {
            WiFiManager::clearEapCredentials();
            Serial.println(F("[CLI] WPA2-Enterprise credentials cleared."));
        } else if (upperArgs.startsWith("USER ")) {
            String newUser = args.substring(5);
            newUser.trim();
            if (newUser.startsWith("\"") && newUser.endsWith("\"") && newUser.length() >= 2) {
                newUser = newUser.substring(1, newUser.length() - 1);
            }
            WiFiManager::setEapUsername(newUser, "");
            Serial.printf("[CLI] Updated EAP User to '%s' (Identity: None). Rebooting...\r\n", newUser.c_str());
            delay(500);
            ESP.restart();
        } else if (upperArgs.startsWith("ID ")) {
            String newId = args.substring(3);
            newId.trim();
            if (newId.equalsIgnoreCase("NONE") || newId.equalsIgnoreCase("CLEAR")) {
                newId = "";
            } else if (newId.startsWith("\"") && newId.endsWith("\"") && newId.length() >= 2) {
                newId = newId.substring(1, newId.length() - 1);
            }
            WiFiManager::setEapUsername(WiFiManager::getEapUsername(), newId);
            Serial.printf("[CLI] Updated EAP Outer Identity to '%s'. Rebooting...\r\n", 
                          newId.length() > 0 ? newId.c_str() : "None");
            delay(500);
            ESP.restart();
        } else if (upperArgs.startsWith("SSID ")) {
            String newSsid = args.substring(5);
            newSsid.trim();
            if (newSsid.startsWith("\"") && newSsid.endsWith("\"") && newSsid.length() >= 2) {
                newSsid = newSsid.substring(1, newSsid.length() - 1);
            }
            WiFiManager::setEapSsid(newSsid);
            Serial.printf("[CLI] Updated EAP Network SSID to '%s'. Rebooting...\r\n", newSsid.c_str());
            delay(500);
            ESP.restart();
        } else {
            String ssid = "", user = "", pass = "", id = "";
            int idx = 0;
            if (args.startsWith("\"")) {
                int q2 = args.indexOf('"', 1);
                if (q2 > 1) {
                    ssid = args.substring(1, q2);
                    idx = q2 + 1;
                }
            }
            if (ssid.length() == 0) {
                int sp = args.indexOf(' ');
                if (sp > 0) {
                    ssid = args.substring(0, sp);
                    idx = sp + 1;
                }
            }
            if (idx > 0 && idx < (int)args.length()) {
                String rem = args.substring(idx);
                rem.trim();
                int sp1 = rem.indexOf(' ');
                if (sp1 > 0) {
                    user = rem.substring(0, sp1);
                    String rem2 = rem.substring(sp1 + 1);
                    rem2.trim();
                    int sp2 = rem2.indexOf(' ');
                    if (sp2 > 0) {
                        pass = rem2.substring(0, sp2);
                        id = rem2.substring(sp2 + 1);
                        id.trim();
                    } else {
                        pass = rem2;
                    }
                }
            }
            if (ssid.length() > 0 && user.length() > 0 && pass.length() > 0) {
                if (user.startsWith("\"") && user.endsWith("\"") && user.length() >= 2) user = user.substring(1, user.length() - 1);
                if (pass.startsWith("\"") && pass.endsWith("\"") && pass.length() >= 2) pass = pass.substring(1, pass.length() - 1);
                if (id.startsWith("\"") && id.endsWith("\"") && id.length() >= 2) id = id.substring(1, id.length() - 1);
                WiFiManager::setEapCredentials(ssid, user, pass, id);
                Serial.printf("[CLI] Configured WPA2-Enterprise: SSID '%s', User '%s'\r\n", ssid.c_str(), user.c_str());
                Serial.println(F("[CLI] Rebooting to apply Enterprise Wi-Fi connection..."));
                delay(500);
                ESP.restart();
            } else {
                Serial.println(F("[CLI ERROR] Usage: EAP <ssid> <username> <password> [identity] or EAP CLEAR"));
            }
        }
    } else if (upper.startsWith("WIFI ")) {
        String args = cmd.substring(5);
        args.trim();
        String upperArgs = args;
        upperArgs.toUpperCase();
        if (upperArgs == "CLEAR" || upperArgs == "OFF") {
            WiFiManager::clearAllCredentials();
            if (_saveCallback) _saveCallback();
            Serial.println(F("[CLI] External router WiFi credentials cleared. Operating in standalone SoftAP mode."));
        } else if (upperArgs == "STATUS") {
            Serial.printf("[CLI] SoftAP Active SSID: '%s' (IP: 192.168.4.1)\r\n", apSSID.c_str());
            Serial.printf("[CLI] External Router STA Connected: %s\r\n", WiFiManager::isConnected() ? "YES" : "NO");
            if (WiFiManager::isConnected()) {
                Serial.printf("  STA IP: %s\r\n", WiFi.localIP().toString().c_str());
            }
        } else {
            String ssid = "", pass = "";
            int firstQuote = args.indexOf('"');
            int secondQuote = args.indexOf('"', firstQuote + 1);
            if (firstQuote >= 0 && secondQuote > firstQuote) {
                ssid = args.substring(firstQuote + 1, secondQuote);
                pass = args.substring(secondQuote + 1);
                pass.trim();
            } else {
                int lastSpace = args.lastIndexOf(' ');
                if (lastSpace > 0) {
                    ssid = args.substring(0, lastSpace);
                    pass = args.substring(lastSpace + 1);
                    ssid.trim(); pass.trim();
                } else {
                    ssid = args;
                    pass = "";
                }
            }
            if (ssid.startsWith("\"") && ssid.endsWith("\"") && ssid.length() >= 2) {
                ssid = ssid.substring(1, ssid.length() - 1);
            }
            if (pass.startsWith("\"") && pass.endsWith("\"") && pass.length() >= 2) {
                pass = pass.substring(1, pass.length() - 1);
            }
            if (ssid.length() > 0) {
                WiFiManager::setPskCredentials(ssid, pass);
                Serial.printf("[CLI] Configured External WiFi SSID: '%s' (Pass len: %d)\r\n", ssid.c_str(), (int)pass.length());
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
