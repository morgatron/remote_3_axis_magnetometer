#ifndef TELEMETRY_PACKET_H
#define TELEMETRY_PACKET_H

#include <Arduino.h>

/**
 * @brief Telemetry payload struct passed through FreeRTOS queue.
 */
struct TelemetryItem {
    char line[192];         // Formatted CSV line: device_id,timestamp_us,x_nT,y_nT,z_nT,status_hex,temp,vbat,rssi
    char node_id[32];       // Field sensor identifier string
    uint8_t mac[6];         // Transmitter MAC address
    int rssi;               // Signal strength in dBm
    float x;                // Field X in nT
    float y;                // Field Y in nT
    float z;                // Field Z in nT
    float temp;             // Temperature deg C
    float vbat;             // Battery voltage V
    uint32_t status;        // Hardware status code
    uint64_t timestamp_us;  // Sensor uptime in us
    char protocol[12];      // Protocol source ("ESP-NOW", "BLE", "WIFI_UDP")
};

/**
 * @brief Binary ESP-NOW telemetry packet struct sent by battery sensor nodes.
 * Total size: 48 bytes (aligned, efficient, low-power).
 */
typedef struct __attribute__((packed)) {
    char device_id[16];      // Null-terminated string or MAC-derived ID
    uint64_t timestamp_us;   // MCU micros() uptime
    float x_nT;              // Magnetic field X in nT
    float y_nT;              // Magnetic field Y in nT
    float z_nT;              // Magnetic field Z in nT
    uint32_t status;         // Hardware status word (e.g. 0xC00000)
    float temp;              // Optional sensor temperature in deg C
    uint16_t vbat_mv;        // Optional battery voltage in mV
} SensorBinaryPacket;

#endif // TELEMETRY_PACKET_H
