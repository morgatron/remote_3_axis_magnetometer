#include <Arduino.h>
#include <SPI.h>
#include <Preferences.h>
#include <esp_mac.h>
#include <esp_pm.h>
#include "board_config.h"
#include "TelemetryRingBuffer.h"

#include "OLEDDisplay.h"
#include "SensorConfig.h"
#include "SensorManager.h"

float g_cachedBatteryVoltage = 0.0f;

void configurePowerManagement();

SensorManager sensorManager(g_sensorConfig);
Magnetometer*& sensor = sensorManager.getSensorRef();

#include "CLI.h"
#include <WiFi.h>
#include <WiFiUdp.h>

// Centralized SensorConfig references for main.cpp
bool &streaming = g_sensorConfig.streaming;
uint8_t &current_rate = g_sensorConfig.rate;
uint16_t &current_downsample = g_sensorConfig.downsample;
uint8_t &sensorTypeConfig = g_sensorConfig.sensorType;
uint8_t &outputMode = g_sensorConfig.outputMode;
uint8_t &batchSizeConfig = g_sensorConfig.batchSize;
uint16_t &cycleCountConfig = g_sensorConfig.cycleCount;
String &wifiSSID = g_sensorConfig.wifiSSID;
String &wifiPass = g_sensorConfig.wifiPass;
IPAddress &targetIP = g_sensorConfig.targetIP;
uint16_t &targetPort = g_sensorConfig.targetPort;
uint16_t &udpListenPort = g_sensorConfig.udpListenPort;
String &deviceID = g_sensorConfig.deviceID;

bool wifiConnected = false;

#include "TelemetryEgress.h"

// WiFi & UDP Globals
WiFiUDP udp;
TelemetryEgress telemetry(g_sensorConfig, sensor, udp, wifiConnected);

#include "BLEStream.h"
#if defined(BOARD_HAS_LORA)
#include "LoRaStream.h"
#endif

#if defined(ESP_PLATFORM)
portMUX_TYPE g_spiFrequencyMux = portMUX_INITIALIZER_UNLOCKED;

void safeSetCpuFrequencyMhz(uint32_t freq) {
    if (getCpuFrequencyMhz() == freq) return;
    portENTER_CRITICAL(&g_spiFrequencyMux);
    setCpuFrequencyMhz(freq);
    portEXIT_CRITICAL(&g_spiFrequencyMux);
}
#else
void safeSetCpuFrequencyMhz(uint32_t freq) {
    setCpuFrequencyMhz(freq);
}
#endif

void configurePowerManagement() {
#if defined(ESP_PLATFORM)
#if defined(CONFIG_IDF_TARGET_ESP32C6) || defined(ARDUINO_ARCH_ESP32C6)
    // ESP32-C6 Bluetooth baseband & hardware encryption controller require active PLL (minimum 80 MHz)
    safeSetCpuFrequencyMhz(80);
    Serial.printf("[POWER] ESP32-C6 Low-Power Mode: CPU @ %d MHz (PLL active, Modem OFF between bursts).\r\n", getCpuFrequencyMhz());
#else
    if (outputMode == MODE_BLE) {
        safeSetCpuFrequencyMhz(40);
        Serial.printf("[POWER] Low-Power BLE Mode: CPU @ %d MHz (Modem OFF between bursts).\r\n", getCpuFrequencyMhz());
    } else {
        safeSetCpuFrequencyMhz(80);
        Serial.printf("[POWER] CPU Frequency set to %d MHz (80MHz APB retained, zero SPI latency).\r\n", getCpuFrequencyMhz());
    }
#endif
#endif
}

void saveSettings() {
    g_sensorConfig.save();
}

void flushUdpBatch() {
    telemetry.flushUdp();
}

