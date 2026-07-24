#include "CLI.h"
#include "RM3100.h"
#include "FLC100_ADS131.h"

CLI::CLI(Magnetometer* sensor, bool& streaming, uint8_t& current_rate, void (*saveCallback)())
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
    Serial.println("STATUS        - Show current status");
    Serial.println("-----------------------");
}

void CLI::handleCommand(String cmd) {
    cmd.trim();
    cmd.toUpperCase();

    if (cmd == "HELP") {
        printHelp();
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
            Serial.print("Cycle count set to ");
            Serial.println(cycle);
        } else {
            Serial.println("CYCLE command not supported for this sensor.");
        }
    } else if (cmd == "STATUS") {
        Serial.print("Streaming: "); Serial.println(_streaming ? "ON" : "OFF");
        Serial.print("Sensor: "); Serial.println(_sensor->getSensorName());
        Serial.print("Rate Code: 0x"); Serial.println(_current_rate, HEX);
        Serial.println(_sensor->getStatusString());
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
    } else if (cmd.length() > 0) {
        Serial.print("Unknown command: ");
        Serial.println(cmd);
    }
}
