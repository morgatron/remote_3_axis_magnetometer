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
#include "OLEDDisplay.h"

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

String apSSID = "";
String apPass = "magnetometer123";

void setupSoftAP() {
    WiFi.mode(WIFI_AP_STA);
    delay(100);

    uint8_t mac[6];
    WiFi.softAPmacAddress(mac);
    char buf[32];
    snprintf(buf, sizeof(buf), "MAG_GATEWAY_%02X%02X", mac[4], mac[5]);
    apSSID = String(buf);
    IPAddress apIP(192, 168, 4, 1);
    IPAddress gateway(192, 168, 4, 1);
    IPAddress subnet(255, 255, 255, 0);
    WiFi.softAPConfig(apIP, gateway, subnet);
    bool apSuccess = WiFi.softAP(apSSID.c_str(), apPass.c_str(), 1, 0, 8);

    if (apSuccess) {
        Serial.println(F("\r\n========================================================="));
        Serial.printf(" [SOFTAP CREATED] Receiver Field Access Point Active!\r\n");
        Serial.printf("   SSID:     %s\r\n", apSSID.c_str());
        Serial.printf("   Password: %s\r\n", apPass.c_str());
        Serial.printf("   AP IP:    192.168.4.1\r\n");
        Serial.printf("   Channel:  1\r\n");
        Serial.println(F("=========================================================\r\n"));
    } else {
        Serial.println(F("[SOFTAP ERROR] Failed to create SoftAP!"));
    }
}

uint32_t lastOledActivityMs = 0;
bool oledScreenActive = true;

void connectEgressWiFi() {
    if (egressModeConfig == MODE_EGRESS_SERIAL) {
        WiFi.mode(WIFI_OFF);
        Serial.println(F("[POWER] Egress mode is SERIAL. Wi-Fi radio turned OFF (~80mA saved)."));
        return;
    }

    setupSoftAP();

    if (wifiSSID.length() > 0) {
        Serial.printf("[WIFI RELAY] Connecting to External Egress Router: '%s'...\r\n", wifiSSID.c_str());
        WiFi.begin(wifiSSID.c_str(), wifiPass.c_str());

        unsigned long start = millis();
        while (WiFi.status() != WL_CONNECTED && (millis() - start < 8000)) {
            delay(500);
            Serial.print(".");
        }

        if (WiFi.status() == WL_CONNECTED) {
            wifiRelayConnected = true;
            Serial.println(F("\r\n[WIFI RELAY SUCCESS] Connected to External Router!"));
            Serial.print(F("  Router Local IP: ")); Serial.println(WiFi.localIP());
            Serial.print(F("  RSSI:            ")); Serial.print(WiFi.RSSI()); Serial.println(F(" dBm"));
        } else {
            wifiRelayConnected = false;
            Serial.println(F("\r\n[WIFI RELAY NOTICE] External Router not reachable. Operating in standalone SoftAP mode."));
        }
    }
}

ReceiverCLI receiverCLI(saveReceiverSettings);

void setup() {
#if defined(ESP_PLATFORM)
    setCpuFrequencyMhz(80); // Scale CPU frequency down to 80 MHz to save ~28 mA
#endif
    pinMode(0, INPUT_PULLUP); // PRG / USER button on Heltec V4 for OLED wake
    lastOledActivityMs = millis();

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
    Serial.println(F(" Power Mode: Low-Power Receiver (80 MHz CPU, OLED 30s Timeout)"));
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

    // 6. Initialize WiFi UDP Listener (SoftAP & Router STA)
    UDPReceiver::begin(targetServerPort);

    // 7. Initialize BLE / BLE Coded PHY Receiver Scanner
    BLEReceiver::begin();

    // 8. Initialize Sub-GHz SX1262 LoRa Receiver
    LoRaReceiver::begin(LORA_CS_PIN, LORA_DIO1_PIN, LORA_RST_PIN, LORA_BUSY_PIN);

    // 9. Start Interactive CLI
    receiverCLI.begin();

#if defined(HELTEC_V4) || defined(ARDUINO_heltec_wifi_lora_32_V3)
    oledDisplay.begin(17, 18, 21, 36);
#endif
}

void loop() {
    // Check USER/PRG Button Press (GPIO 0) to wake OLED screen
    static bool lastBtnState = HIGH;
    bool btnState = digitalRead(0);
    if (btnState == LOW && lastBtnState == HIGH) {
        lastOledActivityMs = millis();
        if (!oledScreenActive) {
            oledDisplay.displayOn();
            oledScreenActive = true;
        }
    }
    lastBtnState = btnState;

#if defined(HELTEC_V4) || defined(ARDUINO_heltec_wifi_lora_32_V3)
    static uint32_t lastReceiverOledMs = 0;
    if (millis() - lastReceiverOledMs >= 500) {
        lastReceiverOledMs = millis();
        if (millis() - lastOledActivityMs > 30000) {
            if (oledScreenActive) {
                oledDisplay.displayOff();
                oledScreenActive = false;
            }
        } else {
            if (!oledScreenActive) {
                oledDisplay.displayOn();
                oledScreenActive = true;
            }
            const char* modeNames[] = {"SERIAL", "WIFI", "BOTH"};
            String egressIp = wifiRelayConnected ? WiFi.localIP().toString() : "192.168.4.1";
            uint32_t totalRxCount = bleRxCount + espnowRxCount + udpRxCount + loraRxCount;
            oledDisplay.updateReceiverScreen(
                nodeTracker.getNodeCount(),
                totalRxCount,
                nodeTracker.getLastRssi(),
                nodeTracker.getLastNodeId(),
                egressIp.c_str(),
                modeNames[egressModeConfig % 3]
            );
        }
    }
#endif

    // Process serial CLI commands
    if (Serial.available() > 0) {
        lastOledActivityMs = millis();
    }
    receiverCLI.process();

    // Process incoming UDP packets from SoftAP and Router STA
    UDPReceiver::handlePackets();

    delay(2);
}
