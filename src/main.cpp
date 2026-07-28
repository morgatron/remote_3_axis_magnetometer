#include <Arduino.h>
#include <SPI.h>
#include <Preferences.h>
// Define the sensor type to use
// Define the sensor type to use
#define SENSOR_TYPE_RM3100   0
#define SENSOR_TYPE_FLC100   1

// CHANGE THIS LINE TO SWITCH SENSORS:
#define SENSOR_TYPE          SENSOR_TYPE_FLC100

#if defined(CONFIG_IDF_TARGET_ESP32C3) || defined(ARDUINO_ARCH_ESP32C3)
// Custom ESP32-C3 PCB Pinout
#define SCK_PIN      6
#define MOSI_PIN     7
#define MISO_PIN     2
#define DRDY_PIN     3
#define CS_PIN       10
#define CLK_GEN_PIN  1
#define LED_PIN      -1
#else
// Classic ESP32 Dev Module Pinout
#define SCK_PIN      18
#define MISO_PIN     19
#define MOSI_PIN     23
#define CS_PIN       5
#define DRDY_PIN     4
#define CLK_GEN_PIN  27
#define LED_PIN      2
#endif

#define RESET_PIN   -1

#if (SENSOR_TYPE == SENSOR_TYPE_RM3100)
#include "RM3100.h"
RM3100 magnetometer(CS_PIN, DRDY_PIN);
const uint8_t DEFAULT_RATE = 0x96; // RM3100 default rate: 0x96 (~37 Hz)
#else
#include "FLC100_ADS131.h"
FLC100_ADS131 magnetometer(CS_PIN, DRDY_PIN, RESET_PIN);
const uint8_t DEFAULT_RATE = 0x06; // ADS131E08 default rate (1kSPS)
#endif

#include "CLI.h"
#include <WiFi.h>
#include <WiFiUdp.h>

Preferences prefs;
Magnetometer* sensor = &magnetometer;

bool streaming = true;
uint8_t current_rate = DEFAULT_RATE;
uint16_t current_downsample = 1;

// WiFi & UDP Globals
WiFiUDP udp;
IPAddress targetIP(255, 255, 255, 255); // Default broadcast
uint16_t targetPort = 9876;
uint16_t udpListenPort = 9876;

enum OutputMode { MODE_SERIAL = 0, MODE_WIFI = 1, MODE_BOTH = 2 };
uint8_t outputMode = MODE_SERIAL;
String wifiSSID = "";
String wifiPass = "";
bool wifiConnected = false;

void saveSettings() {
    prefs.begin("mcu_v0", false);
    prefs.putBool("streaming", streaming);
    prefs.putUChar("rate", current_rate);
    prefs.putUShort("downsample", current_downsample);
    prefs.putUChar("mode", outputMode);
    prefs.putString("ssid", wifiSSID);
    prefs.putString("pass", wifiPass);
    prefs.putString("target", targetIP.toString());
    prefs.end();
}

void connectWiFi() {
    if (wifiSSID.length() > 0) {
        Serial.print("Connecting to WiFi network: ");
        Serial.println(wifiSSID);
        WiFi.mode(WIFI_STA);
        WiFi.begin(wifiSSID.c_str(), wifiPass.c_str());
        
        unsigned long start = millis();
        while (WiFi.status() != WL_CONNECTED && (millis() - start < 8000)) {
            delay(250);
            Serial.print(".");
        }
        Serial.println();

        if (WiFi.status() == WL_CONNECTED) {
            wifiConnected = true;
            Serial.print("WiFi Connected! IP Address: ");
            Serial.println(WiFi.localIP());
            udp.begin(udpListenPort);
            Serial.print("UDP Receiver active on port ");
            Serial.println(udpListenPort);
        } else {
            wifiConnected = false;
            Serial.println("WiFi Connection Failed.");
        }
    }
}

void loadSettings() {
    prefs.begin("mcu_v0", true);
    streaming = prefs.getBool("streaming", true);
    current_rate = prefs.getUChar("rate", DEFAULT_RATE);
    current_downsample = prefs.getUShort("downsample", 1);
    outputMode = prefs.getUChar("mode", MODE_SERIAL);
    wifiSSID = prefs.getString("ssid", "");
    wifiPass = prefs.getString("pass", "");
    String tIP = prefs.getString("target", "255.255.255.255");
    targetIP.fromString(tIP);
    prefs.end();

    if (wifiSSID.length() > 0) {
        connectWiFi();
    }
}

void sendOutputSample(uint64_t ts, int32_t x, int32_t y, int32_t z, uint32_t status = 0xC00000) {
    char line[128];
    snprintf(line, sizeof(line), "%llu,%ld,%ld,%ld,%06X", (unsigned long long)ts, (long)x, (long)y, (long)z, (unsigned int)(status & 0xFFFFFF));

    if (outputMode == MODE_SERIAL || outputMode == MODE_BOTH) {
        Serial.println(line);
    }

    if ((outputMode == MODE_WIFI || outputMode == MODE_BOTH) && wifiConnected) {
        udp.beginPacket(targetIP, targetPort);
        udp.println(line);
        udp.endPacket();
    }
}

CLI serialCLI(sensor, streaming, current_rate, saveSettings);

// Set to 1 if CLKSEL is tied to Vin/3.3V (using ADS131E08 internal 2.048MHz oscillator).
// Set to 0 if CLKSEL is tied to GND (requiring external clock on CLKIN pin).
#define USE_INTERNAL_CLOCK   1

