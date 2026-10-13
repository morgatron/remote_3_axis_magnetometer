#ifndef SENSOR_MANAGER_H
#define SENSOR_MANAGER_H

#include <Arduino.h>
#include "SensorConfig.h"
#include "Magnetometer.h"
#include "RM3100.h"
#include "FLC100_ADS131.h"
#include "MockSensor.h"

class TelemetryEgress;

/**
 * @brief Manages magnetometer hardware lifecycle, auto-probing, continuous acquisition task,
 * DRDY interrupt servicing, and bus recovery.
 */
class SensorManager {
public:
    /**
     * @brief Construct a new SensorManager.
     * 
     * @param config Reference to global SensorConfig
     */
    SensorManager(SensorConfig& config);

    /**
     * @brief Initialize SPI bus, detect/probe magnetometer hardware, attach DRDY ISR,
     * and spawn the FreeRTOS background sampling task.
     * 
     * @return true if hardware or mock sensor successfully initialized
     */
    bool begin();

    /**
     * @brief Check for DRDY interrupt stalls (>500ms without sample) and recover the sensor if needed.
     * Call periodically in loop().
     */
    void checkWatchdog();

    /**
     * @brief Switch the active sensor at runtime (e.g., from CLI command).
     * 
     * @param newType SensorType enum value (SENSOR_TYPE_RM3100, SENSOR_TYPE_FLC100, SENSOR_TYPE_MOCK)
     * @return true if initialization succeeded
     */
    bool switchSensor(uint8_t newType);

    /**
     * @brief Switch sensor by name string ("RM3100", "FLC100", "MOCK").
     */
    bool switchSensorByName(const String& name);

    /**
     * @brief Update RM3100 cycle counts across X, Y, Z axes.
     */
    bool setCycleCount(uint16_t count);

    /**
     * @brief Update FLC100 ADS131E08 PGA gain.
     */
    bool setPgaGain(uint8_t gain);

    /**
     * @brief Update FLC100 calibration reference voltage.
     */
    bool setVref(float vref);

    /**
     * @brief Enable/disable internal test signal on FLC100.
     */
    bool setTestSignal(bool enable);

    /**
     * @brief Attempt watchdog bus recovery on DRDY stall.
     */
    void recoverSensor();

    /**
     * @brief Pop samples from active sensor ring buffer, apply scaling,
     * perform downsampling averaging, and dispatch through TelemetryEgress.
     * Call regularly in loop().
     */
    void processSamples(TelemetryEgress& telemetry);

    /**
     * @brief Get pointer to active sensor.
     */
    Magnetometer* getActiveSensor() const { return _activeSensor; }

    /**
     * @brief Get reference to active sensor pointer.
     */
    Magnetometer*& getSensorRef() { return _activeSensor; }

    /**
     * @brief Get reference to config.
     */
    SensorConfig& getConfig() const { return _config; }

private:
    bool probeFLC100(int retries = 10);
    bool probeRM3100(int retries = 10);
    void attachDrdyInterrupt();

    SensorConfig& _config;

    RM3100 _sensorRM3100;
    FLC100_ADS131 _sensorFLC100;
    MockSensor _sensorMock;

    Magnetometer* _activeSensor;
    uint32_t _lastWdCheckMs = 0;

    // Decimation moving average accumulators
    float _sumX = 0.0f;
    float _sumY = 0.0f;
    float _sumZ = 0.0f;
    uint16_t _decimationCounter = 0;
};

#endif // SENSOR_MANAGER_H
