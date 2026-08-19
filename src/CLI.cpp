#include "CLI.h"
#include "board_config.h"
#include "RM3100.h"
#include "FLC100_ADS131.h"
#include "MockSensor.h"
#include "BLEStream.h"
#include "LoRaStream.h"
#include <WiFi.h>

CLI::CLI(Magnetometer*& sensor, bool& streaming, uint8_t& current_rate, void (*saveCallback)())
    : _sensor(sensor), _streaming(streaming), _current_rate(current_rate), _saveCallback(saveCallback) {}

void CLI::begin() {
    _inputBuffer.reserve(64);
}

void CLI::update() {
    while (Serial.available()) {
        char c = Serial.read();
        if (c == '\n' || c == '\r') {
            if (_inputBuffer.length() > 0) {
                handleCommand(_inputBuffer);
                _inputBuffer = "";
            }
        } else {
            _inputBuffer += c;
        }
    }
}

void CLI::printHelp() {
    Serial.println("--- Magnetometer CLI Help ---");
    Serial.println("HELP          - Show this help");
    Serial.println("STREAM ON/OFF - Enable/Disable data streaming");
    Serial.println("RATE <hex>    - Set rate code (saved)");
    Serial.println("CYCLE <int>   - Set cycle count (RM3100 specific)");
    Serial.println("BATCH <1-10>  - Set samples per BLE burst (1 = instant 1Hz, 10 = low power)");
    Serial.println("STATUS        - Show current status");
    Serial.println("-----------------------");
}

