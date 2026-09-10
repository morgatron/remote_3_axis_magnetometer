#include <Arduino.h>
#include <SPI.h>
#include <Preferences.h>
#include <esp_mac.h>
#include <esp_pm.h>
#include "board_config.h"
#include "TelemetryRingBuffer.h"

#include "RM3100.h"
#include "FLC100_ADS131.h"
#include "MockSensor.h"
#include "OLEDDisplay.h"

float g_cachedBatteryVoltage = 0.0f;

void configurePowerManagement();

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
#if defined(BOARD_HAS_LORA)
#include "LoRaStream.h"
#endif

enum OutputMode { MODE_SERIAL = 0, MODE_WIFI = 1, MODE_BOTH = 2, MODE_BLE = 3, MODE_LORA = 4 };
uint8_t outputMode = MODE_BOTH; // Default: MODE_BOTH (Serial & BLE Coded PHY telemetry active)
uint8_t batchSizeConfig = 10; // Default: 10 samples per Coded PHY burst (range 1-10)
String wifiSSID = "";
String wifiPass = "";
bool wifiConnected = false;
String deviceID = "";

void configurePowerManagement() {
#if defined(ESP_PLATFORM)
#if defined(CONFIG_IDF_TARGET_ESP32C6) || defined(ARDUINO_ARCH_ESP32C6)
    // ESP32-C6 Bluetooth baseband & hardware encryption controller require active PLL (minimum 80 MHz)
    setCpuFrequencyMhz(80);
    Serial.printf("[POWER] ESP32-C6 Low-Power Mode: CPU @ %d MHz (PLL active, Modem OFF between bursts).\r\n", getCpuFrequencyMhz());
#else
    if (outputMode == MODE_BLE) {
        setCpuFrequencyMhz(40);
        Serial.printf("[POWER] Low-Power BLE Mode: CPU @ %d MHz (Modem OFF between bursts).\r\n", getCpuFrequencyMhz());
    } else {
        setCpuFrequencyMhz(80);
        Serial.printf("[POWER] CPU Frequency set to %d MHz (80MHz APB retained, zero SPI latency).\r\n", getCpuFrequencyMhz());
    }
#endif
#endif
}

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
    sensorTypeConfig = prefs.getUChar("sensor_type", 1);
    current_rate = prefs.getUChar("rate", DEFAULT_RATE);
    current_downsample = prefs.getUShort("downsample", (sensorTypeConfig == 1) ? 1000 : 1);
    outputMode = prefs.getUChar("mode", MODE_BLE);
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
        if (outputMode == MODE_BLE) {
            bleStream.powerDownModem();
        }
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

volatile uint32_t sampleCounter = 0;
float lastBmag_nT = 0.0f;

void sendOutputSample(const String &deviceID, uint64_t ts, float x, float y, float z, uint32_t status, const char *line, size_t len);

void sendOutputSample(uint64_t ts, float x, float y, float z, uint32_t status = 0xC00000) {
    sampleCounter = sampleCounter + 1;
    lastBmag_nT = sqrtf(x * x + y * y + z * z);
    char line[160];
    int len = snprintf(line, sizeof(line), "%s,%llu,%.2f,%.2f,%.2f,%06X\n", deviceID.c_str(), (unsigned long long)ts, x, y, z, (unsigned int)(status & 0xFFFFFF));
    sendOutputSample(deviceID, ts, x, y, z, status, line, (size_t)len);
}


static TelemetryRingBuffer telemetryRingBuffer;
static uint8_t lastSentCount = 0;
static uint32_t nextBurstTxMs = 0;
static bool isTxActive = false;
static uint32_t txStartMs = 0;
static uint8_t txRetryCount = 0;

static bool isModemWarming = false;
static uint32_t warmupStartMs = 0;
const uint32_t BLE_WARMUP_MS = 150; // 150ms pre-burst warmup for RF PLL and baseband stabilization

#ifndef DEBUG_BLE_TX
#define DEBUG_BLE_TX 1 // Set to 0 (or pass -D DEBUG_BLE_TX=0 in platformio.ini) to disable TX debug prints
#endif

#if DEBUG_BLE_TX
  #define TX_DEBUG_PRINTF(...) Serial.printf(__VA_ARGS__)
#else
  #define TX_DEBUG_PRINTF(...) ((void)0)
#endif

