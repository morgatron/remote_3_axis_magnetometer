#ifndef MOCK_SENSOR_H
#define MOCK_SENSOR_H

#include <Arduino.h>
#include "Magnetometer.h"

/**
 * @brief Synthetic Mock Magnetometer Driver for Wireless Range & Pipeline Testing.
 * 
 * Generates realistic 3-axis magnetic vector field telemetry without requiring
 * physical SPI sensor hardware attached.
 */
class MockSensor : public Magnetometer {
public:
    MockSensor();
    ~MockSensor() override;

    // Magnetometer Interface
    bool begin() override;
    bool dataReady() override;
    using Magnetometer::readXYZ;
    void readXYZ(int32_t &x, int32_t &y, int32_t &z) override;
    void readXYZ(int32_t &x, int32_t &y, int32_t &z, uint32_t &status) override;
    void setContinuousMode(bool enable, uint8_t rate_code = 0x95) override;
    String getStatusString() override;
    String getSensorName() override { return "Mock Synthetic Sensor (Range Test)"; }
    float getScaleFactor() override { return 0.01f; }

    // Ring Buffer API
    void readAndPushSample() override;
    bool popSample(ADCSample &sample) override;
    bool isBufferEmpty() const override;

    /**
     * @brief Generate next synthetic sample and push to ring buffer (called periodically).
     */
    void generateSample();

private:
    static const size_t RING_BUFFER_SIZE = 128;
    ADCSample _ringBuffer[RING_BUFFER_SIZE];
    volatile size_t _ringHead = 0;
    volatile size_t _ringTail = 0;

    bool _enabled = false;
    uint32_t _sampleIntervalUs = 13333; // Default ~75 Hz
    uint64_t _lastSampleTimeUs = 0;
    float _phase = 0.0f;
};

#endif // MOCK_SENSOR_H
