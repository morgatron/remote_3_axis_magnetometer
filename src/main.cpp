#include <Arduino.h>
#include <SPI.h>
#include <Preferences.h>
#include <esp_mac.h>
#include <esp_pm.h>
#include "board_config.h"

#include "RM3100.h"
#include "FLC100_ADS131.h"
#include "MockSensor.h"
#include "OLEDDisplay.h"

void configurePowerManagement() {
#if defined(ESP_PLATFORM)
    esp_pm_config_t pm_config;
    memset(&pm_config, 0, sizeof(pm_config));
    pm_config.max_freq_mhz = 160;
    pm_config.min_freq_mhz = 20;
    pm_config.light_sleep_enable = false; // APB clock kept ON to ensure zero DRDY interrupt latency & 0% sample loss

    esp_err_t err = esp_pm_configure(&pm_config);
    if (err == ESP_OK) {
        Serial.println(F("[POWER] DFS Enabled (20MHz-160MHz, APB ON, 0% sample loss)."));
    } else {
        Serial.printf("[POWER] Power management config info: 0x%X\n", err);
    }
#endif
}

RM3100 sensorRM3100(CS_PIN, DRDY_PIN);
FLC100_ADS131 sensorFLC100(CS_PIN, DRDY_PIN, RESET_PIN);
MockSensor sensorMock;

uint8_t sensorTypeConfig = 1; // 0 = RM3100, 1 = FLC100-ADS131E08, 2 = Mock Sensor
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

#include "BLEStream.h"

enum OutputMode { MODE_SERIAL = 0, MODE_WIFI = 1, MODE_BOTH = 2, MODE_BLE = 3 };
uint8_t outputMode = MODE_BOTH; // Default: MODE_BOTH (Serial & BLE Coded PHY telemetry active)
uint8_t batchSizeConfig = 10; // Default: 10 samples per Coded PHY burst (range 1-10)
String wifiSSID = "";
String wifiPass = "";
bool wifiConnected = false;
String deviceID = "";

void saveSettings() {
    prefs.begin("mcu_v0", false);
    prefs.putBool("streaming", streaming);
    prefs.putUChar("rate", current_rate);
    prefs.putUShort("downsample", current_downsample);
    prefs.putUChar("sensor_type", sensorTypeConfig);
    prefs.putUChar("mode", outputMode);
    prefs.putUChar("batch_size", batchSizeConfig);
    prefs.putString("ssid", wifiSSID);
    prefs.putString("pass", wifiPass);
    prefs.putString("target", targetIP.toString());
    prefs.putString("device_id", deviceID);
    prefs.end();
}

static char udpBatchBuf[4096];
static size_t udpBatchLen = 0;
static uint32_t lastUdpFlushMs = 0;

void flushUdpBatch() {
    if (udpBatchLen > 0 && wifiConnected) {
        udp.beginPacket(targetIP, targetPort);
        udp.write((const uint8_t*)udpBatchBuf, udpBatchLen);
        udp.endPacket();
        udpBatchLen = 0;
        lastUdpFlushMs = millis();
    }
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
    prefs.begin("mcu_v0", true);
    streaming = prefs.getBool("streaming", true);
    current_rate = prefs.getUChar("rate", DEFAULT_RATE);
    current_downsample = prefs.getUShort("downsample", 1);
    sensorTypeConfig = prefs.getUChar("sensor_type", 1);
    outputMode = prefs.getUChar("mode", MODE_BOTH);
    batchSizeConfig = prefs.getUChar("batch_size", 10);
    if (batchSizeConfig < 1 || batchSizeConfig > 10) batchSizeConfig = 10;
    wifiSSID = prefs.getString("ssid", "");
    wifiPass = prefs.getString("pass", "");
    String tIP = prefs.getString("target", "255.255.255.255");
    targetIP.fromString(tIP);
    deviceID = prefs.getString("device_id", "");
    prefs.end();

    if (deviceID.length() == 0) {
        uint8_t mac[6];
        esp_read_mac(mac, ESP_MAC_WIFI_STA);
        char macBuf[32];
        snprintf(macBuf, sizeof(macBuf), "NODE_%02X%02X%02X", mac[3], mac[4], mac[5]);
        deviceID = String(macBuf);
    }

    if (sensorTypeConfig == 0) {
        sensor = &sensorRM3100;
    } else if (sensorTypeConfig == 1) {
        sensor = &sensorFLC100;
    } else if (sensorTypeConfig == 2) {
        sensor = &sensorMock;
    }

    if (outputMode == MODE_BLE || outputMode == MODE_BOTH) {
        bleStream.begin(deviceID);
        delay(150); // Allow NimBLE BTDM controller memory allocation to complete cleanly before Wi-Fi init
    }

    if (outputMode == MODE_WIFI || outputMode == MODE_BOTH) {
        if (wifiSSID.length() > 0) {
            connectWiFi();
        }
    } else {
        WiFi.mode(WIFI_OFF);
    }
}

