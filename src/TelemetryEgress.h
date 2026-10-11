#ifndef TELEMETRY_EGRESS_H
#define TELEMETRY_EGRESS_H

#include <Arduino.h>
#include <WiFiUdp.h>
#include "board_config.h"
#include "SensorConfig.h"
#include "Magnetometer.h"
#include "TelemetryPacket.h"
#include "TelemetryRingBuffer.h"

// Forward declaration for frequency management
void safeSetCpuFrequencyMhz(uint32_t freq);

/**
 * @brief Manages dispatching of magnetometer telemetry across all enabled outputs:
 * Serial, WiFi UDP (batched), BLE Coded PHY bursts, and LoRa packets/bursts,
 * as well as output mode lifecycle and power management.
 */
class TelemetryEgress {
public:
    /**
     * @brief Construct a new TelemetryEgress object.
     * 
     * @param config Reference to global SensorConfig
     * @param sensor Reference to active Magnetometer pointer
     */
    TelemetryEgress(SensorConfig& config, Magnetometer*& sensor);

    /**
     * @brief Initialize telemetry subsystems and active output mode (BLE, WiFi, LoRa).
     */
    void begin();

    /**
     * @brief Format and send a single magnetometer sample across enabled outputs.
     * 
     * @param ts Timestamp in microseconds
     * @param x Magnetic field X in nT
     * @param y Magnetic field Y in nT
     * @param z Magnetic field Z in nT
     * @param status Hardware status code
     */
    void sendSample(uint64_t ts, float x, float y, float z, uint32_t status = 0xC00000);

    /**
     * @brief Periodic polling loop for BLE bursts, ACK verification, and UDP batch flushing.
     * Call regularly in Arduino loop().
     */
    void update();

    /**
     * @brief Immediately flush pending WiFi UDP packet buffer.
     */
    void flushUdp();

    /**
     * @brief Apply current output mode configuration, managing radio/modem power states.
     */
    bool applyMode();

    /**
     * @brief Set and apply output mode (MODE_SERIAL, MODE_WIFI, MODE_BOTH, MODE_BLE, MODE_LORA).
     */
    bool setOutputMode(uint8_t mode);

    /**
     * @brief Attempt connection to configured WiFi network.
     */
    bool connectWiFi();

    /**
     * @brief Disconnect WiFi and turn off WiFi radio.
     */
    void disconnectWiFi();

    /**
     * @brief Check whether WiFi is currently connected.
     */
    bool isWifiConnected() const { return _wifiConnected; }

    /**
     * @brief Configure Dynamic Frequency Scaling and Tickless Light Sleep for active mode.
     */
    void configurePowerManagement();

    /**
     * @brief Access UDP socket for command reception.
     */
    WiFiUDP& getUdp() { return _udp; }

#if defined(BOARD_HAS_LORA)
    /**
     * @brief Configure LoRa SX1262 transmission power in dBm.
     */
    void setLoRaPower(int8_t powerDbm);

    /**
     * @brief Check whether LoRa radio is initialized.
     */
    bool isLoRaInitialized() const;

    /**
     * @brief Get active LoRa transmit power in dBm.
     */
    int8_t getLoRaPower() const;
#endif

    /**
     * @brief Get cumulative sample count processed.
     */
    uint32_t getSampleCount() const { return _sampleCounter; }

    /**
     * @brief Get last calculated magnetic magnitude |B| in nT.
     */
    float getLastBmag() const { return _lastBmag; }

    /**
     * @brief Get 16-bit telemetry status word for active sensor.
     */
    uint16_t getStatusWord(uint32_t rawStatus = 0) const;

private:
    void sendSampleInternal(const String& deviceID, uint64_t ts, float x, float y, float z, uint32_t status, const char* line, size_t len);
    void processBleTelemetry(const String& deviceID, uint64_t ts, float x, float y, float z, uint32_t status, const char* line);
    void checkBleBurstTransmission();
    void checkBleAckTask();
    void processLoRaTelemetry(const String& deviceID, uint64_t ts, float x, float y, float z, uint32_t status, const char* line);

    SensorConfig& _config;
    Magnetometer*& _sensor;

    WiFiUDP _udp;
    bool _wifiConnected = false;

    uint32_t _sampleCounter = 0;
    float _lastBmag = 0.0f;

    // UDP batch buffer
    char _udpBatchBuf[4096];
    size_t _udpBatchLen = 0;
    uint32_t _lastUdpFlushMs = 0;

    // BLE Ring Buffer & Burst state
    TelemetryRingBuffer _telemetryRingBuffer;
    uint8_t _lastSentCount = 0;
    uint32_t _nextBurstTxMs = 0;
    bool _isTxActive = false;
    uint32_t _txStartMs = 0;
    uint8_t _txRetryCount = 0;
    uint32_t _activeBurstDurationMs = 1000;
    bool _isModemWarming = false;
    uint32_t _warmupStartMs = 0;

#if defined(BOARD_HAS_LORA)
    TelemetryRingBuffer _loraRingBuffer;
    uint32_t _lastLoraBurstTxMs = 0;
#endif
};

#endif // TELEMETRY_EGRESS_H
