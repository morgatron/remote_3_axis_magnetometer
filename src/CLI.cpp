#include "CLI.h"
#include "board_config.h"

CLI::CLI(SensorManager& sensorManager, TelemetryEgress& telemetry, SensorConfig& config, 
         ChangeCallback changeCallback, ActivityCallback activityCallback)
    : _sensorManager(sensorManager), _telemetry(telemetry), _config(config), 
      _changeCallback(changeCallback), _activityCallback(activityCallback) {}

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
    Serial.println("BATCH <1-18>  - Set samples per burst (1 = instant 1Hz, 10 = 10s batch for BLE & LoRa)");
    Serial.println("POWER <-9..22>- Set LoRa TX power in dBm");
    Serial.println("SENSOR <name> - Switch sensor (RM3100, FLC100, MOCK)");
    Serial.println("STATUS        - Show current status");
    Serial.println("-----------------------");
}

void CLI::handleCommand(String cmd) {
    if (_activityCallback) _activityCallback();

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
        if (_sensorManager.getActiveSensor() != nullptr) {
            _sensorManager.getActiveSensor()->setContinuousMode(true, _config.rate);
        }
        _config.save();
        if (_changeCallback) _changeCallback();
        Serial.print("Rate set to 0x");
        Serial.println(_config.rate, HEX);
    } else if (cmd.startsWith("CYCLE ")) {
        String valStr = cmd.substring(6);
        uint16_t cycle = (uint16_t)valStr.toInt();
        if (_sensorManager.setCycleCount(cycle)) {
            _config.cycleCount = cycle;
            _config.save();
            if (_changeCallback) _changeCallback();
            float gain = 0.3671f * (float)cycle + 1.5f;
            Serial.printf("Cycle count set to %u (Gain: %.2f LSB/uT, Scale: %.2f nT/LSB)\r\n",
                          cycle, gain, 1000.0f / gain);
        } else {
            Serial.println("CYCLE command not supported for active sensor.");
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
        if (_sensorManager.switchSensorByName(sub)) {
            _config.save();
            if (_changeCallback) _changeCallback();
        }
    } else if (cmd == "STATUS") {
        Serial.print("Device ID: "); Serial.println(_config.deviceID);
        Serial.print("Streaming: "); Serial.println(_config.streaming ? "ON" : "OFF");
        if (_sensorManager.getActiveSensor() != nullptr) {
            Serial.print("Sensor: "); Serial.println(_sensorManager.getActiveSensor()->getSensorName());
            Serial.println(_sensorManager.getActiveSensor()->getStatusString());
        }
        Serial.print("Output Mode: "); Serial.println(_config.getModeString());
        Serial.print("Downsample: "); Serial.print(_config.downsample); Serial.println("x");
        Serial.printf("CPU Frequency: %d MHz\r\n", getCpuFrequencyMhz());
        Serial.print("Rate Code: 0x"); Serial.println(_config.rate, HEX);
        Serial.print("Batch Burst Size: "); Serial.print(_config.batchSize); Serial.println(" samples/burst");
#if defined(BOARD_HAS_LORA)
        Serial.printf("LoRa SX1262: %s (TX: +%d dBm)\r\n", 
                      _telemetry.isLoRaInitialized() ? "INITIALIZED" : "NOT INITIALIZED",
                      _telemetry.getLoRaPower());
#endif
    } else if (cmd.startsWith("POWER ")) {
        int8_t pwr = (int8_t)cmd.substring(6).toInt();
        if (pwr >= -9 && pwr <= 22) {
#if defined(BOARD_HAS_LORA)
            _telemetry.setLoRaPower(pwr);
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
        if (_sensorManager.setTestSignal(true)) {
            Serial.println("Internal test signal enabled (1Hz square wave).");
        } else {
            Serial.println("TEST command only supported for FLC100-ADS131E08.");
        }
    } else if (cmd == "TEST OFF") {
        if (_sensorManager.setTestSignal(false)) {
            Serial.println("Internal test signal disabled. Normal inputs active.");
        }
    } else if (cmd.startsWith("GAIN ")) {
        uint8_t gain = (uint8_t)cmd.substring(5).toInt();
        if (_sensorManager.setPgaGain(gain)) {
            _config.save();
            if (_changeCallback) _changeCallback();
            Serial.print("PGA Gain set to ");
            Serial.println(gain);
        }
    } else if (cmd.startsWith("VREF ")) {
        float vref = cmd.substring(5).toFloat();
        if (_sensorManager.setVref(vref)) {
            Serial.print("VREF set to ");
            Serial.print(vref);
            Serial.println(" V");
        }
    } else if (cmd.startsWith("SSID ")) {
        String ssid = originalCmd.substring(5);
        ssid.trim();
        _config.wifiSSID = ssid;
        _config.save();
        Serial.print("WiFi SSID set to "); Serial.println(_config.wifiSSID);
        if (_changeCallback) _changeCallback();
        if (_config.wifiSSID.length() > 0 && _config.wifiPass.length() > 0) {
            _telemetry.connectWiFi();
        }
    } else if (cmd.startsWith("PASS ")) {
        String pass = originalCmd.substring(5);
        pass.trim();
        _config.wifiPass = pass;
        _config.save();
        Serial.println("WiFi Password updated.");
        if (_changeCallback) _changeCallback();
        if (_config.wifiSSID.length() > 0 && _config.wifiPass.length() > 0) {
            _telemetry.connectWiFi();
        }
    } else if (cmd.startsWith("WIFI ")) {
        String rawSub = originalCmd.substring(5);
        rawSub.trim();
        if (rawSub.length() > 0 && rawSub.charAt(0) == '"') {
            int closeQuote = rawSub.indexOf('"', 1);
            if (closeQuote > 1) {
                _config.wifiSSID = rawSub.substring(1, closeQuote);
                String rest = rawSub.substring(closeQuote + 1);
                rest.trim();
                if (rest.startsWith("\"") && rest.endsWith("\"") && rest.length() >= 2) {
                    _config.wifiPass = rest.substring(1, rest.length() - 1);
                } else {
                    _config.wifiPass = rest;
                }
                _config.wifiSSID.trim();
                _config.wifiPass.trim();
                _config.save();
                _telemetry.connectWiFi();
                if (_changeCallback) _changeCallback();
            } else {
                Serial.println("Usage: WIFI \"SSID with spaces\" \"Password\"");
            }
        } else {
            rawSub.trim();
            int spaceIdx = rawSub.lastIndexOf(' ');
            if (spaceIdx > 0) {
                _config.wifiSSID = rawSub.substring(0, spaceIdx);
                _config.wifiPass = rawSub.substring(spaceIdx + 1);
                _config.wifiSSID.trim();
                _config.wifiPass.trim();
                _config.save();
                _telemetry.connectWiFi();
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

        if (modeStr == "SERIAL") {
            _telemetry.setOutputMode(MODE_SERIAL);
        } else if (modeStr == "WIFI") {
            _telemetry.setOutputMode(MODE_WIFI);
        } else if (modeStr == "BOTH") {
            _telemetry.setOutputMode(MODE_BOTH);
        } else if (modeStr == "BLE") {
            _telemetry.setOutputMode(MODE_BLE);
        } else if (modeStr == "LORA") {
            _telemetry.setOutputMode(MODE_LORA);
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
        Serial.println("Type HELP for available commands.");
    }
}
