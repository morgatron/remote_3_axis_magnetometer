#include "CLI.h"
#include "board_config.h"
#include "RM3100.h"
#include "FLC100_ADS131.h"
#include "MockSensor.h"
#include "BLEStream.h"
#if defined(BOARD_HAS_LORA)
#include "LoRaStream.h"
#endif
#include <WiFi.h>

CLI::CLI(Magnetometer*& sensor, SensorConfig& config, ChangeCallback changeCallback)
    : _sensor(sensor), _config(config), _changeCallback(changeCallback) {}

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
    Serial.println("BATCH <1-10>  - Set samples per burst (1 = instant 1Hz, 10 = 10s batch for BLE & LoRa)");
    Serial.println("POWER <-9..22>- Set LoRa TX power in dBm");
    Serial.println("STATUS        - Show current status");
    Serial.println("-----------------------");
}

void CLI::handleCommand(String cmd) {
#if defined(HELTEC_V4) || defined(ARDUINO_heltec_wifi_lora_32_V3)
    extern uint32_t lastOledActivityMs;
    lastOledActivityMs = millis(); // Refresh OLED wake timer upon user CLI interaction
#endif

    String originalCmd = cmd;
    originalCmd.trim();

    cmd.trim();
    cmd.toUpperCase();

    if (cmd == "HELP") {
        printHelp();
    } else if (cmd.startsWith("BATCH ")) {
        uint8_t count = (uint8_t)cmd.substring(6).toInt();
        if (count >= 1 && count <= 18) {
            _config.batchSize = count;
            _config.save();
            if (_changeCallback) _changeCallback();
            Serial.printf("Batch Burst Size set to %d samples per burst (BLE & LoRa).\r\n", _config.batchSize);
        } else {
            Serial.println("Usage: BATCH <1-18> (Set samples per burst for BLE & LoRa)");
        }
    } else if (cmd == "STREAM ON" || cmd == "START") {
        _config.streaming = true;
        _config.save();
        if (_changeCallback) _changeCallback();
        Serial.println("Streaming enabled.");
    } else if (cmd == "STREAM OFF" || cmd == "STOP") {
        _config.streaming = false;
        _config.save();
        if (_changeCallback) _changeCallback();
        Serial.println("Streaming disabled.");
    } else if (cmd.startsWith("RATE ")) {
        String valStr = cmd.substring(5);
        _config.rate = (uint8_t)strtol(valStr.c_str(), NULL, 16);
        extern TaskHandle_t adcTaskHandle;
        extern void IRAM_ATTR drdyISR();
        if (adcTaskHandle) vTaskSuspend(adcTaskHandle);
        detachInterrupt(digitalPinToInterrupt(DRDY_PIN));
        _sensor->setContinuousMode(true, _config.rate);
        attachInterrupt(digitalPinToInterrupt(DRDY_PIN), drdyISR, _config.sensorType == 0 ? RISING : FALLING);
        if (adcTaskHandle) vTaskResume(adcTaskHandle);
        _config.save();
        if (_changeCallback) _changeCallback();
        Serial.print("Rate set to 0x");
        Serial.println(_config.rate, HEX);
    } else if (cmd.startsWith("CYCLE ")) {
        String valStr = cmd.substring(6);
        uint16_t cycle = (uint16_t)valStr.toInt();
        if (_sensor->getSensorName() == "RM3100" && cycle > 0) {
            extern TaskHandle_t adcTaskHandle;
            extern void IRAM_ATTR drdyISR();
            if (adcTaskHandle) vTaskSuspend(adcTaskHandle);
            detachInterrupt(digitalPinToInterrupt(DRDY_PIN));

            static_cast<RM3100*>(_sensor)->setCycleCount(cycle, cycle, cycle);
            _config.cycleCount = cycle;

            attachInterrupt(digitalPinToInterrupt(DRDY_PIN), drdyISR, RISING);
            if (adcTaskHandle) vTaskResume(adcTaskHandle);
            _config.save();
            if (_changeCallback) _changeCallback();
            float gain = 0.3671f * (float)cycle + 1.5f;
            Serial.printf("Cycle count set to %u (Gain: %.2f LSB/uT, Scale: %.2f nT/LSB)\r\n",
                          cycle, gain, 1000.0f / gain);
        } else {
            Serial.println("CYCLE command not supported for this sensor.");
        }
    } else if (cmd.startsWith("DOWNSAMPLE ")) {
        uint16_t ratio = (uint16_t)cmd.substring(11).toInt();
        if (ratio >= 1) {
            _config.downsample = ratio;
            _config.save();
            if (_changeCallback) _changeCallback();
            Serial.print("Downsample ratio set to ");
            Serial.print(_config.downsample);
            Serial.println("x");
        }
    } else if (cmd.startsWith("SENSOR ")) {
        String sub = cmd.substring(7);
        sub.trim();
        extern Magnetometer* sensor;
        extern RM3100 sensorRM3100;
        extern FLC100_ADS131 sensorFLC100;
        extern MockSensor sensorMock;
        uint8_t newType = _config.sensorType;
        if (sub == "RM3100" || sub == "0") {
            newType = SENSOR_TYPE_RM3100;
            sensor = &sensorRM3100;
            _config.rate = 0x96; // RM3100 default rate 37 Hz
        } else if (sub == "FLC100" || sub == "ADS131" || sub == "1") {
            newType = SENSOR_TYPE_FLC100;
            sensor = &sensorFLC100;
            _config.rate = 0x06; // ADS131 default rate 1 kSPS
            if (_config.downsample <= 1) _config.downsample = 1000;
        } else if (sub == "MOCK" || sub == "2") {
            newType = SENSOR_TYPE_MOCK;
            sensor = &sensorMock;
            _config.rate = 0x95; // Synthetic Mock default rate 75 Hz
        }
        extern void IRAM_ATTR drdyISR();
        _config.sensorType = newType;
        _sensor = sensor;

        Serial.print("Active sensor set to: ");
        Serial.println(_sensor->getSensorName());

        // Ensure SPI bus is active
        SPI.begin(SCK_PIN, MISO_PIN, MOSI_PIN, CS_PIN);
        pinMode(DRDY_PIN, INPUT_PULLUP);

        if (newType == SENSOR_TYPE_MOCK) {
            detachInterrupt(digitalPinToInterrupt(DRDY_PIN));
        }

        if (!sensor->begin()) {
            Serial.println("Warning: Selected sensor failed initialization!");
        } else {
            if (newType == SENSOR_TYPE_FLC100) {
                static_cast<FLC100_ADS131*>(sensor)->setCalibration(2.4f, 20.0f, 1);
                attachInterrupt(digitalPinToInterrupt(DRDY_PIN), drdyISR, FALLING);
            } else if (newType == SENSOR_TYPE_RM3100) {
                static_cast<RM3100*>(sensor)->setCycleCount(200, 200, 200);
                attachInterrupt(digitalPinToInterrupt(DRDY_PIN), drdyISR, RISING);
            }
            sensor->setContinuousMode(true, _config.rate);
            Serial.println("Sensor initialized and continuous mode started.");
        }
        _config.save();
        if (_changeCallback) _changeCallback();
    } else if (cmd == "STATUS") {
        Serial.print("Device ID: "); Serial.println(_config.deviceID);
        Serial.print("Streaming: "); Serial.println(_config.streaming ? "ON" : "OFF");
        Serial.print("Sensor: "); Serial.println(_sensor->getSensorName());
        Serial.print("Output Mode: "); Serial.println(_config.getModeString());
        Serial.print("Downsample: "); Serial.print(_config.downsample); Serial.println("x");
        Serial.printf("CPU Frequency: %d MHz\r\n", getCpuFrequencyMhz());
        Serial.print("Rate Code: 0x"); Serial.println(_config.rate, HEX);
        Serial.print("Batch Burst Size: "); Serial.print(_config.batchSize); Serial.println(" samples/burst");
#if defined(BOARD_HAS_LORA)
        extern LoRaStream loraStream;
        Serial.printf("LoRa SX1262: %s (TX: +%d dBm)\r\n", 
                      loraStream.isInitialized() ? "INITIALIZED" : "NOT INITIALIZED",
                      loraStream.getOutputPower());
#endif
        Serial.println(_sensor->getStatusString());
    } else if (cmd.startsWith("POWER ")) {
        int8_t pwr = (int8_t)cmd.substring(6).toInt();
        if (pwr >= -9 && pwr <= 22) {
#if defined(BOARD_HAS_LORA)
            extern LoRaStream loraStream;
            loraStream.setOutputPower(pwr);
            Serial.printf("LoRa Transmit Power set to +%d dBm.\r\n", pwr);
#else
            Serial.println("LoRa hardware not supported on this board.");
#endif
        } else {
            Serial.println("Usage: POWER <-9 to 22> (Set LoRa TX power in dBm)");
        }
    } else if (cmd.startsWith("ID ")) {
        String newID = cmd.substring(3);
        newID.trim();
        if (newID.length() > 0) {
            _config.deviceID = newID;
            _config.save();
            Serial.print("Device ID set to "); Serial.println(_config.deviceID);
            if (_changeCallback) _changeCallback();
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
            _config.save();
            if (_changeCallback) _changeCallback();
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
        extern void connectWiFi();
        _config.wifiSSID = ssid;
        _config.save();
        Serial.print("WiFi SSID set to "); Serial.println(_config.wifiSSID);
        if (_changeCallback) _changeCallback();
        if (_config.wifiSSID.length() > 0 && _config.wifiPass.length() > 0) {
            connectWiFi();
        }
    } else if (cmd.startsWith("PASS ")) {
        String pass = originalCmd.substring(5);
        pass.trim();
        extern void connectWiFi();
        _config.wifiPass = pass;
        _config.save();
        Serial.println("WiFi Password updated.");
        if (_changeCallback) _changeCallback();
        if (_config.wifiSSID.length() > 0 && _config.wifiPass.length() > 0) {
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
                extern void connectWiFi();
                _config.wifiSSID = rawSub.substring(0, spaceIdx);
                _config.wifiPass = rawSub.substring(spaceIdx + 1);
                _config.wifiSSID.trim();
                _config.wifiPass.trim();
                _config.save();
                connectWiFi();
                if (_changeCallback) _changeCallback();
            } else {
                Serial.println("Usage: WIFI <ssid> <password>");
            }
        }
    } else if (cmd.startsWith("TARGET ")) {
        String ipStr = cmd.substring(7);
        ipStr.trim();
        if (_config.targetIP.fromString(ipStr)) {
            _config.save();
            Serial.print("Target IP set to "); Serial.println(_config.targetIP);
            if (_changeCallback) _changeCallback();
        } else {
            Serial.println("Invalid IP address format. Example: TARGET 192.168.1.100");
        }
    } else if (cmd.startsWith("MODE ")) {
        String modeStr = cmd.substring(5);
        modeStr.trim();
        modeStr.toUpperCase();
        extern bool wifiConnected;
        extern void connectWiFi();
        extern BLEStream bleStream;
        extern void configurePowerManagement();

        if (modeStr == "SERIAL") {
            _config.outputMode = MODE_SERIAL;
            WiFi.mode(WIFI_OFF);
            wifiConnected = false;
            bleStream.powerDownModem();
            configurePowerManagement();
            Serial.println("Output Mode set to SERIAL.");
        } else if (modeStr == "WIFI") {
            _config.outputMode = MODE_WIFI;
            bleStream.powerDownModem();
            configurePowerManagement();
            Serial.println("Output Mode set to WIFI.");
            if (_config.wifiSSID.length() > 0 && !wifiConnected) {
                connectWiFi();
            }
        } else if (modeStr == "BOTH") {
            _config.outputMode = MODE_BOTH;
            bleStream.powerDownModem();
            configurePowerManagement();
            Serial.println("Output Mode set to BOTH (Serial & WiFi).");
            if (_config.wifiSSID.length() > 0 && !wifiConnected) {
                connectWiFi();
            }
        } else if (modeStr == "BLE") {
            _config.outputMode = MODE_BLE;
            WiFi.mode(WIFI_OFF);
            wifiConnected = false;
            bleStream.begin(_config.deviceID);
            configurePowerManagement();
            Serial.println("Output Mode set to BLE (Bluetooth 5.0 Long Range).");
        } else if (modeStr == "LORA") {
            _config.outputMode = MODE_LORA;
            WiFi.mode(WIFI_OFF);
            wifiConnected = false;
            bleStream.powerDownModem();
            configurePowerManagement();
            #if defined(BOARD_HAS_LORA)
            extern LoRaStream loraStream;
            loraStream.begin(LORA_CS_PIN, LORA_DIO1_PIN, LORA_RST_PIN, LORA_BUSY_PIN, LORA_SCK_PIN, LORA_MISO_PIN, LORA_MOSI_PIN);
            Serial.println("Output Mode set to LORA (Sub-GHz SX1262 LoRa).");
            #else
            Serial.println("LORA mode not supported on this hardware.");
            #endif
        } else {
            Serial.println("Usage: MODE <SERIAL|WIFI|BOTH|BLE|LORA>");
        }
        _config.save();
        if (_changeCallback) _changeCallback();
    } else if (cmd == "SAVE") {
        _config.save();
        if (_changeCallback) _changeCallback();
        Serial.println("Configuration saved to NVS.");
    } else if (cmd.length() > 0) {
        Serial.print("Unknown command: ");
        Serial.println(cmd);
    }
}
