#ifndef SENSOR_CONFIG_H
#define SENSOR_CONFIG_H

#include <Arduino.h>
#include <IPAddress.h>

enum OutputMode : uint8_t {
    MODE_SERIAL = 0,
    MODE_WIFI   = 1,
    MODE_BOTH   = 2,
    MODE_BLE    = 3,
    MODE_LORA   = 4
};

enum SensorType : uint8_t {
    SENSOR_TYPE_RM3100 = 0,
    SENSOR_TYPE_FLC100 = 1,
    SENSOR_TYPE_MOCK   = 2,
    SENSOR_TYPE_AUTO   = 255
};

/**
 * @brief Centralized configuration and NVS persistence manager for sensor nodes.
 *
 * Encapsulates all persistent user settings (streaming, rates, downsample,
 * output modes, batch burst sizing, and network targets) in flash memory (NVS).
 */
struct SensorConfig {
    bool streaming;
    uint8_t rate;
    uint16_t downsample;
    uint8_t sensorType;
    uint8_t outputMode;
    uint8_t batchSize;
    uint16_t cycleCount;
    String wifiSSID;
    String wifiPass;
    IPAddress targetIP;
    uint16_t targetPort;
    uint16_t udpListenPort;
    String deviceID;

    SensorConfig();

    void load();
    void save();
    const char* getModeString() const;
    const char* getSensorTypeString() const;
};

extern SensorConfig g_sensorConfig;

#endif // SENSOR_CONFIG_H
