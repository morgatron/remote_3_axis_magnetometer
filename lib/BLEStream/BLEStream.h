#ifndef BLE_STREAM_H
#define BLE_STREAM_H

#include <Arduino.h>
#include "TelemetryPacket.h"

namespace BLEConfig {
    constexpr uint16_t ADV_MIN_INTERVAL_UNITS = 80;   // 50 ms (80 * 0.625ms) compliant with scannable ext adv
    constexpr uint16_t ADV_MAX_INTERVAL_UNITS = 160;  // 100 ms (160 * 0.625ms)
    constexpr uint32_t BURST_DURATION_MS      = 1000; // 1000ms max burst window (shuts down early on ACK)
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
     * @brief Completely power down NimBLE and Bluetooth baseband controller.
     */
    void powerDownModem();

    /**
     * @brief Power up and re-initialize BLE modem and Coded PHY advertising.
     */
    void powerUpModem(const String &deviceName = "");

    /**
     * @brief Check if a central client is connected.
     * @return true if connected.
     */
    bool isConnected() const;

    /**
     * @brief Check if BLE modem controller is currently powered up.
     */
    bool isModemPowered() const;

private:
    bool _initialized;
    String _savedDeviceName;
};

extern BLEStream bleStream;

#endif // BLE_STREAM_H