volatile uint32_t sampleCounter = 0;
float lastBmag_nT = 0.0f;

void sendOutputSample(const String &deviceID, uint64_t ts, float x, float y, float z, uint32_t status, const char *line, size_t len);

void sendOutputSample(uint64_t ts, float x, float y, float z, uint32_t status = 0xC00000) {
    sampleCounter++;
    lastBmag_nT = sqrtf(x * x + y * y + z * z);
    char line[160];
    int len = snprintf(line, sizeof(line), "%s,%llu,%.2f,%.2f,%.2f,%06X\n", deviceID.c_str(), (unsigned long long)ts, x, y, z, (unsigned int)(status & 0xFFFFFF));
    sendOutputSample(deviceID, ts, x, y, z, status, line, (size_t)len);
}


#define DISCONNECT_BUFFER_SIZE 600 // 600 samples = 10 minutes of disconnect storage at 1 Hz

struct BufferedSample {
    uint32_t ts_ms;
    int32_t  x_nT;
    int32_t  y_nT;
    int32_t  z_nT;
};

static BufferedSample ringBuffer[DISCONNECT_BUFFER_SIZE];
static uint16_t ringHead = 0;        // Next write position
static uint16_t ringTail = 0;        // Oldest un-ACKed sample position
static uint16_t unackedCount = 0;    // Number of un-ACKed samples in buffer
static uint16_t lastSentCount = 0;   // Number of samples in current in-flight batch
static uint32_t lastBurstTxMs = 0;
static bool isTxActive = false;
static uint32_t txStartMs = 0;
static uint8_t txRetryCount = 0;

void checkBleAckTask() {
    if (isTxActive) {
        if (bleStream.isBatchAcked()) {
            bleStream.stopAdvertising();
            isTxActive = false;
            txRetryCount = 0;
            if (lastSentCount > 0) {
                ringTail = (ringTail + lastSentCount) % DISCONNECT_BUFFER_SIZE;
                if (unackedCount >= lastSentCount) {
                    unackedCount -= lastSentCount;
                } else {
                    unackedCount = 0;
                }
                lastSentCount = 0;
            }
        } else if (millis() - txStartMs >= 1500) {
            // Burst timeout (receiver offline or missed hardware ACK)
            bleStream.stopAdvertising();
            isTxActive = false;
            txRetryCount++;

            // If burst failed to receive ACK after 2 retries, advance ringTail to prevent indefinite queue stalls
            if (txRetryCount >= 2) {
                if (lastSentCount > 0) {
                    ringTail = (ringTail + lastSentCount) % DISCONNECT_BUFFER_SIZE;
                    if (unackedCount >= lastSentCount) {
                        unackedCount -= lastSentCount;
                    } else {
                        unackedCount = 0;
                    }
                    lastSentCount = 0;
                }
                txRetryCount = 0;
            }
        }
    }
}

void processBleTelemetry(const String &deviceID, uint64_t ts, float x, float y, float z, uint32_t status, const char *line) {
    // 1. Insert new 1 Hz sample into 10-minute circular buffer
    ringBuffer[ringHead].ts_ms = (uint32_t)(ts / 1000ULL);
    ringBuffer[ringHead].x_nT = (int32_t)x;
    ringBuffer[ringHead].y_nT = (int32_t)y;
    ringBuffer[ringHead].z_nT = (int32_t)z;
    ringHead = (ringHead + 1) % DISCONNECT_BUFFER_SIZE;

    if (unackedCount < DISCONNECT_BUFFER_SIZE) {
        unackedCount++;
    } else {
        // Buffer full (>10 mins offline): advance tail to drop oldest sample
        ringTail = (ringTail + 1) % DISCONNECT_BUFFER_SIZE;
    }

    // 2. Check ACK/timeout status of in-flight burst
    checkBleAckTask();

    // 3. Transmit batch ONLY when batchSizeConfig new samples buffered (e.g. 10s for BATCH 10) OR catch-up flushing offline backlog
    if (!isTxActive) {
        uint8_t targetBatchSize = (batchSizeConfig >= 1 && batchSizeConfig <= 10) ? batchSizeConfig : 10;
        uint32_t minBurstIntervalMs = (uint32_t)targetBatchSize * 1000UL;

        if (unackedCount >= targetBatchSize || (unackedCount > 0 && millis() - lastBurstTxMs >= minBurstIntervalMs)) {
            SensorBatchPacket batch;
            memset(&batch, 0, sizeof(batch));
            strncpy(batch.device_id, deviceID.c_str(), sizeof(batch.device_id) - 1);
            batch.start_ts_ms = ringBuffer[ringTail].ts_ms;
            batch.sample_interval_ms = 1000;
            batch.status = (uint16_t)(status & 0xFFFF);
            batch.vbat_mv = 3300;

            uint8_t countToSend = (unackedCount >= targetBatchSize) ? targetBatchSize : (uint8_t)unackedCount;
            batch.sample_count = countToSend;

            for (uint8_t i = 0; i < countToSend; i++) {
                uint16_t idx = (ringTail + i) % DISCONNECT_BUFFER_SIZE;
                batch.samples[i].x_nT = ringBuffer[idx].x_nT;
                batch.samples[i].y_nT = ringBuffer[idx].y_nT;
                batch.samples[i].z_nT = ringBuffer[idx].z_nT;
            }

            bleStream.clearBatchAck();
            Serial.print("DEBUG: notifyBatchBinary: "); Serial.println(ringTail);
            bleStream.notifyBatchBinary(batch);
            lastSentCount = countToSend;
            lastBurstTxMs = millis();
            txStartMs = millis();
            isTxActive = true;
        }
    }
}