void checkBleAckTask() {
    if (isTxActive) {
        bool acked = bleStream.isBatchAcked();
        if ((millis() - txStartMs >= 300 && acked) || (millis() - txStartMs >= 1100)) {
            bleStream.powerDownModem();
#if !defined(CONFIG_IDF_TARGET_ESP32C6) && !defined(ARDUINO_ARCH_ESP32C6)
            if (outputMode == MODE_BLE) {
                setCpuFrequencyMhz(40);
            }
#endif
            isTxActive = false;
            isModemWarming = false;
            if (acked) {
                TX_DEBUG_PRINTF("[TX] Burst ACKED! Cleared %d samples.\n", lastSentCount);
                txRetryCount = 0;
                telemetryRingBuffer.confirmAck(lastSentCount);
            } else {
                txRetryCount++;
                TX_DEBUG_PRINTF("[TX] Burst NOT ACKED (timeout). Retry count: %d. Backlog: %d\n", txRetryCount, telemetryRingBuffer.getUnackedCount());
            }
            lastSentCount = 0;
        }
    }
}

void processBleTelemetry(const String &deviceID, uint64_t ts, float x, float y, float z, uint32_t status, const char *line) {
    // Insert new sample into 10-minute circular buffer
    uint32_t ts_ms = (uint32_t)(ts / 1000ULL);
    telemetryRingBuffer.push(ts_ms, x, y, z);
}

void checkBleBurstTransmission() {
    checkBleAckTask();

    if (!isTxActive && (outputMode == MODE_BLE || outputMode == MODE_BOTH)) {
        uint8_t targetBatchSize = (batchSizeConfig >= 1 && batchSizeConfig <= 18) ? batchSizeConfig : 10;
        uint32_t minBurstIntervalMs = (uint32_t)targetBatchSize * 1000UL;
        uint16_t unacked = telemetryRingBuffer.getUnackedCount();
        bool readyToSend = (unacked > 0 && (unacked >= targetBatchSize || !streaming));

        // Phase 1: Pre-burst Wakeup (150 ms lead time)
        // Re-enables the BLE controller and RF PLL so oscillators stabilize before packet transmission
        if (!isModemWarming && readyToSend && (millis() + BLE_WARMUP_MS >= nextBurstTxMs)) {
            setCpuFrequencyMhz(80);
            bleStream.powerUpModem(deviceID);
            isModemWarming = true;
            warmupStartMs = millis();
        }

        // Phase 2: Transmit Burst once target time is reached and stabilization period has completed
        if (isModemWarming && (millis() >= nextBurstTxMs || (millis() - warmupStartMs >= BLE_WARMUP_MS))) {
            uint8_t countToPack = targetBatchSize;
            if (unacked > targetBatchSize) {
                countToPack = min((uint16_t)18, unacked);
            }

            SensorBatchPacket batch;
            uint8_t countToSend = telemetryRingBuffer.getBatch(batch, deviceID.c_str(), countToPack);
            if (countToSend > 0) {
                batch.status = (uint16_t)(sensor ? 0x004D4F : 0);
                batch.vbat_mv = sampleBatteryMilliVolts(); // Sample fresh ADC reading during burst wakeup

                bleStream.clearBatchAck();
                bleStream.notifyBatchBinary(batch);
                lastSentCount = countToSend;

                // Drift-free periodic schedule on exact 10.0s grid
                nextBurstTxMs += minBurstIntervalMs;
                if (nextBurstTxMs <= millis() + BLE_WARMUP_MS) {
                    nextBurstTxMs = millis() + minBurstIntervalMs;
                }
                txStartMs = millis();
                isTxActive = true;
                isModemWarming = false;
            } else {
                bleStream.powerDownModem();
#if !defined(CONFIG_IDF_TARGET_ESP32C6) && !defined(ARDUINO_ARCH_ESP32C6)
                if (outputMode == MODE_BLE) {
                    setCpuFrequencyMhz(40);
                }
#endif
                isModemWarming = false;
            }
        }
    }
}

