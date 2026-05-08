#include <Arduino.h>
#include <SPI.h>
#include <Preferences.h>
#include "RM3100.h"

// Pin Definitions
#define RM3100_CS_PIN   5
#define RM3100_DRDY_PIN 4

Preferences prefs;
RM3100 rm_sensor(RM3100_CS_PIN, RM3100_DRDY_PIN);
Magnetometer* sensor = &rm_sensor;

bool streaming = true;
uint8_t current_rate = 0x92;
String inputBuffer = "";

void saveSettings() {
    prefs.begin("mcu_v0", false);
    prefs.putBool("streaming", streaming);
    prefs.putUChar("rate", current_rate);
    prefs.end();
}

void loadSettings() {
    prefs.begin("mcu_v0", true);
    streaming = prefs.getBool("streaming", true);
    current_rate = prefs.getUChar("rate", 0x92);
    prefs.end();
}

void printHelp() {
    Serial.println("--- Magnetometer CLI Help ---");
    Serial.println("HELP          - Show this help");
    Serial.println("STREAM ON/OFF - Enable/Disable data streaming");
    Serial.println("RATE <hex>    - Set rate code (saved)");
    Serial.println("CYCLE <int>   - Set cycle count (RM3100 specific)");
    Serial.println("STATUS        - Show current status");
    Serial.println("-----------------------");
}

void handleCommand(String cmd) {
    cmd.trim();
    cmd.toUpperCase();

    if (cmd == "HELP") {
        printHelp();
    } else if (cmd == "STREAM ON") {
        streaming = true;
        saveSettings();
        Serial.println("Streaming enabled.");
    } else if (cmd == "STREAM OFF") {
        streaming = false;
        saveSettings();
        Serial.println("Streaming disabled.");
    } else if (cmd.startsWith("RATE ")) {
        String valStr = cmd.substring(5);
        current_rate = (uint8_t)strtol(valStr.c_str(), NULL, 16);
        sensor->setContinuousMode(true, current_rate);
        saveSettings();
        Serial.print("Rate set to 0x");
        Serial.println(current_rate, HEX);
    } else if (cmd.startsWith("CYCLE ")) {
        String valStr = cmd.substring(6);
        uint16_t cycle = (uint16_t)valStr.toInt();
        if (sensor->getSensorName() == "RM3100") {
            static_cast<RM3100*>(sensor)->setCycleCount(cycle, cycle, cycle);
            Serial.print("Cycle count set to ");
            Serial.println(cycle);
        } else {
            Serial.println("CYCLE command not supported for this sensor.");
        }
    } else if (cmd == "STATUS") {
        Serial.print("Streaming: "); Serial.println(streaming ? "ON" : "OFF");
        Serial.print("Sensor: "); Serial.println(sensor->getSensorName());
        Serial.print("Rate Code: 0x"); Serial.println(current_rate, HEX);
        Serial.println(sensor->getStatusString());
    } else if (cmd.length() > 0) {
        Serial.print("Unknown command: ");
        Serial.println(cmd);
    }
}

void setup() {
    Serial.begin(115200);
    while (!Serial) delay(10);
    
    inputBuffer.reserve(64); // Prevent frequent reallocations

    loadSettings();

    Serial.println("Initializing Magnetometer...");

    SPI.begin();
    
    if (!sensor->begin()) {
        Serial.print("Failed to find ");
        Serial.print(sensor->getSensorName());
        Serial.println("! Check wiring.");
        while (1) delay(10);
    }

    Serial.print("Sensor Found: ");
    Serial.println(sensor->getSensorName());
    Serial.println(sensor->getStatusString());

    // Sensor specific setup
    if (sensor->getSensorName() == "RM3100") {
        static_cast<RM3100*>(sensor)->setCycleCount(200, 200, 200);
    }
    
    // Resume continuous mode with saved rate
    sensor->setContinuousMode(true, current_rate);

    Serial.println("timestamp_us,x,y,z");
    printHelp();
}

void loop() {
    // CLI Parsing
    while (Serial.available()) {
        char c = Serial.read();
        if (c == '\n' || c == '\r') {
            if (inputBuffer.length() > 0) {
                handleCommand(inputBuffer);
                inputBuffer = "";
            }
        } else {
            inputBuffer += c;
        }
    }

    // Data Streaming
    if (streaming && sensor->dataReady()) {
        int32_t x, y, z;
        sensor->readXYZ(x, y, z);
        
        Serial.print(micros());
        Serial.print(",");
        Serial.print(x);
        Serial.print(",");
        Serial.print(y);
        Serial.print(",");
        Serial.println(z);
    }
}
