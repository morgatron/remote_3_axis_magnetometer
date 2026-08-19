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
 * @brief Binary BLE & ESP-NOW telemetry packet struct sent by field sensor nodes.
 * Total size: 26 bytes (fits inside standard BLE advertisement payload limits <= 29 bytes).
 */
typedef struct __attribute__((packed)) {
    char     device_id[8];       // Null-terminated string (e.g. "NODE_B5")
    uint32_t packet_age_ms;      // Age of sample in ms at instant of RF transmission
    float    x_nT;              // Magnetic field X in nT
    float    y_nT;              // Magnetic field Y in nT
    float    z_nT;              // Magnetic field Z in nT
    uint16_t status;             // Hardware status word
} SensorBinaryPacket;

typedef struct __attribute__((packed)) {
    int32_t x_nT;
    int32_t y_nT;
    int32_t z_nT;
} CompactSample;

typedef struct __attribute__((packed)) {
    char          device_id[8];         // Null-terminated identifier (e.g. "NODE_3A8")
    uint32_t      latest_sample_age_ms; // Age in ms of newest sample (samples[sample_count-1]) at instant of TX
    uint16_t      sample_interval_ms;   // Time between samples in ms (e.g. 1000 ms = 1 Hz)
    uint8_t       sample_count;         // Number of samples in batch (up to 10)
    uint16_t      status;               // Status word
    uint16_t      vbat_mv;              // Battery voltage in mV
    CompactSample samples[10];          // Array of up to 10 compact samples (120 bytes)
} SensorBatchPacket;

#endif // TELEMETRY_PACKET_H
