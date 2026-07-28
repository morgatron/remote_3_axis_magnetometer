#ifndef MAGNETOMETER_H
#define MAGNETOMETER_H

#include <Arduino.h>

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
