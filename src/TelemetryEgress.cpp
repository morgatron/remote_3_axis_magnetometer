#include "TelemetryEgress.h"
#include "board_config.h"
#include "BLEStream.h"
#if defined(BOARD_HAS_LORA)
#include "LoRaStream.h"
#endif
#include <WiFi.h>
#include <esp_mac.h>

#ifndef DEBUG_BLE_TX
#define DEBUG_BLE_TX 1 // Set to 0 to disable TX debug prints
#endif

#if DEBUG_BLE_TX
  #define TX_DEBUG_PRINTF(...) Serial.printf(__VA_ARGS__)
#else
  #define TX_DEBUG_PRINTF(...) ((void)0)
#endif

static const uint32_t BLE_WARMUP_MS = 150; // 150ms pre-burst warmup for RF PLL and baseband stabilization

TelemetryEgress::TelemetryEgress(SensorConfig& config, Magnetometer*& sensor, WiFiUDP& udp, const bool& wifiConnected)
    : _config(config), _sensor(sensor), _udp(udp), _wifiConnected(wifiConnected) {}

void TelemetryEgress::begin() {
#if defined(BOARD_HAS_LORA)
    if (_config.outputMode == MODE_LORA) {
        LoRaConfig cfg = {915.0f, 125.0f, 7, 5, LORA_TX_POWER_DBM, 8};
        if (loraStream.begin(LORA_CS_PIN, LORA_DIO1_PIN, LORA_RST_PIN, LORA_BUSY_PIN, LORA_SCK_PIN, LORA_MISO_PIN, LORA_MOSI_PIN, nullptr, cfg)) {
            loraStream.sleep(); // Deep sleep SX1262 until first packet transmission
        }
    }
#endif

    // Stagger initial burst transmission based on MAC address to prevent multi-sensor collisions
    uint8_t staMac[6];
    esp_read_mac(staMac, ESP_MAC_WIFI_STA);
    uint32_t slotOffsetMs = (staMac[5] % 8) * 1250;
    _nextBurstTxMs = millis() + slotOffsetMs + 10000;
}

uint16_t TelemetryEgress::getStatusWord(uint32_t rawStatus) const {
    if (_config.sensorType == SENSOR_TYPE_MOCK || (_sensor != nullptr && _sensor->getSensorName().indexOf("Mock") >= 0)) {
        return STATUS_FLAG_MOCK;
    }
    if (_config.sensorType == SENSOR_TYPE_FLC100) {
        return (uint16_t)(rawStatus & 0x7FFE) | STATUS_FLAG_FLC100;
    }
    if (_config.sensorType == SENSOR_TYPE_RM3100) {
        return (uint16_t)(rawStatus & 0x7FFD) | STATUS_FLAG_RM3100;
    }
    return 0x0000;
}

void TelemetryEgress::sendSample(uint64_t ts, float x, float y, float z, uint32_t status) {
    _sampleCounter++;
    _lastBmag = sqrtf(x * x + y * y + z * z);
    char line[160];
    uint32_t finalStatus = status;
    if (_config.sensorType == SENSOR_TYPE_MOCK || (_sensor != nullptr && _sensor->getSensorName().indexOf("Mock") >= 0)) {
        finalStatus = 0x800000 | STATUS_FLAG_MOCK;
    }
    float tC = (_sensor != nullptr) ? _sensor->readTemperatureC() : -999.0f;
    float tVal = (tC > -200.0f && tC < 200.0f) ? tC : 0.0f;
    int len = snprintf(line, sizeof(line), "%s,%llu,%.2f,%.2f,%.2f,%06X,%.1f,%.2f\n",
                       _config.deviceID.c_str(), (unsigned long long)ts, x, y, z,
                       (unsigned int)(finalStatus & 0xFFFFFF), tVal, g_cachedBatteryVoltage);
    sendSampleInternal(_config.deviceID, ts, x, y, z, finalStatus, line, (size_t)len);
}

void TelemetryEgress::sendSampleInternal(const String& deviceID, uint64_t ts, float x, float y, float z, uint32_t status, const char* line, size_t len) {
    // Non-blocking Serial output (prevents USB CDC buffer stalls when host monitor is not attached)
    if (_config.outputMode == MODE_SERIAL || _config.outputMode == MODE_BOTH) {
        if (Serial.availableForWrite() >= len) {
            Serial.print(line);
        }
    }

    if ((_config.outputMode == MODE_WIFI || _config.outputMode == MODE_BOTH) && _wifiConnected) {
        if (_udpBatchLen + len >= sizeof(_udpBatchBuf) - 1) {
            flushUdp();
        }
        memcpy(_udpBatchBuf + _udpBatchLen, line, len);
        _udpBatchLen += len;

        // Flush UDP batch every 1000 ms (1 Hz packet rate)
        if (millis() - _lastUdpFlushMs >= 1000) {
            flushUdp();
        }
    }

    if (_config.outputMode == MODE_BLE || _config.outputMode == MODE_BOTH) {
        processBleTelemetry(deviceID, ts, x, y, z, status, line);
    }

    if (_config.outputMode == MODE_LORA) {
        processLoRaTelemetry(deviceID, ts, x, y, z, status, line);
    }
}

