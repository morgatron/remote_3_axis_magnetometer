#include <Arduino.h>
#include <SPI.h>
#include <Preferences.h>
// Define the sensor type to use
// Define the sensor type to use
#define SENSOR_TYPE_RM3100   0
#define SENSOR_TYPE_FLC100   1

// CHANGE THIS LINE TO SWITCH SENSORS:
#define SENSOR_TYPE          SENSOR_TYPE_RM3100

#if (SENSOR_TYPE == SENSOR_TYPE_RM3100)
#include "RM3100.h"
#define CS_PIN 5
#define DRDY_PIN 4
RM3100 magnetometer(CS_PIN, DRDY_PIN);
const uint8_t DEFAULT_RATE = 0x96; // RM3100 default rate: 0x96 (~37 Hz)
#else
#include "FLC100_ADS131.h"
#define CS_PIN 5
#define DRDY_PIN 4
#define RESET_PIN -1
FLC100_ADS131 magnetometer(CS_PIN, DRDY_PIN, RESET_PIN);
const uint8_t DEFAULT_RATE = 0x06; // ADS131E08 default rate (1kSPS)
#endif

#include "CLI.h"

Preferences prefs;
Magnetometer* sensor = &magnetometer;

bool streaming = true;
uint8_t current_rate = DEFAULT_RATE;

void saveSettings() {
    prefs.begin("mcu_v0", false);
    prefs.putBool("streaming", streaming);
    prefs.putUChar("rate", current_rate);
    prefs.end();
}

void loadSettings() {
    prefs.begin("mcu_v0", true);
    streaming = prefs.getBool("streaming", true);
    current_rate = prefs.getUChar("rate", DEFAULT_RATE);
    prefs.end();
}

CLI serialCLI(sensor, streaming, current_rate, saveSettings);

void setup() {
    Serial.begin(921600);
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
#if (SENSOR_TYPE == SENSOR_TYPE_RM3100)
    static_cast<RM3100*>(sensor)->setCycleCount(200, 200, 200);
#elif (SENSOR_TYPE == SENSOR_TYPE_FLC100)
    // Set calibration: VREF = 2.4V (standard for 3.3V systems), Sensitivity = 20.0 uV/nT, Gain = 1
    static_cast<FLC100_ADS131*>(sensor)->setCalibration(2.4f, 20.0f, 1);
#endif
    
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