void sendOutputSample(const String &deviceID, uint64_t ts, float x, float y, float z, uint32_t status, const char *line, size_t len) {
    // Non-blocking Serial output (prevents USB CDC buffer stalls when host monitor is not attached)
    if (outputMode == MODE_SERIAL || outputMode == MODE_BOTH) {
        if (Serial.availableForWrite() >= len) {
            Serial.print(line);
        }
    }

    if ((outputMode == MODE_WIFI || outputMode == MODE_BOTH) && wifiConnected) {
        if (udpBatchLen + len >= sizeof(udpBatchBuf) - 1) {
            flushUdpBatch();
        }
        memcpy(udpBatchBuf + udpBatchLen, line, len);
        udpBatchLen += len;

        // Flush UDP batch every 1000 ms (1 Hz packet rate)
        if (millis() - lastUdpFlushMs >= 1000) {
            flushUdpBatch();
        }
    }

    if (outputMode == MODE_BLE || outputMode == MODE_BOTH) {
        processBleTelemetry(deviceID, ts, x, y, z, status, line);
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
        if (sensorTypeConfig == 2) {
            vTaskDelay(pdMS_TO_TICKS(13)); // ~75 Hz tick for MockSensor
            if (streaming && sensor != NULL) {
                sensor->readAndPushSample();
            }
        } else {
            if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(13))) { // ~75 Hz fallback tick
                if (streaming && sensor != NULL) {
                    sensor->readAndPushSample();
                }
            }
        }
    }
}

