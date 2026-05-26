#include <Arduino.h>
#include <SPI.h>
#include <Preferences.h>
#include "RM3100.h"
#include "CLI.h"

// Pin Definitions
#define RM3100_CS_PIN   5
#define RM3100_DRDY_PIN 4

Preferences prefs;
RM3100 rm_sensor(RM3100_CS_PIN, RM3100_DRDY_PIN);
Magnetometer* sensor = &rm_sensor;

bool streaming = true;
uint8_t current_rate = 0x92;

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

CLI serialCLI(sensor, streaming, current_rate, saveSettings);

void setup() {
    Serial.begin(115200);
    while (!Serial) delay(10);
    
    serialCLI.begin();

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
    serialCLI.printHelp();
}

void loop() {
    // CLI Parsing
    serialCLI.update();

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