void connectWiFi() {
    if (wifiSSID.length() > 0) {
        Serial.println("\r\n==========================================");
        Serial.print("[WIFI DEBUG] Initiating connection to SSID: '");
        Serial.print(wifiSSID);
        Serial.println("'...");

        WiFi.disconnect(true);
        delay(100);
        WiFi.mode(WIFI_STA);
        WiFi.setTxPower(WIFI_POWER_15dBm); // Cap TX power to 15dBm to prevent 3.3V power supply dips
        WiFi.begin(wifiSSID.c_str(), wifiPass.c_str());

        unsigned long start = millis();
        while (WiFi.status() != WL_CONNECTED && (millis() - start < 12000)) {
            delay(500);
            wl_status_t status = WiFi.status();
            Serial.print("[WIFI DEBUG] Connecting... State=");
            Serial.print((int)status);
            if (status == WL_NO_SSID_AVAIL) Serial.println(" (SSID Not Found in 2.4GHz scan)");
            else if (status == WL_CONNECT_FAILED) Serial.println(" (Connection Failed - Check Password)");
            else if (status == WL_DISCONNECTED) Serial.println(" (Associating...)");
            else Serial.println();
        }

        if (WiFi.status() == WL_CONNECTED) {
            wifiConnected = true;
            Serial.println("[WIFI SUCCESS] Connected to network!");
            Serial.print("  Local IP:    "); Serial.println(WiFi.localIP());
            Serial.print("  Subnet Mask: "); Serial.println(WiFi.subnetMask());
            Serial.print("  Gateway IP:  "); Serial.println(WiFi.gatewayIP());
            Serial.print("  RSSI Signal: "); Serial.print(WiFi.RSSI()); Serial.println(" dBm");
            Serial.print("  Target IP:   "); Serial.println(targetIP);
            udp.begin(udpListenPort);
            Serial.print("  UDP Active on Port "); Serial.println(udpListenPort);
            Serial.println("==========================================\r\n");
        } else {
            wifiConnected = false;
            Serial.println("[WIFI ERROR] Connection timed out after 12 seconds!");
            Serial.println("  Check SSID spelling, WPA2 password, or 2.4GHz band availability.");
            Serial.println("==========================================\r\n");
        }
    }
}

void loadSettings() {
    g_sensorConfig.load();

    if (outputMode == MODE_BLE || outputMode == MODE_BOTH) {
        bleStream.begin(deviceID);
        delay(150); // Allow NimBLE BTDM controller memory allocation to complete cleanly before Wi-Fi init
        if (outputMode == MODE_BLE) {
            bleStream.powerDownModem();
        }
    } else {
        btStop();
    }

    if (outputMode == MODE_WIFI || outputMode == MODE_BOTH) {
        if (wifiSSID.length() > 0) {
            connectWiFi();
        }
    } else {
        WiFi.mode(WIFI_OFF);
        wifiConnected = false;
    }
}

inline void sendOutputSample(uint64_t ts, float x, float y, float z, uint32_t status = 0xC00000) {
    telemetry.sendSample(ts, x, y, z, status);
}

CLI serialCLI(sensorManager, g_sensorConfig);


uint32_t lastOledActivityMs = 0;
bool oledScreenActive = true;

void setup() {
    initBoardPower();
    sampleBatteryVoltage(); // Measure initial voltage once at boot to prime cache

#if defined(HELTEC_V4) || defined(ARDUINO_heltec_wifi_lora_32_V3)
    pinMode(0, INPUT_PULLUP); // PRG / USER button on Heltec V4 for OLED wake
    oledDisplay.begin(17, 18, 21, 36);
    oledDisplay.updateSensorScreen("BOOTING...", "INITIALIZING", 0.0f, 0, getBatteryVoltage(), "INIT");
    lastOledActivityMs = millis();
#endif

#if (defined(ARDUINO_USB_CDC_ON_BOOT) && ARDUINO_USB_CDC_ON_BOOT) || defined(CONFIG_IDF_TARGET_ESP32C3) || defined(ARDUINO_ARCH_ESP32C3) || defined(CONFIG_IDF_TARGET_ESP32C6) || defined(ARDUINO_ARCH_ESP32C6)
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

    sensorManager.begin();

    Serial.print("Device ID: ");
    Serial.println(deviceID);
    Serial.println("device_id,timestamp_us,x,y,z,status");
    serialCLI.printHelp();

#if LED_PIN >= 0
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LED_OFF);
#endif

    telemetry.begin();

    // Enable Automatic Tickless Light Sleep and Dynamic Frequency Scaling
    configurePowerManagement();
}