void CLI::handleCommand(String cmd) {
    String originalCmd = cmd;
    originalCmd.trim();

    cmd.trim();
    cmd.toUpperCase();

    if (cmd == "HELP") {
        printHelp();
    } else if (cmd.startsWith("BATCH ")) {
        uint8_t count = (uint8_t)cmd.substring(6).toInt();
        if (count >= 1 && count <= 10) {
            extern uint8_t batchSizeConfig;
            batchSizeConfig = count;
            if (_saveCallback) _saveCallback();
            Serial.printf("BLE Batch Burst Size set to %d samples per burst.\r\n", batchSizeConfig);
        } else {
            Serial.println("Usage: BATCH <1-10> (Set samples per Coded PHY Extended Advertising burst)");
        }
    } else if (cmd == "STREAM ON") {
        _streaming = true;
        if (_saveCallback) _saveCallback();
        Serial.println("Streaming enabled.");
    } else if (cmd == "STREAM OFF") {
        _streaming = false;
        if (_saveCallback) _saveCallback();
        Serial.println("Streaming disabled.");
    } else if (cmd.startsWith("RATE ")) {
        String valStr = cmd.substring(5);
        _current_rate = (uint8_t)strtol(valStr.c_str(), NULL, 16);
        _sensor->setContinuousMode(true, _current_rate);
        if (_saveCallback) _saveCallback();
        Serial.print("Rate set to 0x");
        Serial.println(_current_rate, HEX);
    } else if (cmd.startsWith("CYCLE ")) {
        String valStr = cmd.substring(6);
        uint16_t cycle = (uint16_t)valStr.toInt();
        if (_sensor->getSensorName() == "RM3100") {
            static_cast<RM3100*>(_sensor)->setCycleCount(cycle, cycle, cycle);
            if (_saveCallback) _saveCallback();
            Serial.print("Cycle count set to ");
            Serial.println(cycle);
        } else {
            Serial.println("CYCLE command not supported for this sensor.");
        }
    } else if (cmd.startsWith("DOWNSAMPLE ")) {
        uint16_t ratio = (uint16_t)cmd.substring(11).toInt();
        if (ratio >= 1) {
            extern uint16_t current_downsample;
            current_downsample = ratio;
            if (_saveCallback) _saveCallback();
            Serial.print("Downsample ratio set to ");
            Serial.print(current_downsample);
            Serial.println("x");
        }
    } else if (cmd.startsWith("SENSOR ")) {
        String sub = cmd.substring(7);
        sub.trim();
        extern uint8_t sensorTypeConfig;
        extern Magnetometer* sensor;
        extern RM3100 sensorRM3100;
        extern FLC100_ADS131 sensorFLC100;
        extern MockSensor sensorMock;
        uint8_t newType = sensorTypeConfig;
        if (sub == "RM3100" || sub == "0") {
            newType = 0;
            sensor = &sensorRM3100;
            _current_rate = 0x96; // RM3100 default rate 37 Hz
        } else if (sub == "FLC100" || sub == "ADS131" || sub == "1") {
            newType = 1;
            sensor = &sensorFLC100;
            _current_rate = 0x06; // ADS131 default rate 1 kSPS
        } else if (sub == "MOCK" || sub == "2") {
            newType = 2;
            sensor = &sensorMock;
            _current_rate = 0x95; // Synthetic Mock default rate 75 Hz
        }
        sensorTypeConfig = newType;
        _sensor = sensor;

        Serial.print("Active sensor set to: ");
        Serial.println(_sensor->getSensorName());

        if (!sensor->begin()) {
            Serial.println("Warning: Selected sensor failed initialization!");
        } else {
            sensor->setContinuousMode(true, _current_rate);
            Serial.println("Sensor initialized and continuous mode started.");
        }
        if (_saveCallback) _saveCallback();
    } else if (cmd == "STATUS") {
        extern String deviceID;
        extern uint8_t batchSizeConfig;
        Serial.print("Device ID: "); Serial.println(deviceID);
        Serial.print("Streaming: "); Serial.println(_streaming ? "ON" : "OFF");
        Serial.print("Sensor: "); Serial.println(_sensor->getSensorName());
        Serial.print("Rate Code: 0x"); Serial.println(_current_rate, HEX);
        Serial.print("BLE Batch Size: "); Serial.print(batchSizeConfig); Serial.println(" samples/burst");
        Serial.println(_sensor->getStatusString());
    } else if (cmd.startsWith("ID ")) {
        String newID = cmd.substring(3);
        newID.trim();
        if (newID.length() > 0) {
            extern String deviceID;
            deviceID = newID;
            Serial.print("Device ID set to "); Serial.println(deviceID);
            if (_saveCallback) _saveCallback();
        }
    } else if (cmd == "TEST ON") {
        if (_sensor->getSensorName() == "FLC100-ADS131E08") {
            static_cast<FLC100_ADS131*>(_sensor)->setTestSignal(true);
            Serial.println("Internal test signal enabled (1Hz square wave).");
        } else {
            Serial.println("TEST command only supported for FLC100-ADS131E08.");
        }
    } else if (cmd == "TEST OFF") {
        if (_sensor->getSensorName() == "FLC100-ADS131E08") {
            static_cast<FLC100_ADS131*>(_sensor)->setTestSignal(false);
            Serial.println("Internal test signal disabled. Normal inputs active.");
        }
    } else if (cmd.startsWith("GAIN ")) {
        uint8_t gain = (uint8_t)cmd.substring(5).toInt();
        if (_sensor->getSensorName() == "FLC100-ADS131E08") {
            static_cast<FLC100_ADS131*>(_sensor)->setCalibration(2.4f, 20.0f, gain);
            if (_saveCallback) _saveCallback();
            Serial.print("PGA Gain set to ");
            Serial.println(gain);
        }
    } else if (cmd.startsWith("VREF ")) {
        float vref = cmd.substring(5).toFloat();
        if (vref > 0.0f && _sensor->getSensorName() == "FLC100-ADS131E08") {
            static_cast<FLC100_ADS131*>(_sensor)->setCalibration(vref, 20.0f, 1);
            Serial.print("VREF set to ");
            Serial.print(vref);
            Serial.println(" V");
        }
    } else if (cmd.startsWith("SSID ")) {
        String ssid = originalCmd.substring(5);
        ssid.trim();
        extern String wifiSSID, wifiPass;
        extern void connectWiFi();
        wifiSSID = ssid;
        Serial.print("WiFi SSID set to "); Serial.println(wifiSSID);
        if (_saveCallback) _saveCallback();
        if (wifiSSID.length() > 0 && wifiPass.length() > 0) {
            connectWiFi();
        }
    } else if (cmd.startsWith("PASS ")) {
        String pass = originalCmd.substring(5);
        pass.trim();
        extern String wifiSSID, wifiPass;
        extern void connectWiFi();
        wifiPass = pass;
        Serial.println("WiFi Password updated.");
        if (_saveCallback) _saveCallback();
        if (wifiSSID.length() > 0 && wifiPass.length() > 0) {
            connectWiFi();
        }
    } else if (cmd.startsWith("WIFI ")) {
        String subUpper = cmd.substring(5);
        subUpper.trim();
        if (subUpper == "OFF") {
            extern bool wifiConnected;
            WiFi.disconnect(true);
            WiFi.mode(WIFI_OFF);
            wifiConnected = false;
            Serial.println("WiFi Disabled.");
        } else if (subUpper == "STATUS") {
            extern bool wifiConnected;
            Serial.print("WiFi Connected: "); Serial.println(wifiConnected ? "YES" : "NO");
            if (wifiConnected) {
                Serial.print("IP Address: "); Serial.println(WiFi.localIP());
                Serial.print("RSSI: "); Serial.print(WiFi.RSSI()); Serial.println(" dBm");
            }
        } else if (subUpper == "SCAN") {
            Serial.println("Scanning 2.4GHz WiFi networks...");
            WiFi.mode(WIFI_STA);
            WiFi.disconnect();
            delay(100);
            int n = WiFi.scanNetworks();
            Serial.print("Scan completed. Found "); Serial.print(n); Serial.println(" networks:");
            for (int i = 0; i < n; ++i) {
                Serial.print("  "); Serial.print(i + 1); Serial.print(": ");
                Serial.print(WiFi.SSID(i)); Serial.print(" (");
                Serial.print(WiFi.RSSI(i)); Serial.println(" dBm)");
                delay(10);
            }
        } else {
            String rawSub = originalCmd.substring(5);
            rawSub.trim();
            int spaceIdx = rawSub.lastIndexOf(' ');
            if (spaceIdx > 0) {
                extern String wifiSSID, wifiPass;
                extern void connectWiFi();
                wifiSSID = rawSub.substring(0, spaceIdx);
                wifiPass = rawSub.substring(spaceIdx + 1);
                wifiSSID.trim();
                wifiPass.trim();
                connectWiFi();
                _saveCallback();
            } else {
                Serial.println("Usage: WIFI <ssid> <password>");
            }
        }
    } else if (cmd.startsWith("TARGET ")) {
        String ipStr = cmd.substring(7);
        ipStr.trim();
        extern IPAddress targetIP;
        if (targetIP.fromString(ipStr)) {
            Serial.print("Target IP set to "); Serial.println(targetIP);
            _saveCallback();
        } else {
            Serial.println("Invalid IP address format. Example: TARGET 192.168.1.100");
        }
    } else if (cmd.startsWith("MODE ")) {
        String modeStr = cmd.substring(5);
        modeStr.trim();
        modeStr.toUpperCase();
        extern uint8_t outputMode;
        extern String wifiSSID;
        extern bool wifiConnected;
        extern void connectWiFi();

        if (modeStr == "SERIAL") {
            outputMode = 0;
            WiFi.mode(WIFI_OFF);
            wifiConnected = false;
            Serial.println("Output Mode set to SERIAL.");
        } else if (modeStr == "WIFI") {
            outputMode = 1;
            Serial.println("Output Mode set to WIFI.");
            if (wifiSSID.length() > 0 && !wifiConnected) {
                connectWiFi();
            }
        } else if (modeStr == "BOTH") {
            outputMode = 2;
            Serial.println("Output Mode set to BOTH (Serial & WiFi).");
            if (wifiSSID.length() > 0 && !wifiConnected) {
                connectWiFi();
            }
        } else if (modeStr == "BLE") {
            outputMode = 3;
            extern String deviceID;
            extern BLEStream bleStream;
            bleStream.begin(deviceID);
            Serial.println("Output Mode set to BLE (Bluetooth 5.0 Long Range).");
        } else if (modeStr == "LORA") {
            outputMode = 4;
            #if defined(LORA_CS_PIN)
            extern LoRaStream loraStream;
            loraStream.begin(LORA_CS_PIN, LORA_DIO1_PIN, LORA_RST_PIN, LORA_BUSY_PIN, LORA_SCK_PIN, LORA_MISO_PIN, LORA_MOSI_PIN);
            #endif
            Serial.println("Output Mode set to LORA (Sub-GHz SX1262 LoRa).");
        } else {
            Serial.println("Usage: MODE <SERIAL|WIFI|BOTH|BLE|LORA>");
        }
        _saveCallback();
    } else if (cmd.length() > 0) {
        Serial.print("Unknown command: ");
        Serial.println(cmd);
    }
}