void processLoRaTelemetry(const String &deviceID, uint64_t ts, float x, float y, float z, uint32_t status, const char *line) {
    #if defined(BOARD_HAS_LORA)
    if (!loraStream.isInitialized()) {
        loraStream.begin(LORA_CS_PIN, LORA_DIO1_PIN, LORA_RST_PIN, LORA_BUSY_PIN, LORA_SCK_PIN, LORA_MISO_PIN, LORA_MOSI_PIN);
    }
    if (!loraStream.isInitialized()) return;

    uint8_t targetBatchSize = (batchSizeConfig >= 1 && batchSizeConfig <= 10) ? batchSizeConfig : 10;

    if (targetBatchSize == 1) {
        // Direct unbuffered single-sample transmission (1 packet per sample)
        SensorBinaryPacket pkt;
        memset(&pkt, 0, sizeof(pkt));
        strncpy(pkt.device_id, deviceID.c_str(), sizeof(pkt.device_id) - 1);
        pkt.packet_age_ms = 0; // Transmitted immediately upon sample capture
        pkt.x_nT = x;
        pkt.y_nT = y;
        pkt.z_nT = z;
        pkt.status = (uint16_t)(status & 0xFFFF);
        loraStream.transmit((const uint8_t*)&pkt, sizeof(pkt));
        loraStream.sleep();
    } else {
        // Batched telemetry mode (e.g. 10 samples per burst every 10 seconds)
        uint32_t ts_ms = (uint32_t)(ts / 1000ULL);
        static TelemetryRingBuffer loraRingBuffer;
        loraRingBuffer.push(ts_ms, x, y, z);

        static uint32_t lastLoraBurstTxMs = 0;
        uint32_t minBurstIntervalMs = (uint32_t)targetBatchSize * 1000UL;
        uint16_t unacked = loraRingBuffer.getUnackedCount();

        if (unacked > 0 && (unacked >= targetBatchSize || !streaming) && (millis() - lastLoraBurstTxMs >= minBurstIntervalMs)) {
            SensorBatchPacket batch;
            uint8_t countToSend = loraRingBuffer.getBatch(batch, deviceID.c_str(), targetBatchSize);
            if (countToSend > 0) {
                batch.status = (uint16_t)(status & 0xFFFF);
                batch.vbat_mv = sampleBatteryMilliVolts(); // Sample fresh ADC reading during 10s burst wakeup
                loraStream.transmit((const uint8_t*)&batch, sizeof(batch));
                loraStream.sleep(); // Put SX1262 into ultra-low-power sleep during idle gap
                loraRingBuffer.confirmAck(countToSend);
                lastLoraBurstTxMs = millis();
            }
        }
    }
    #endif
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

    if (outputMode == MODE_LORA) {
        processLoRaTelemetry(deviceID, ts, x, y, z, status, line);
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
            drdyAnomalyCount = drdyAnomalyCount + 1;
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

    sensor->setContinuousMode(true, current_rate);
    sensor->readAndPushSample(); // Clear pending DRDY latch on ASIC

    lastDrdyTimeUs = esp_timer_get_time();
    attachInterrupt(digitalPinToInterrupt(DRDY_PIN), drdyISR, sensorTypeConfig == 0 ? RISING : FALLING);
    Serial.println("[WATCHDOG RECOVERY] Sensor streaming recovered successfully.\r\n");
}


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

    // ALWAYS initialize SPI bus and DRDY pin regardless of initial configured sensor
    Serial.println("Initializing SPI Bus...");
    SPI.begin(SCK_PIN, MISO_PIN, MOSI_PIN, CS_PIN);
    pinMode(DRDY_PIN, INPUT_PULLUP);

    if (sensorTypeConfig == 1) {
        Serial.println("Configured sensor: FLC100-ADS131E08. Probing hardware...");
        bool found = false;
        for (int retry = 0; retry < 5 && !found; retry++) {
            if (sensorFLC100.begin()) {
                found = true;
                break;
            }
            delay(150);
        }
        if (found) {
            sensor = &sensorFLC100;
            static_cast<FLC100_ADS131*>(sensor)->setCalibration(2.4f, 20.0f, 1);
            attachInterrupt(digitalPinToInterrupt(DRDY_PIN), drdyISR, FALLING);
            Serial.println("Sensor initialized: FLC100-ADS131E08");
        } else {
            Serial.println("[ERROR] Physical FLC100 did not respond! Falling back to MOCK for this session.");
            sensor = &sensorMock;
            sensor->begin();
        }
    } else if (sensorTypeConfig == 0) {
        Serial.println("Configured sensor: RM3100. Probing hardware...");
        bool found = false;
        for (int retry = 0; retry < 5 && !found; retry++) {
            if (sensorRM3100.begin()) {
                found = true;
                break;
            }
            delay(150);
        }
        if (found) {
            sensor = &sensorRM3100;
            static_cast<RM3100*>(sensor)->setCycleCount(200, 200, 200);
            attachInterrupt(digitalPinToInterrupt(DRDY_PIN), drdyISR, RISING);
            Serial.println("Sensor initialized: RM3100");
        } else {
            Serial.println("[ERROR] Physical RM3100 did not respond! Falling back to MOCK for this session.");
            sensor = &sensorMock;
            sensor->begin();
        }
    } else if (sensorTypeConfig == 2) {
        sensor = &sensorMock;
        sensor->begin();
        Serial.println("[MOCK MODE] Explicit Synthetic Sensor Mode Active (Range Testing)");
        Serial.println("Status Header: 0x80MOCK");
    } else {
        Serial.println("Auto-probing physical sensor hardware...");
        bool found = false;
        for (int retry = 0; retry < 5 && !found; retry++) {
            if (sensorFLC100.begin()) {
                sensor = &sensorFLC100;
                sensorTypeConfig = 1;
                found = true;
                static_cast<FLC100_ADS131*>(sensor)->setCalibration(2.4f, 20.0f, 1);
                attachInterrupt(digitalPinToInterrupt(DRDY_PIN), drdyISR, FALLING);
                Serial.println("Auto-detected sensor: FLC100-ADS131E08");
                saveSettings();
                break;
            } else if (sensorRM3100.begin()) {
                sensor = &sensorRM3100;
                sensorTypeConfig = 0;
                found = true;
                static_cast<RM3100*>(sensor)->setCycleCount(200, 200, 200);
                attachInterrupt(digitalPinToInterrupt(DRDY_PIN), drdyISR, RISING);
                Serial.println("Auto-detected sensor: RM3100");
                saveSettings();
                break;
            }
            delay(150);
        }

        if (!found) {
            Serial.print("[SENSOR NOTICE] No physical SPI sensor detected. Falling back to Synthetic MOCK mode.\r\n");
            Serial.print("[HINT] Connect RM3100 or FLC100-ADS131E08 and reboot, or send 'SENSOR RM3100' / 'SENSOR FLC100'.\r\n");
            sensor = &sensorMock;
            sensorTypeConfig = 2;
            sensor->begin();
        }
    }

    Serial.print("Active Sensor: ");
    Serial.println(sensor->getSensorName());
    Serial.println(sensor->getStatusString());

    // Resume continuous mode with saved rate
    sensor->setContinuousMode(true, current_rate);

    // Create high-priority task for immediate ISR-notified ADC sampling (Pinned to Core 1 to isolate from WiFi on Core 0)
#if CONFIG_FREERTOS_UNICORE
    xTaskCreatePinnedToCore(adcSamplingTask, "ADC_Task", 4096, NULL, 3, &adcTaskHandle, 0);
#else
    xTaskCreatePinnedToCore(adcSamplingTask, "ADC_Task", 4096, NULL, configMAX_PRIORITIES - 1, &adcTaskHandle, 1);
#endif

    Serial.print("Device ID: ");
    Serial.println(deviceID);
    Serial.println("device_id,timestamp_us,x,y,z,status");
    serialCLI.printHelp();

#if LED_PIN >= 0
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LED_OFF);
#endif

    if (outputMode == MODE_LORA) {
        #if defined(BOARD_HAS_LORA)
        if (loraStream.begin(LORA_CS_PIN, LORA_DIO1_PIN, LORA_RST_PIN, LORA_BUSY_PIN, LORA_SCK_PIN, LORA_MISO_PIN, LORA_MOSI_PIN)) {
            loraStream.sleep(); // Deep sleep SX1262 until first packet transmission
        }
        #endif
    }

    // Stagger initial burst transmission based on MAC address to prevent multi-sensor collisions
    uint8_t staMac[6];
    esp_read_mac(staMac, ESP_MAC_WIFI_STA);
    uint32_t slotOffsetMs = (staMac[5] % 8) * 1250;
    nextBurstTxMs = millis() + slotOffsetMs + 10000;

    // Enable Automatic Tickless Light Sleep and Dynamic Frequency Scaling
    configurePowerManagement();
}

void loop() {
    checkBleBurstTransmission();

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
                lastBmag_nT,
                sampleCounter,
                getBatteryVoltage(), // 0ms instantaneous read from cache
                modeNames[outputMode % 5]
            );
        }
    }
#endif

    // Fallback periodic refresh when unbatched streaming (batch_size == 1)
    static uint32_t lastUnbatchedVbatMs = 0;
    if (batchSizeConfig == 1 && (millis() - lastUnbatchedVbatMs >= 10000)) {
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

    // Yield to FreeRTOS IDLE task to enable automatic dynamic CPU clock gating
    vTaskDelay(pdMS_TO_TICKS(1));
}
