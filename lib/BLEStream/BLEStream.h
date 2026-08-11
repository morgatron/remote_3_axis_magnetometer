#ifndef BLE_STREAM_H
#define BLE_STREAM_H

#include <Arduino.h>
#include "TelemetryPacket.h"

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
     * @brief Transmit data notification to connected BLE Central and broadcast in Manufacturer Data.
     * @param data Null-terminated CSV line string.
     */
    void notify(const char *data);

    /**
     * @brief Broadcast binary telemetry packet via BLE Manufacturer Data.
     * @param pkt SensorBinaryPacket struct.
     */
    void notifyBinary(const SensorBinaryPacket &pkt);
    
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

