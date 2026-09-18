#include <Arduino.h>
#include <NimBLEDevice.h>
#include "board_config.h"
#include "TelemetryPacket.h"

// Supermini ESP32-C3 Status LED
#ifndef LED_PIN
#define LED_PIN 8
#endif
#ifndef LED_ON
#define LED_ON LOW
#endif
#ifndef LED_OFF
#define LED_OFF HIGH
#endif

static unsigned long g_lastLedOnMs = 0;
static volatile bool g_ledState = false;
static uint32_t g_rxPacketCount = 0;
static uint8_t g_lastSeq = 255;
static uint64_t g_lastTimestampUs = 0;

void triggerLed() {
    digitalWrite(LED_PIN, LED_ON);
    g_lastLedOnMs = millis();
    g_ledState = true;
}

class BridgeScanCallbacks : public NimBLEScanCallbacks {
    void onResult(const NimBLEAdvertisedDevice* advertisedDevice) override {
        std::string manuData = advertisedDevice->getManufacturerData();
        size_t mlen = manuData.length();
        if (mlen < 26) return;

        const uint8_t* raw = (const uint8_t*)manuData.data();
        int rssi = advertisedDevice->getRSSI();

        // ---------------------------------------------------------------------
        // 1. Gateway 1M Extended Advertising Telemetry Broadcast (GatewayAdvPacket)
        // Magic bytes "MG" at offset 0 (if company ID stripped) or offset 2 (if 0xFFFF prefixed)
        // ---------------------------------------------------------------------
        int mgOffset = -1;
        if (mlen >= 27 && raw[0] == 'M' && raw[1] == 'G') {
            mgOffset = 0;
        } else if (mlen >= 29 && raw[0] == 0xFF && raw[1] == 0xFF && raw[2] == 'M' && raw[3] == 'G') {
            mgOffset = 2;
        }

        if (mgOffset >= 0) {
            const uint8_t* p = raw + mgOffset;
            size_t availableLen = mlen - mgOffset;

            uint8_t seq = p[2];
            char nodeId[9] = {0};
            memcpy(nodeId, p + 3, 8);

            uint64_t timestamp_us;
            memcpy(&timestamp_us, p + 11, sizeof(uint64_t));

            // Deduplicate repeat broadcasts of the exact same batch
            if (seq == g_lastSeq && timestamp_us == g_lastTimestampUs) {
                return;
            }
            g_lastSeq = seq;
            g_lastTimestampUs = timestamp_us;

            uint16_t sample_interval_ms;
            memcpy(&sample_interval_ms, p + 19, sizeof(uint16_t));
            if (sample_interval_ms == 0) sample_interval_ms = 1000;

            uint8_t sample_count = p[21];
            uint16_t status;
            memcpy(&status, p + 22, sizeof(uint16_t));

            uint16_t vbat_mv;
            memcpy(&vbat_mv, p + 24, sizeof(uint16_t));
            float vbat = (float)vbat_mv / 1000.0f;

            int8_t node_rssi = (int8_t)p[26];

            uint16_t gw_vbat_mv = 0;
            size_t headerSize = 27;
            if (availableLen >= 29) {
                memcpy(&gw_vbat_mv, p + 27, sizeof(uint16_t));
                headerSize = 29;
            }
            float gw_vbat = (float)gw_vbat_mv / 1000.0f;

            triggerLed();
            g_rxPacketCount++;

            if (sample_count == 0) {
                // Heartbeat packet from gateway
                Serial.printf("# [HEARTBEAT] ID: %s | Battery: %.2fV | Seq: %u | RSSI: %d dBm\n",
                              nodeId, gw_vbat > 0 ? gw_vbat : vbat, seq, rssi);
                return;
            }

            // Emit each sample as standard CSV
            for (uint8_t i = 0; i < sample_count; i++) {
                size_t sampleOffset = mgOffset + headerSize + (i * sizeof(CompactSample));
                if (sampleOffset + sizeof(CompactSample) > mlen) break;

                float x, y, z;
                memcpy(&x, raw + sampleOffset, sizeof(float));
                memcpy(&y, raw + sampleOffset + 4, sizeof(float));
                memcpy(&z, raw + sampleOffset + 8, sizeof(float));

                uint64_t offset_us = (uint64_t)(sample_count - 1 - i) * (uint64_t)sample_interval_ms * 1000ULL;
                uint64_t sample_ts = (timestamp_us >= offset_us) ? (timestamp_us - offset_us) : 0;

                // Standard telemetry CSV format:
                // node_id,timestamp_us,x_nT,y_nT,z_nT,status_hex,temp,vbat,rssi
                Serial.printf("%s,%llu,%.2f,%.2f,%.2f,%04X,0.0,%.2f,%d\n",
                              nodeId,
                              (unsigned long long)sample_ts,
                              x, y, z,
                              status,
                              vbat,
                              (int)node_rssi);
            }
            return;
        }

        // ---------------------------------------------------------------------
        // 2. Direct Field Node Batch Packet (SensorBatchPacket: 19 bytes + 12*N)
        // ---------------------------------------------------------------------
        if (mlen >= 19 + sizeof(CompactSample)) {
            size_t minBatchSize = 19 + sizeof(CompactSample);
            size_t maxOffset = (mlen > minBatchSize) ? min((size_t)4, mlen - minBatchSize) : 0;
            for (size_t offset = 0; offset <= maxOffset; offset++) {
                const uint8_t* p = raw + offset;
                uint8_t sampleCount = p[14];
                if (sampleCount >= 1 && sampleCount <= 18 && (mlen - offset >= 19 + sampleCount * sizeof(CompactSample))) {
                    char devId[9] = {0};
                    memcpy(devId, p, 8);
                    if (isprint(devId[0]) && isprint(devId[1])) {
                        uint32_t latestAgeMs;
                        memcpy(&latestAgeMs, p + 8, sizeof(uint32_t));
                        uint16_t sampleIntervalMs;
                        memcpy(&sampleIntervalMs, p + 12, sizeof(uint16_t));
                        if (sampleIntervalMs == 0) sampleIntervalMs = 1000;
                        uint16_t status;
                        memcpy(&status, p + 15, sizeof(uint16_t));
                        uint16_t vbatMv;
                        memcpy(&vbatMv, p + 17, sizeof(uint16_t));
                        float vbat = (float)vbatMv / 1000.0f;

                        triggerLed();
                        g_rxPacketCount++;

                        uint64_t nowUs = (uint64_t)millis() * 1000ULL;
                        for (uint8_t i = 0; i < sampleCount; i++) {
                            size_t sOff = offset + 19 + (i * sizeof(CompactSample));
                            float x, y, z;
                            memcpy(&x, raw + sOff, sizeof(float));
                            memcpy(&y, raw + sOff + 4, sizeof(float));
                            memcpy(&z, raw + sOff + 8, sizeof(float));

                            uint64_t offsetUs = (uint64_t)(sampleCount - 1 - i) * (uint64_t)sampleIntervalMs * 1000ULL;
                            uint64_t sTs = nowUs - ((uint64_t)latestAgeMs * 1000ULL) - offsetUs;

                            Serial.printf("%s,%llu,%.2f,%.2f,%.2f,%06X,0.0,%.2f,%d\n",
                                          devId,
                                          (unsigned long long)sTs,
                                          x, y, z,
                                          status,
                                          vbat,
                                          rssi);
                        }
                        return;
                    }
                }
            }
        }

        // ---------------------------------------------------------------------
        // 3. Direct Field Node Single Legacy Packet (SensorBinaryPacket: 26 bytes)
        // ---------------------------------------------------------------------
        size_t binSize = sizeof(SensorBinaryPacket);
        size_t maxOffset = (mlen >= binSize && mlen - binSize <= 4) ? (mlen - binSize) : 0;
        if (mlen >= binSize && mlen <= binSize + 4) {
            const uint8_t* p = raw + maxOffset;
            SensorBinaryPacket pkt;
            memcpy(&pkt, p, sizeof(pkt));

            // Basic sanity validation
            if (pkt.device_id[0] >= 32 && pkt.device_id[0] <= 126 &&
                !isnan(pkt.x_nT) && !isnan(pkt.y_nT) && !isnan(pkt.z_nT) &&
                abs(pkt.x_nT) < 1e7 && abs(pkt.y_nT) < 1e7 && abs(pkt.z_nT) < 1e7) {
                
                triggerLed();
                g_rxPacketCount++;

                char devId[9] = {0};
                memcpy(devId, pkt.device_id, 8);
                uint64_t ts_us = (uint64_t)millis() * 1000ULL;

                Serial.printf("%s,%llu,%.2f,%.2f,%.2f,%06X,0.0,0.0,%d\n",
                              devId,
                              (unsigned long long)ts_us,
                              pkt.x_nT, pkt.y_nT, pkt.z_nT,
                              pkt.status,
                              rssi);
                return;
            }
        }
    }
};

