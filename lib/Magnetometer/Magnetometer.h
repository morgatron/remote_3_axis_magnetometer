#ifndef MAGNETOMETER_H
#define MAGNETOMETER_H

#include <Arduino.h>

struct ADCSample {
    uint64_t ts;
    int32_t x;
    int32_t y;
    int32_t z;
    uint32_t status;
};

/**
 * @brief Abstract base class for 3-axis magnetometers.
 */
class Magnetometer {
public:
    virtual ~Magnetometer() {}

    /**
     * @brief Initialize the sensor.
     * @return true if initialization was successful.
     */
    virtual bool begin() = 0;

    /**
     * @brief Check if new data is available.
     * @return true if data is ready to be read.
     */
    virtual bool dataReady() = 0;

    /**
     * @brief Read raw X, Y, Z magnetic field values.
     * @param x Reference to store X-axis value.
     * @param y Reference to store Y-axis value.
     * @param z Reference to store Z-axis value.
     */
    virtual void readXYZ(int32_t &x, int32_t &y, int32_t &z) = 0;

    /**
     * @brief Read raw X, Y, Z magnetic field values along with the 24-bit status header.
     * @param x Reference to store X-axis value.
     * @param y Reference to store Y-axis value.
     * @param z Reference to store Z-axis value.
     * @param status Reference to store 24-bit raw status header.
     */
    virtual void readXYZ(int32_t &x, int32_t &y, int32_t &z, uint32_t &status) {
        readXYZ(x, y, z);
        status = 0xC00000;
    }

    /**
     * @brief Read sample via SPI and push to ring buffer (called by high-priority sampling task).
     */
    virtual void readAndPushSample() = 0;

    /**
     * @brief Pop a sample from the ring buffer.
     * @param sample Reference to store popped sample.
     * @return true if sample was successfully popped.
     */
    virtual bool popSample(ADCSample &sample) = 0;

    /**
     * @brief Check if the ring buffer is empty.
     * @return true if ring buffer is empty.
     */
    virtual bool isBufferEmpty() const = 0;

    /**
     * @brief Enable or disable continuous measurement mode.
     * @param enable True to enable, false to disable.
     * @param rate_code Implementation-specific rate code.
     */
    virtual void setContinuousMode(bool enable, uint8_t rate_code) = 0;

    /**
     * @brief Get a sensor-specific identifier or status string.
     * @return String describing the sensor status or ID.
     */
    virtual String getStatusString() = 0;

    /**
     * @brief Get the name of the sensor model.
     * @return String sensor name.
     */
    virtual String getSensorName() = 0;
};

#endif
