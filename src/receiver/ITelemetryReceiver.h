#ifndef ITELEMETRY_RECEIVER_H
#define ITELEMETRY_RECEIVER_H

#include <Arduino.h>

/**
 * @brief Abstract polymorphic interface for multi-protocol receiver drivers.
 * Provides a uniform API for initialization and non-blocking polling loops across
 * BLE, ESP-NOW, Wi-Fi UDP, and Sub-GHz LoRa drivers.
 */
class ITelemetryReceiver {
public:
    virtual ~ITelemetryReceiver() = default;

    /**
     * @brief Initialize hardware drivers, sockets, or Bluetooth scan callbacks.
     */
    virtual void begin() = 0;

    /**
     * @brief Non-blocking poll tick executed on every main loop iteration.
     * Default no-op implementation for asynchronous interrupt-driven drivers (BLE, ESP-NOW).
     */
    virtual void poll() {}

    /**
     * @brief Get protocol name string (e.g. "BLE", "ESP-NOW", "UDP", "LoRa").
     */
    virtual const char* getName() const = 0;
};

#endif // ITELEMETRY_RECEIVER_H