void IRAM_ATTR drdyISR() {
    uint64_t now = esp_timer_get_time();
    if (lastDrdyTimeUs > 0) {
        uint32_t dt = (uint32_t)(now - lastDrdyTimeUs);
        lastDrdyIntervalUs = dt;
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

void recoverSensor() {
    Serial.println("\r\n[WATCHDOG STALL DETECTED] No DRDY interrupt for >500ms! Recovering sensor...");
    detachInterrupt(digitalPinToInterrupt(DRDY_PIN));

    pinMode(CS_PIN, OUTPUT);
    digitalWrite(CS_PIN, HIGH);
    delay(10);

    bool ok = sensor->begin();
    if (!ok) {
        Serial.println("[WATCHDOG RECOVERY] Re-initializing SPI bus...");
        SPI.end();
        delay(10);
        SPI.begin(SCK_PIN, MISO_PIN, MOSI_PIN, CS_PIN);
        sensor->begin();
    }

    sensor->setContinuousMode(true, current_rate);
    sensor->readAndPushSample(); // Clear pending DRDY latch on ASIC

    lastDrdyTimeUs = esp_timer_get_time();
    attachInterrupt(digitalPinToInterrupt(DRDY_PIN), drdyISR, sensorTypeConfig == 0 ? RISING : FALLING);
    Serial.println("[WATCHDOG RECOVERY] Sensor streaming recovered successfully.\r\n");
}


void setup() {
    initBoardPower();

#if defined(HELTEC_V4) || defined(ARDUINO_heltec_wifi_lora_32_V3)
    oledDisplay.begin(17, 18, 21, 36);
    oledDisplay.updateSensorScreen("BOOTING...", "INITIALIZING", 0.0f, 0, 3.70f, "INIT");
#endif

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

    if (sensorTypeConfig == 2) {
        sensor = &sensorMock;
        sensor->begin();
        Serial.println("[MOCK MODE] Explicit Synthetic Sensor Mode Active (Range Testing)");
        Serial.println("Status Header: 0x80MOCK");
    } else {
        Serial.println("Initializing SPI Bus...");
        SPI.begin(SCK_PIN, MISO_PIN, MOSI_PIN, CS_PIN);
        pinMode(DRDY_PIN, INPUT_PULLUP);

        Serial.println("Auto-probing physical sensor hardware...");
        bool found = false;

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
            Serial.print("[SENSOR ERROR] Physical sensor hardware probe failed! Check SPI wiring.\r\n");
            Serial.print("[HINT] Send CLI command 'SENSOR MOCK' to enable synthetic range testing.\r\n");
            serialCLI.update();
            if (sensor->begin()) {
                found = true;
            }
        }

        // Attach DRDY interrupt with appropriate edge polarity: RM3100 = RISING (Active HIGH), FLC100 = FALLING (Active LOW)
        attachInterrupt(digitalPinToInterrupt(DRDY_PIN), drdyISR, sensorTypeConfig == 0 ? RISING : FALLING);
    }

    Serial.print("Active Sensor: ");
    Serial.println(sensor->getSensorName());
    Serial.println(sensor->getStatusString());

    // Sensor specific setup
    if (sensorTypeConfig == 0) {
        static_cast<RM3100*>(sensor)->setCycleCount(200, 200, 200);
    } else if (sensorTypeConfig == 1) {
        // Set calibration: VREF = 2.4V (standard for 3.3V systems), Sensitivity = 20.0 uV/nT, Gain = 1
        static_cast<FLC100_ADS131*>(sensor)->setCalibration(2.4f, 20.0f, 1);
    }

    // Resume continuous mode with saved rate
    sensor->setContinuousMode(true, current_rate);

    // Create high-priority task for immediate ISR-notified ADC sampling (Pinned to Core 1 to isolate from WiFi on Core 0)
#if CONFIG_FREERTOS_UNICORE
    xTaskCreatePinnedToCore(adcSamplingTask, "ADC_Task", 4096, NULL, configMAX_PRIORITIES - 1, &adcTaskHandle, 0);
#else
    xTaskCreatePinnedToCore(adcSamplingTask, "ADC_Task", 4096, NULL, configMAX_PRIORITIES - 1, &adcTaskHandle, 1);
#endif

    Serial.print("Device ID: ");
    Serial.println(deviceID);
    Serial.println("device_id,timestamp_us,x,y,z,status");
    serialCLI.printHelp();

#if LED_PIN >= 0
    pinMode(LED_PIN, OUTPUT);
#endif

    if (outputMode == MODE_BLE || outputMode == MODE_BOTH) {
        bleStream.begin(deviceID);
    }

    // Enable Automatic Tickless Light Sleep and Dynamic Frequency Scaling
    configurePowerManagement();
}

void loop() {
    checkBleAckTask();

#if defined(HELTEC_V4) || defined(ARDUINO_heltec_wifi_lora_32_V3)
    static uint32_t lastOledUpdateMs = 0;
    if (millis() - lastOledUpdateMs >= 500) {
        lastOledUpdateMs = millis();
        const char* modeNames[] = {"SERIAL", "WIFI", "BOTH", "BLE"};
        oledDisplay.updateSensorScreen(
            deviceID.c_str(),
            sensor ? sensor->getSensorName().c_str() : "NONE",
            lastBmag_nT,
            sampleCounter,
            3.70f,
            modeNames[outputMode % 4]
        );
    }
#endif

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

    // Hardware Watchdog: Detect DRDY interrupt stall due to physical bumps or power glitches
    static uint32_t lastWdCheckMs = 0;
    uint32_t nowWdMs = millis();
    if (streaming && sensor != NULL && sensorTypeConfig != 2 && (nowWdMs - lastWdCheckMs >= 500)) {
        lastWdCheckMs = nowWdMs;
        uint64_t nowUs = esp_timer_get_time();
        if (lastDrdyTimeUs > 0 && (nowUs - lastDrdyTimeUs > 500000ULL)) {
            recoverSensor();
        }
    }

    // WiFi UDP Command Listening & Low-Latency Batch Flushing
    if (wifiConnected) {
        if (udpBatchLen > 0 && (millis() - lastUdpFlushMs >= 50)) {
            flushUdpBatch();
        }

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

            uint16_t decimationFactor = current_downsample;
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
                    float avgX = sumX / (float)decimationFactor;
                    float avgY = sumY / (float)decimationFactor;
                    float avgZ = sumZ / (float)decimationFactor;

                    sumX = 0;
                    sumY = 0;
                    sumZ = 0;
                    decimationCounter = 0;

                    sendOutputSample(ts, avgX, avgY, avgZ, status);
                }
            }
        }
    }
}
