#include <Arduino.h>
#include <SPI.h>
#include <esp_mac.h>
#include "board_config.h"
#include "TelemetryRingBuffer.h"

#include "OLEDDisplay.h"
#include "SensorConfig.h"
#include "SensorManager.h"
#include "TelemetryEgress.h"
#include "CLI.h"
#include <WiFiUdp.h>

float g_cachedBatteryVoltage = 0.0f;

SensorManager sensorManager(g_sensorConfig);
Magnetometer*& sensor = sensorManager.getSensorRef();
TelemetryEgress telemetry(g_sensorConfig, sensor);

uint32_t lastOledActivityMs = 0;
bool oledScreenActive = true;

void onCliActivity() {
#if defined(HELTEC_V4) || defined(ARDUINO_heltec_wifi_lora_32_V3)
    lastOledActivityMs = millis();
#endif
}

CLI serialCLI(sensorManager, telemetry, g_sensorConfig, nullptr, onCliActivity);

void setup() {
    initBoardPower();
    sampleBatteryVoltage(); // Measure initial voltage once at boot to prime cache

#if defined(HELTEC_V4) || defined(ARDUINO_heltec_wifi_lora_32_V3)
    pinMode(0, INPUT_PULLUP); // PRG / USER button on Heltec V4 for OLED wake
    oledDisplay.begin(17, 18, 21, 36);
    oledDisplay.updateSensorScreen("BOOTING...", "INITIALIZING", 0.0f, 0, getBatteryVoltage(), "INIT");
    lastOledActivityMs = millis();
#endif

    Serial.begin(921600);
#if defined(ARDUINO_USB_CDC_ON_BOOT) && ARDUINO_USB_CDC_ON_BOOT
    Serial.setTxTimeoutMs(0);
    // Timeout waiting for Serial so board boots even without open Serial Monitor
    unsigned long startWait = millis();
    while (!Serial && (millis() - startWait < 500)) delay(10);
#endif

    Serial.println("\r\n==================================================");
    Serial.println(" FIRMWARE: Remote 3-Axis Magnetometer Acquisition System");
    Serial.println("==================================================");

    // Wait for internal oscillator, external boost converter, and ADC analog rail stabilization
    delay(1000);

    g_sensorConfig.load();
    sensorManager.begin();

    Serial.print("Device ID: ");
    Serial.println(g_sensorConfig.deviceID);
    Serial.println("device_id,timestamp_us,x,y,z,status");
    serialCLI.printHelp();

#if LED_PIN >= 0
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LED_OFF);
#endif

    telemetry.begin();
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
                g_sensorConfig.deviceID.c_str(),
                sensor ? sensor->getSensorName().c_str() : "NONE",
                telemetry.getLastBmag(),
                telemetry.getSampleCount(),
                getBatteryVoltage(), // 0ms instantaneous read from cache
                modeNames[g_sensorConfig.outputMode % 5]
            );
        }
    }
#endif

    // Fallback periodic refresh when unbatched streaming (batch_size == 1) or serial mode
    static uint32_t lastUnbatchedVbatMs = 0;
    if ((g_sensorConfig.batchSize == 1 || g_sensorConfig.outputMode == MODE_SERIAL) && (millis() - lastUnbatchedVbatMs >= 10000)) {
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
    if (telemetry.isWifiConnected()) {
        WiFiUDP& udp = telemetry.getUdp();
        int packetSize = udp.parsePacket();
        if (packetSize) {
            g_sensorConfig.targetIP = udp.remoteIP(); // Auto-discover desktop app's IP address
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

    // Data Streaming (Unified ring-buffer driven with downsampling averaging)
    sensorManager.processSamples(telemetry);

    // Yield to FreeRTOS IDLE task to enable automatic dynamic CPU clock gating
    vTaskDelay(pdMS_TO_TICKS(1));
}
