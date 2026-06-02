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
    SPI.begin(18, 19, 23, 5); // Explicitly define SCK=18, MISO=19, MOSI=23, CS=5 for VSPI
    
    if (!sensor->begin()) {
        Serial.print("Failed to find ");
        Serial.print(sensor->getSensorName());
        Serial.println("! Check wiring.");
        while (1) {delay(1000);
            Serial.println("Failed to find sensor! Check wiring.");
        }
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
    
  pinMode(2, OUTPUT);  // change state of the LED by setting the pin to the HIGH voltage level
}

void loop() {
    // Non-blocking LED blink and "Running" message every 1 second
    static unsigned long lastBlinkTime = 0;
    static bool ledState = false;
    unsigned long currentMillis = millis();
    
    if (currentMillis - lastBlinkTime >= 1000) {
        lastBlinkTime = currentMillis;
        ledState = !ledState;
        digitalWrite(2, ledState ? HIGH : LOW);
        //if (ledState) {
        //    Serial.println("Running");
        //}
    }

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
