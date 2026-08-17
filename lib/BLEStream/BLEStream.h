#ifndef BLE_STREAM_H
#define BLE_STREAM_H

#include <Arduino.h>
#include "TelemetryPacket.h"

namespace BLEConfig {
    constexpr uint16_t ADV_MIN_INTERVAL_UNITS = 80;   // 50 ms (80 * 0.625ms)
    constexpr uint16_t ADV_MAX_INTERVAL_UNITS = 160;  // 100 ms (160 * 0.625ms)
    constexpr uint32_t BURST_DURATION_MS      = 1000; // 1.0s awake window for transmission & ACKs
    constexpr int8_t   TX_POWER_DBM           = 15;   // +15 dBm max power
}

/**
 * @brief Lightweight Bluetooth 5.0 LE / Long Range (Coded PHY) Streaming Driver.
 */
class BLEStream {
public:
    BLEStream();
    
    /**
     * @brief Initialize NimBLE server and Coded PHY advertising.
     * @param deviceName Unique Device ID / Node name.
     */
    void begin(const String &deviceName);
    
    /**
     * @brief Transmit data notification to connected BLE Central.
     * @param data Null-terminated CSV line string.
     */
    void notify(const char *data);

    /**
     * @brief Broadcast binary telemetry packet via BLE Manufacturer Data.
     * @param pkt SensorBinaryPacket struct.
     */
    void notifyBinary(const SensorBinaryPacket &pkt);
    
    /**
     * @brief Broadcast 10-sample batch burst via Extended Advertising Coded PHY.
     * @param batch SensorBatchPacket struct.
     */
    void notifyBatchBinary(const SensorBatchPacket &batch);
    
    /**
     * @brief Check if gateway receiver sent a hardware AUX_SCAN_REQ acknowledgment for the last batch.
     */
    bool isBatchAcked() const;

    /**
     * @brief Reset batch ACK flag before next batch transmission.
     */
    void clearBatchAck();

    /**
     * @brief Stop active BLE advertising instance.
     */
    void stopAdvertising();

    /**
     * @brief Check if a central client is connected.
     * @return true if connected.
     */
    bool isConnected() const;

private:
    bool _initialized;
};

extern BLEStream bleStream;

#endif // BLE_STREAM_H