static BridgeScanCallbacks s_bridgeCallbacks;

void setup() {
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LED_OFF);

    // Native USB CDC serial (default to 921600 baud to match central_service/gateway.py)
    Serial.begin(921600);
    // Give USB CDC time to enumerate if host is connected
    delay(500);

    Serial.println(F("\n========================================================"));
    Serial.println(F(" ESP32-C3 Supermini BLE 1M -> Serial Bridge"));
    Serial.println(F("========================================================"));
    Serial.println(F("  Listening for: BLE 1M Extended Advertising packets"));
    Serial.println(F("  Output format: Standard CSV stream for gateway.py"));
    Serial.println(F("  Baud rate:     921600 / Native USB CDC"));
    Serial.println(F("========================================================\n"));

    // Quick startup LED flash
    for (int i = 0; i < 3; i++) {
        digitalWrite(LED_PIN, LED_ON);
        delay(60);
        digitalWrite(LED_PIN, LED_OFF);
        delay(60);
    }

    NimBLEDevice::init("BLE_SERIAL_BRIDGE");
    NimBLEDevice::setPower(9); // +9 dBm

    NimBLEScan* pScan = NimBLEDevice::getScan();
    pScan->setScanCallbacks(&s_bridgeCallbacks, false);
    pScan->setActiveScan(true);
    pScan->setDuplicateFilter(false); // Receive all packets, deduplication is handled in software
    pScan->setInterval(40);           // 25 ms interval
    pScan->setWindow(40);             // 25 ms window (100% duty cycle)
    pScan->setMaxResults(0);          // Continuous streaming without caching

    if (pScan->start(0, false)) {
        Serial.println(F("[BRIDGE ACTIVE] 100% duty cycle BLE scanner listening..."));
    } else {
        Serial.println(F("[ERROR] Failed to start BLE scanner!"));
    }
}

void loop() {
    // LED blink timeout
    if (g_ledState && (millis() - g_lastLedOnMs >= 25)) {
        digitalWrite(LED_PIN, LED_OFF);
        g_ledState = false;
    }

    delay(2);
}