void startClockGenerator() {
#if !USE_INTERNAL_CLOCK
    pinMode(CLK_GEN_PIN, OUTPUT);
#if defined(ESP_ARDUINO_VERSION) && (ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0))
    ledcAttach(CLK_GEN_PIN, 2048000, 4); // 4-bit resolution
    ledcWrite(CLK_GEN_PIN, 8);           // 50% duty (8 of 16)
#else
    ledcSetup(0, 2048000, 4);            // Channel 0, 2.048 MHz, 4-bit resolution
    ledcAttachPin(CLK_GEN_PIN, 0);
    ledcWrite(0, 8);                     // 50% duty (8 of 16)
#endif
#else
    pinMode(CLK_GEN_PIN, INPUT);         // Leave pin floating/high-Z when using internal clock
#endif
}

volatile bool drdyInterruptFlag = false;
volatile uint64_t lastDrdyTimeUs = 0;
volatile uint32_t lastDrdyIntervalUs = 0;
volatile uint32_t drdyAnomalyCount = 0;

void IRAM_ATTR drdyISR() {
    uint64_t now = esp_timer_get_time();
    if (lastDrdyTimeUs > 0) {
        uint32_t dt = (uint32_t)(now - lastDrdyTimeUs);
        lastDrdyIntervalUs = dt;
        // Expected interval at 1 kSPS is 1000 us (+/- 200 us). Flag any gap outside 800 us - 1200 us.
        if (dt < 800 || dt > 1200) {
            drdyAnomalyCount++;
        }
    }
    lastDrdyTimeUs = now;
    drdyInterruptFlag = true;
}

void setup() {
#if defined(CONFIG_IDF_TARGET_ESP32C3) || defined(ARDUINO_ARCH_ESP32C3)
    Serial.setTxTimeoutMs(0);
#endif
    Serial.begin(921600);
    // Timeout waiting for Serial so board boots even without open Serial Monitor
    unsigned long startWait = millis();
    while (!Serial && (millis() - startWait < 3000)) delay(10);
    
    Serial.println("\r\n==================================================");
    Serial.println(" FIRMWARE: ADS131E08 HW Interrupt & Spike Filter v2.2");
    Serial.println("==================================================");

#if !USE_INTERNAL_CLOCK
    // Start generating external clock for the ADS131E08 (since CLKSEL is tied low)
    startClockGenerator();
    delay(200); // Give the ADS131E08 time to complete its internal POR with external clock
#else
    // When CLKSEL is tied to Vin, wait for internal 2.048MHz oscillator to start up & stabilize
    delay(250);
#endif
    
    serialCLI.begin();

    loadSettings();

    Serial.println("Initializing Magnetometer...");
    SPI.begin(SCK_PIN, MISO_PIN, MOSI_PIN, CS_PIN);

    pinMode(DRDY_PIN, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(DRDY_PIN), drdyISR, FALLING);
    
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

    Serial.println("timestamp_us,x,y,z,status");
    serialCLI.printHelp();
    
#if LED_PIN >= 0
    pinMode(LED_PIN, OUTPUT);
#endif
}

void loop() {
    // Non-blocking LED blink
#if LED_PIN >= 0
    static unsigned long lastBlinkTime = 0;
    static bool ledState = false;
    unsigned long currentMillis = millis();
    
    if (currentMillis - lastBlinkTime >= 1000) {
        lastBlinkTime = currentMillis;
        ledState = !ledState;
        digitalWrite(LED_PIN, ledState ? HIGH : LOW);
    }
#endif

    // CLI Parsing
    serialCLI.update();

    // WiFi UDP Command Listening
    if (wifiConnected) {
        int packetSize = udp.parsePacket();
        if (packetSize) {
            targetIP = udp.remoteIP(); // Auto-discover desktop app's IP address
            char buf[256];
            int len = udp.read(buf, 255);
            if (len > 0) {
                buf[len] = 0;
                String udpCmd = String(buf);
                udpCmd.trim();
                if (udpCmd.length() > 0) {
                    Serial.print("Executing UDP Command: ");
                    Serial.println(udpCmd);
                    serialCLI.handleCommand(udpCmd);
                }
            }
        }
    }

    // Data Streaming
    if (streaming && sensor->dataReady()) {
        int32_t x, y, z;
        uint32_t status = 0xC00000;
        sensor->readXYZ(x, y, z, status);
        
#if (SENSOR_TYPE == SENSOR_TYPE_FLC100)
        uint16_t decimationFactor = current_downsample;
        if (decimationFactor <= 1) {
            // Direct 1 kS/s streaming without averaging
            sendOutputSample(esp_timer_get_time(), x, y, z, status);
        } else {
            static int32_t sumX = 0, sumY = 0, sumZ = 0;
            static uint16_t sampleCounter = 0;

            sumX += x;
            sumY += y;
            sumZ += z;
            sampleCounter++;

            if (sampleCounter >= decimationFactor) {
                int32_t avgX = sumX / decimationFactor;
                int32_t avgY = sumY / decimationFactor;
                int32_t avgZ = sumZ / decimationFactor;

                sumX = 0;
                sumY = 0;
                sumZ = 0;
                sampleCounter = 0;

                sendOutputSample(esp_timer_get_time(), avgX, avgY, avgZ, status);
            }
        }
#else
        // For RM3100, stream raw values directly (hardware controls low rates: 9 Hz to 600 Hz)
        sendOutputSample(esp_timer_get_time(), x, y, z, status);
#endif
    }
}
