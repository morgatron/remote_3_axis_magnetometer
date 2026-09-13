#ifndef TELEMETRY_PACKET_H
#define TELEMETRY_PACKET_H

#include <Arduino.h>

// Telemetry Status Word Flags (uint16_t)
#define STATUS_FLAG_MOCK        0x8000  // Bit 15: 1 = Synthetic Mock Data, 0 = Physical Hardware
#define STATUS_FLAG_FLC100      0x0001  // Physical FLC100-ADS131E08 Fluxgate Sensor
#define STATUS_FLAG_RM3100      0x0002  // Physical RM3100 Magneto-Inductive Sensor

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
    char protocol[16];      // Protocol source ("ESP-NOW", "BLE", "WIFI_UDP", "LORA")

    /**
     * @brief Formats current item attributes into standard 9-column CSV string in line buffer.
     */
    void formatCsvLine() {
        snprintf(line, sizeof(line), "%s,%llu,%.2f,%.2f,%.2f,%06X,%.1f,%.2f,%d\n",
                 node_id[0] != '\0' ? node_id : "UNKNOWN",
                 (unsigned long long)timestamp_us,
                 x, y, z,
                 (unsigned int)(status & 0xFFFFFF),
                 temp, vbat, rssi);
    }

    /**
     * @brief Parses standard 6-column or 8-column ASCII CSV string into TelemetryItem attributes.
     * Format: device_id,timestamp_us,x_nT,y_nT,z_nT,status_hex[,temp,vbat]
     */
    static bool parseCsvLine(const char* csvLine, TelemetryItem &outItem) {
        if (!csvLine || strlen(csvLine) < 10) return false;
        char devBuf[32] = {0};
        unsigned long long ts = 0;
        float x = 0, y = 0, z = 0;
        unsigned int st = 0;
        float temp = 0.0f, vbat = 0.0f;

        int scanned = sscanf(csvLine, "%31[^,],%llu,%f,%f,%f,%x,%f,%f", devBuf, &ts, &x, &y, &z, &st, &temp, &vbat);
        if (scanned >= 5) {
            strncpy(outItem.node_id, devBuf, sizeof(outItem.node_id) - 1);
            outItem.timestamp_us = ts;
            outItem.x = x; outItem.y = y; outItem.z = z;
            outItem.status = st; outItem.temp = temp; outItem.vbat = vbat;
            return true;
        }
        return false;
    }
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
    float x_nT;
    float y_nT;
    float z_nT;
} CompactSample;

typedef struct __attribute__((packed)) {
    char          device_id[8];         // Null-terminated identifier (e.g. "NODE_3A8")
    uint32_t      latest_sample_age_ms; // Age in ms of newest sample (samples[sample_count-1]) at instant of TX
    uint16_t      sample_interval_ms;   // Time between samples in ms (e.g. 1000 ms = 1 Hz)
    uint8_t       sample_count;         // Number of samples in batch (up to 18)
    uint16_t      status;               // Status word
    uint16_t      vbat_mv;              // Battery voltage in mV
    int16_t       temp_c_x100;          // Temperature in deg C * 100 (e.g. 2350 = 23.50 C, 0x7FFF = invalid/unmeasured)
    CompactSample samples[18];          // Array of up to 18 compact samples (216 bytes, total = 237 bytes)
} SensorBatchPacket;

/**
 * @brief Gateway 1M Extended Advertising Telemetry Broadcast Packet.
 * Total size: 29 bytes header + up to 18 samples (12 bytes each) = 41 to 245 bytes.
 * Fits within standard 251-byte Bluetooth 5.0 LE Extended Advertising auxiliary PDU.
 */
typedef struct __attribute__((packed)) {
    uint16_t      company_id;           // 0xFFFF (Test / Non-registered company identifier)
    uint8_t       magic[2];             // 0x4D, 0x47 ("MG" for Mag Gateway)
    uint8_t       packet_seq;           // Transmission sequence counter (0-255)
    char          node_id[8];           // Field node identifier (null-terminated)
    uint64_t      timestamp_us;         // Microsecond timestamp of newest sample
    uint16_t      sample_interval_ms;   // Sample interval in ms (e.g. 1000)
    uint8_t       sample_count;         // Number of samples in packet (0 to 18)
    uint16_t      status;               // Status word
    uint16_t      vbat_mv;              // Battery voltage in mV
    int16_t       temp_c_x100;          // Temperature in deg C * 100 (0x7FFF = invalid/unmeasured)
    int8_t        rssi;                 // Signal strength from node to gateway in dBm
    uint16_t      gw_vbat_mv;           // Gateway battery voltage in mV
    CompactSample samples[18];          // Array of up to 18 samples (x, y, z in nT)
} GatewayAdvPacket;

#endif // TELEMETRY_PACKET_H