void loop() {
    telemetry.update();

#if defined(HELTEC_V4) || defined(ARDUINO_heltec_wifi_lora_32_V3)
    // Check USER/PRG button press (GPIO 0) to wake OLED screen
    static bool lastBtnState = HIGH;
    bool btnState = digitalRead(0);
    if (btnState == LOW && lastBtnState == HIGH) {
        lastOledActivityMs = millis();
        if (!oledScreenActive) {
            oledDisplay.displayOn();
            oledScreenActive = true;
        }
    }
    lastBtnState = btnState;

    // Auto-sleep OLED display after 10 seconds of inactivity (~20 mA power saving)
    if (millis() - lastOledActivityMs > 10000) {
        if (oledScreenActive) {
            oledDisplay.displayOff();
            oledScreenActive = false;
        }
    } else {
        if (!oledScreenActive) {
            oledDisplay.displayOn();
            oledScreenActive = true;
        }

        static uint32_t lastOledUpdateMs = 0;
        if (millis() - lastOledUpdateMs >= 500) {
            lastOledUpdateMs = millis();
            const char* modeNames[] = {"SERIAL", "WIFI", "BOTH", "BLE", "LORA"};
            oledDisplay.updateSensorScreen(
                deviceID.c_str(),
                sensor ? sensor->getSensorName().c_str() : "NONE",
                telemetry.getLastBmag(),
                telemetry.getSampleCount(),
                getBatteryVoltage(), // 0ms instantaneous read from cache
                modeNames[outputMode % 5]
            );
        }
    }
#endif

    // Fallback periodic refresh when unbatched streaming (batch_size == 1) or serial mode
    static uint32_t lastUnbatchedVbatMs = 0;
    if ((batchSizeConfig == 1 || outputMode == MODE_SERIAL) && (millis() - lastUnbatchedVbatMs >= 10000)) {
        lastUnbatchedVbatMs = millis();
        sampleBatteryVoltage();
    }

    // Low-power status LED pulse (crisp 20 ms flash every 10s instead of 50% continuous burn)
#if LED_PIN >= 0
    static unsigned long lastPulseStartMs = 0;
    static bool ledActive = false;
    unsigned long currentMillis = millis();

    if (ledActive && (currentMillis - lastPulseStartMs >= 20)) {
        digitalWrite(LED_PIN, LED_OFF);
        ledActive = false;
    } else if (!ledActive && (currentMillis - lastPulseStartMs >= 10000)) {
        lastPulseStartMs = currentMillis;
        digitalWrite(LED_PIN, LED_ON);
        ledActive = true;
    }
#endif

    // CLI Parsing
    serialCLI.update();

    // Hardware Watchdog: Detect DRDY interrupt stall due to physical bumps or power glitches
    sensorManager.checkWatchdog();

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

    // Data Streaming (Unified ring-buffer driven for all sensors)
    if (streaming && sensor != NULL) {
        float scaleFactor = sensor->getScaleFactor();
        ADCSample sample;
        while (sensor->popSample(sample)) {
            float x = (float)sample.x * scaleFactor;
            float y = (float)sample.y * scaleFactor;
            float z = (float)sample.z * scaleFactor;
            uint32_t status = sample.status;
            uint64_t ts = sample.ts;

            uint16_t decimationFactor = (sensorTypeConfig == 2) ? 1 : current_downsample;
            if (decimationFactor <= 1) {
                // Direct streaming without averaging
                sendOutputSample(ts, x, y, z, status);
            } else {
                static float sumX = 0, sumY = 0, sumZ = 0;
                static uint16_t decimationCounter = 0;

                sumX += x;
                sumY += y;
                sumZ += z;
                decimationCounter++;

                if (decimationCounter >= decimationFactor) {
                    float avgX = sumX / (float)decimationCounter;
                    float avgY = sumY / (float)decimationCounter;
                    float avgZ = sumZ / (float)decimationCounter;

                    sumX = 0;
                    sumY = 0;
                    sumZ = 0;
                    decimationCounter = 0;

                    sendOutputSample(ts, avgX, avgY, avgZ, status);
                }
            }
        }
    }

    // Yield to FreeRTOS IDLE task to enable automatic dynamic CPU clock gating
    vTaskDelay(pdMS_TO_TICKS(1));
}
