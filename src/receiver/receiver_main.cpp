#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>
#include "board_config.h"
#include "TelemetryPacket.h"
#include "NodeTracker.h"
#include "ESPNowReceiver.h"
#include "BLEReceiver.h"
#include "UDPReceiver.h"
#include "LoRaReceiver.h"
#include "RelayEgress.h"
#include "ReceiverCLI.h"

// Global Receiver State Variables
QueueHandle_t telemetryQueue = NULL;
NodeTracker nodeTracker;

volatile uint32_t espnowRxCount = 0;
volatile uint32_t bleRxCount = 0;
volatile uint32_t udpRxCount = 0;
volatile uint32_t loraRxCount = 0;
volatile uint32_t relayedPacketCount = 0;

uint8_t egressModeConfig = MODE_EGRESS_BOTH; // 0 = Serial, 1 = WiFi, 2 = Both
String wifiSSID = "";
String wifiPass = "";
String targetServerIP = "255.255.255.255";
uint16_t targetServerPort = 9876;
uint8_t espNowChannel = 1;
bool wifiRelayConnected = false;

WiFiUDP UDPReceiver::_udp;
uint16_t UDPReceiver::_port = 9876;

Preferences prefs;

void saveReceiverSettings() {
    prefs.begin("rcvr_v0", false);
    prefs.putUChar("mode", egressModeConfig);
    prefs.putString("ssid", wifiSSID);
    prefs.putString("pass", wifiPass);
    prefs.putString("target_ip", targetServerIP);
    prefs.putUShort("target_port", targetServerPort);
    prefs.putUChar("channel", espNowChannel);
    prefs.end();
}

void loadReceiverSettings() {
    prefs.begin("rcvr_v0", true);
    egressModeConfig = prefs.getUChar("mode", MODE_EGRESS_BOTH);
    wifiSSID = prefs.getString("ssid", "");
    wifiPass = prefs.getString("pass", "");
    targetServerIP = prefs.getString("target_ip", "255.255.255.255");
    targetServerPort = prefs.getUShort("target_port", 9876);
    espNowChannel = prefs.getUChar("channel", 1);
    prefs.end();
}

void connectEgressWiFi() {
    if (wifiSSID.length() > 0) {
        Serial.printf("[WIFI RELAY] Connecting to Egress Router: '%s'...\r\n", wifiSSID.c_str());
        WiFi.disconnect(true);
        delay(100);
        WiFi.mode(WIFI_AP_STA); // Hybrid AP+STA allows concurrent ESP-NOW & Router STA
        WiFi.begin(wifiSSID.c_str(), wifiPass.c_str());

        unsigned long start = millis();
        while (WiFi.status() != WL_CONNECTED && (millis() - start < 10000)) {
            delay(500);
            Serial.print(".");
        }

        if (WiFi.status() == WL_CONNECTED) {
            wifiRelayConnected = true;
            Serial.println(F("\r\n[WIFI RELAY SUCCESS] Connected to network!"));
            Serial.print(F("  Local IP:   ")); Serial.println(WiFi.localIP());
            Serial.print(F("  RSSI:       ")); Serial.print(WiFi.RSSI()); Serial.println(F(" dBm"));
        } else {
            wifiRelayConnected = false;
            Serial.println(F("\r\n[WIFI RELAY NOTICE] Network connection timed out. Falling back to local reception."));
        }
    }
}

ReceiverCLI receiverCLI(saveReceiverSettings);

void setup() {
#if defined(CONFIG_IDF_TARGET_ESP32C3) || defined(ARDUINO_ARCH_ESP32C3)
    Serial.setTxTimeoutMs(0);
#endif
    Serial.begin(921600);
    unsigned long startWait = millis();
    while (!Serial && (millis() - startWait < 3000)) delay(10);

    Serial.println(F("\r\n========================================================="));
    Serial.println(F(" FIRMWARE: ESP32 Multi-Protocol Receiver & Data Relay Node"));
    Serial.println(F(" Supported Protocols: ESP-NOW, BLE / Coded PHY, WiFi UDP"));
    Serial.println(F(" Egress Targets: USB Serial CDC + WiFi HTTP/UDP Central"));
    Serial.println(F("========================================================="));

    loadReceiverSettings();

    // 1. Initialize Board Power Rail (Vext)
    initBoardPower();

    // 2. Create FreeRTOS Telemetry Queue
    telemetryQueue = xQueueCreate(256, sizeof(TelemetryItem));
    if (!telemetryQueue) {
        Serial.println(F("[FATAL ERROR] Unable to allocate FreeRTOS Telemetry Queue!"));
        return;
    }

    // 3. Start Egress Relay Task
    RelayEgress::begin();

    // 4. Connect to Egress Router if credentials exist
    connectEgressWiFi();

    // 5. Initialize ESP-NOW Receiver
    ESPNowReceiver::begin(espNowChannel);

    // 6. Initialize WiFi UDP Listener
    if (wifiRelayConnected) {
        UDPReceiver::begin(targetServerPort);
    }

    // 7. Initialize BLE / BLE Coded PHY Receiver Scanner
    BLEReceiver::begin();

    // 8. Initialize Sub-GHz SX1262 LoRa Receiver
    LoRaReceiver::begin(LORA_CS_PIN, LORA_DIO1_PIN, LORA_RST_PIN, LORA_BUSY_PIN);

    // 9. Start Interactive CLI
    receiverCLI.begin();
}

void loop() {
    // Process serial CLI commands
    receiverCLI.process();

    // Process incoming UDP packets if connected to WiFi network
    if (wifiRelayConnected) {
        UDPReceiver::handlePackets();
    }

    delay(2);
}
