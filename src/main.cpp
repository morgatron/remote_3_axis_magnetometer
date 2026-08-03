#include <Arduino.h>
#include <SPI.h>
#include <Preferences.h>
#if defined(CONFIG_IDF_TARGET_ESP32C3) || defined(ARDUINO_ARCH_ESP32C3)
// Custom ESP32-C3 PCB Pinout
#define SCK_PIN      6
#define MOSI_PIN     7
#define MISO_PIN     2
#define DRDY_PIN     3
#define CS_PIN       10
#define LED_PIN      -1
#else
// Classic ESP32 Dev Module Pinout
#define SCK_PIN      18
#define MISO_PIN     19
#define MOSI_PIN     23
#define CS_PIN       5
#define DRDY_PIN     4
#define LED_PIN      2
#endif

#define RESET_PIN   -1

#include "RM3100.h"
#include "FLC100_ADS131.h"

RM3100 sensorRM3100(CS_PIN, DRDY_PIN);
FLC100_ADS131 sensorFLC100(CS_PIN, DRDY_PIN, RESET_PIN);

uint8_t sensorTypeConfig = 1; // 0 = RM3100, 1 = FLC100-ADS131E08
const uint8_t DEFAULT_RATE = 0x95;

#include "CLI.h"
#include <WiFi.h>
#include <WiFiUdp.h>

Preferences prefs;
Magnetometer* sensor = &sensorFLC100;

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
    prefs.putUChar("sensor_type", sensorTypeConfig);
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
    sensorTypeConfig = prefs.getUChar("sensor_type", 1);
    outputMode = prefs.getUChar("mode", MODE_SERIAL);
    wifiSSID = prefs.getString("ssid", "");
    wifiPass = prefs.getString("pass", "");
    String tIP = prefs.getString("target", "255.255.255.255");
    targetIP.fromString(tIP);
    prefs.end();

    if (sensorTypeConfig == 0) {
        sensor = &sensorRM3100;
    } else {
        sensor = &sensorFLC100;
    }

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

volatile bool drdyInterruptFlag = false;
volatile uint64_t lastDrdyTimeUs = 0;
volatile uint32_t lastDrdyIntervalUs = 0;
volatile uint32_t drdyAnomalyCount = 0;

TaskHandle_t adcTaskHandle = NULL;

void adcSamplingTask(void *pvParameters) {
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        if (streaming && sensorTypeConfig == 1) {
            sensorFLC100.readAndPushSample();
        }
    }
}

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

    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    if (adcTaskHandle != NULL) {
        vTaskNotifyGiveFromISR(adcTaskHandle, &xHigherPriorityTaskWoken);
        if (xHigherPriorityTaskWoken) {
            portYIELD_FROM_ISR();
        }
    }
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
    Serial.println(" FIRMWARE: Remote 3-Axis Magnetometer Acquisition System");
    Serial.println("==================================================");

    // Wait for internal oscillator and power-on-reset stabilization
    delay(250);
    
    loadSettings();

    Serial.println("Initializing SPI Bus...");
    SPI.begin(SCK_PIN, MISO_PIN, MOSI_PIN, CS_PIN);
    pinMode(DRDY_PIN, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(DRDY_PIN), drdyISR, FALLING);

    Serial.println("Auto-probing sensor hardware...");
    bool found = false;
    
    // Probe RM3100 first (RM3100 REVID register 0x36 MUST equal 0x22)
    if (sensorRM3100.begin()) {
        sensor = &sensorRM3100;
        sensorTypeConfig = 0;
        found = true;
        Serial.println("Auto-detected sensor: RM3100");
    } else if (sensorFLC100.begin()) {
        sensor = &sensorFLC100;
        sensorTypeConfig = 1;
        found = true;
        Serial.println("Auto-detected sensor: FLC100-ADS131E08");
    }

    if (found) {
        saveSettings();
    }

    while (!found) {
        delay(1000);
        Serial.print("Failed to find sensor! Retrying... (Send 'SENSOR RM3100' or 'SENSOR FLC100')\r\n");
        serialCLI.update();
        if (sensor->begin()) {
            found = true;
        }
    }
    

    Serial.print("Sensor Found: ");
    Serial.println(sensor->getSensorName());
    Serial.println(sensor->getStatusString());

    // Sensor specific setup
    if (sensorTypeConfig == 0) {
        static_cast<RM3100*>(sensor)->setCycleCount(200, 200, 200);
    } else {
        // Set calibration: VREF = 2.4V (standard for 3.3V systems), Sensitivity = 20.0 uV/nT, Gain = 1
        static_cast<FLC100_ADS131*>(sensor)->setCalibration(2.4f, 20.0f, 1);
    }
    
    // Resume continuous mode with saved rate
    sensor->setContinuousMode(true, current_rate);

    // Create high-priority task for immediate ISR-notified ADC sampling
    xTaskCreatePinnedToCore(adcSamplingTask, "ADC_Task", 4096, NULL, configMAX_PRIORITIES - 1, &adcTaskHandle, 0);

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
    if (streaming) {
        if (sensorTypeConfig == 1) { // FLC100-ADS131E08 (Ring buffer driven)
            ADCSample sample;
            while (sensorFLC100.popSample(sample)) {
                int32_t x = sample.x;
                int32_t y = sample.y;
                int32_t z = sample.z;
                uint32_t status = sample.status;
                uint64_t ts = sample.ts;

                uint16_t decimationFactor = current_downsample;
                if (decimationFactor <= 1) {
                    // Direct 1 kS/s streaming without averaging
                    sendOutputSample(ts, x, y, z, status);
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

                        sendOutputSample(ts, avgX, avgY, avgZ, status);
                    }
                }
            }
        } else if (sensor->dataReady()) {
            // For RM3100, stream raw values directly
            int32_t x, y, z;
            uint32_t status = 0xC00000;
            sensor->readXYZ(x, y, z, status);
            sendOutputSample(esp_timer_get_time(), x, y, z, status);
        }
    }
}