void TelemetryEgress::flushUdp() {
    if (_udpBatchLen > 0 && _wifiConnected) {
        _udp.beginPacket(_config.targetIP, _config.targetPort);
        _udp.write((const uint8_t*)_udpBatchBuf, _udpBatchLen);
        _udp.endPacket();
        _udpBatchLen = 0;
        _lastUdpFlushMs = millis();
    }
}

void TelemetryEgress::update() {
    checkBleBurstTransmission();

    if (_wifiConnected) {
        if (_udpBatchLen > 0 && (millis() - _lastUdpFlushMs >= 50)) {
            flushUdp();
        }
    }
}

void TelemetryEgress::processBleTelemetry(const String& deviceID, uint64_t ts, float x, float y, float z, uint32_t status, const char* line) {
    // Insert new sample into circular buffer (3.0 hours capacity @ 1 Hz)
    uint32_t ts_ms = (uint32_t)(ts / 1000ULL);
    _telemetryRingBuffer.push(ts_ms, x, y, z);
}

void TelemetryEgress::checkBleAckTask() {
    if (_isTxActive) {
        bool acked = bleStream.isBatchAcked();
        uint32_t elapsed = millis() - _txStartMs;
        uint32_t timeoutMs = _activeBurstDurationMs + 100;
        if ((elapsed >= 250 && acked) || (elapsed >= timeoutMs)) {
            bleStream.stopAdvertising();
            bleStream.powerDownModem();
#if !defined(CONFIG_IDF_TARGET_ESP32C6) && !defined(ARDUINO_ARCH_ESP32C6)
            if (_config.outputMode == MODE_BLE) {
                safeSetCpuFrequencyMhz(40);
            }
#endif
            _isTxActive = false;
            _isModemWarming = false;
            if (acked) {
                TX_DEBUG_PRINTF("[TX] Burst ACKED! Cleared %d samples.\n", _lastSentCount);
                _txRetryCount = 0;
                _telemetryRingBuffer.confirmAck(_lastSentCount);
            } else {
                _txRetryCount++;
                TX_DEBUG_PRINTF("[TX] Burst NOT ACKED (timeout). Retry count: %d. Backlog: %d\n", _txRetryCount, _telemetryRingBuffer.getUnackedCount());
            }
            _lastSentCount = 0;
        }
    }
}

void TelemetryEgress::checkBleBurstTransmission() {
    checkBleAckTask();

    if (!_isTxActive && (_config.outputMode == MODE_BLE || _config.outputMode == MODE_BOTH)) {
        uint8_t targetBatchSize = (_config.batchSize >= 1 && _config.batchSize <= 18) ? _config.batchSize : 10;
        uint32_t minBurstIntervalMs = (uint32_t)targetBatchSize * 1000UL;
        uint16_t unacked = _telemetryRingBuffer.getUnackedCount();
        bool readyToSend = (unacked > 0 && (unacked >= targetBatchSize || !_config.streaming));

        // Phase 1: Pre-burst Wakeup (150 ms lead time)
        // Re-enables the BLE controller and RF PLL so oscillators stabilize before packet transmission
        if (!_isModemWarming && readyToSend && (millis() + BLE_WARMUP_MS >= _nextBurstTxMs)) {
            safeSetCpuFrequencyMhz(80);
            bleStream.powerUpModem(_config.deviceID);
            _isModemWarming = true;
            _warmupStartMs = millis();
        }

        // Phase 2: Transmit Burst once target time is reached and stabilization period has completed
        if (_isModemWarming && (millis() >= _nextBurstTxMs || (millis() - _warmupStartMs >= BLE_WARMUP_MS))) {
            uint8_t countToPack = targetBatchSize;
            if (unacked > targetBatchSize) {
                countToPack = min((uint16_t)18, unacked);
            }

            SensorBatchPacket batch;
            uint8_t countToSend = _telemetryRingBuffer.getBatch(batch, _config.deviceID.c_str(), countToPack);
            if (countToSend > 0) {
                batch.status = getStatusWord();
                batch.vbat_mv = sampleBatteryMilliVolts(); // Sample fresh ADC reading during burst wakeup
                float tC = (_sensor != nullptr) ? _sensor->readTemperatureC() : -999.0f;
                batch.temp_c_x100 = (tC > -200.0f && tC < 200.0f) ? (int16_t)roundf(tC * 100.0f) : 0x7FFF;

                bleStream.clearBatchAck();
                _activeBurstDurationMs = (_txRetryCount > 0) ? 2500 : BLEConfig::BURST_DURATION_MS;
                bleStream.notifyBatchBinary(batch, _activeBurstDurationMs);
                _lastSentCount = countToSend;

                // Drift-free periodic schedule on exact 10.0s grid
                _nextBurstTxMs += minBurstIntervalMs;
                if (_nextBurstTxMs <= millis() + BLE_WARMUP_MS) {
                    _nextBurstTxMs = millis() + minBurstIntervalMs;
                }
                _txStartMs = millis();
                _isTxActive = true;
                _isModemWarming = false;
            } else {
                bleStream.powerDownModem();
#if !defined(CONFIG_IDF_TARGET_ESP32C6) && !defined(ARDUINO_ARCH_ESP32C6)
                if (_config.outputMode == MODE_BLE) {
                    safeSetCpuFrequencyMhz(40);
                }
#endif
                _isModemWarming = false;
            }
        }
    }
}

void TelemetryEgress::processLoRaTelemetry(const String& deviceID, uint64_t ts, float x, float y, float z, uint32_t status, const char* line) {
#if defined(BOARD_HAS_LORA)
    if (!loraStream.isInitialized()) {
        LoRaConfig cfg = {915.0f, 125.0f, 7, 5, LORA_TX_POWER_DBM, 8};
        loraStream.begin(LORA_CS_PIN, LORA_DIO1_PIN, LORA_RST_PIN, LORA_BUSY_PIN, LORA_SCK_PIN, LORA_MISO_PIN, LORA_MOSI_PIN, nullptr, cfg);
    }
    if (!loraStream.isInitialized()) return;

    uint8_t targetBatchSize = (_config.batchSize >= 1 && _config.batchSize <= 10) ? _config.batchSize : 10;

    if (targetBatchSize == 1) {
        // Direct unbuffered single-sample transmission (1 packet per sample)
        SensorBinaryPacket pkt;
        memset(&pkt, 0, sizeof(pkt));
        size_t idLen = deviceID.length();
        if (idLen > sizeof(pkt.device_id)) idLen = sizeof(pkt.device_id);
        memcpy(pkt.device_id, deviceID.c_str(), idLen);
        pkt.packet_age_ms = 0; // Transmitted immediately upon sample capture
        pkt.x_nT = x;
        pkt.y_nT = y;
        pkt.z_nT = z;
        pkt.status = getStatusWord(status);
        bool ok = loraStream.transmit((const uint8_t*)&pkt, sizeof(pkt));
        loraStream.sleep();
        Serial.printf("[LORA TX] Single packet sent (result: %s)\r\n", ok ? "OK" : "FAIL");
    } else {
        // Batched telemetry mode (e.g. 10 samples per burst every 10 seconds)
        uint32_t ts_ms = (uint32_t)(ts / 1000ULL);
        _loraRingBuffer.push(ts_ms, x, y, z);

        uint32_t minBurstIntervalMs = (uint32_t)targetBatchSize * 1000UL;
        uint16_t unacked = _loraRingBuffer.getUnackedCount();

        if (unacked > 0 && (unacked >= targetBatchSize || !_config.streaming) && (millis() - _lastLoraBurstTxMs >= minBurstIntervalMs)) {
            SensorBatchPacket batch;
            uint8_t countToSend = _loraRingBuffer.getBatch(batch, deviceID.c_str(), targetBatchSize);
            if (countToSend > 0) {
                batch.status = getStatusWord(status);
                batch.vbat_mv = sampleBatteryMilliVolts(); // Sample fresh ADC reading during 10s burst wakeup
                float tC = (_sensor != nullptr) ? _sensor->readTemperatureC() : -999.0f;
                batch.temp_c_x100 = (tC > -200.0f && tC < 200.0f) ? (int16_t)roundf(tC * 100.0f) : 0x7FFF;
                bool ok = loraStream.transmit((const uint8_t*)&batch, sizeof(batch));
                loraStream.sleep(); // Put SX1262 into ultra-low-power sleep during idle gap
                _loraRingBuffer.confirmAck(countToSend);
                _lastLoraBurstTxMs = millis();
                Serial.printf("[LORA TX] Sent batch of %d samples from '%s' (result: %s, unacked: %d)\r\n",
                              countToSend, deviceID.c_str(), ok ? "OK" : "FAIL", _loraRingBuffer.getUnackedCount());
            }
        }
    }
#endif
}
