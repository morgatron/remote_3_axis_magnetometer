#ifndef LORA_STREAM_H
#define LORA_STREAM_H

#include <Arduino.h>
#include <SPI.h>

/**
 * @brief SX1262 LoRa Radio Configuration Struct
 */
struct LoRaConfig {
    float frequency;        // 915.0 MHz (US/AU) or 868.0 MHz (EU)
    float bandwidth;        // 125.0, 250.0, or 500.0 kHz
    uint8_t spreadingFactor;// SF7 to SF12
    uint8_t codingRate;     // 5 (4/5) to 8 (4/8)
    int8_t power;           // Output power in dBm (+22 max for SX1262)
    uint16_t preambleLength;// Preamble symbols (default 8)
};

/**
 * @brief Lightweight SX1262 Sub-GHz LoRa Transceiver Driver.
 */
class LoRaStream {
public:
    LoRaStream();

    /**
     * @brief Initialize SPI bus and SX1262 module.
     * @param cs Pin CS
     * @param irq Pin DIO1
     * @param rst Pin RESET
     * @param gpio Pin BUSY
     * @param spiBus Pointer to dedicated SPIClass instance
     * @param cfg Radio frequency & spreading factor configuration
     * @return true if SX1262 initialization succeeds
     */
    bool begin(int cs, int irq, int rst, int gpio, SPIClass *spiBus = nullptr, LoRaConfig cfg = {915.0f, 500.0f, 7, 5, 22, 8});

    /**
     * @brief Transmit telemetry binary packet or string payload over LoRa.
     * @param data Payload buffer
     * @param len Payload length in bytes
     * @return true if packet transmission succeeded
     */
    bool transmit(const uint8_t *data, size_t len);

    /**
     * @brief Set receiver continuous mode.
     */
    bool startReceive();

    /**
     * @brief Check if packet received.
     * @param buf Output buffer for received payload
     * @param maxLen Buffer capacity
     * @param rssi Output RSSI signal strength
     * @param snr Output Signal-to-Noise Ratio
     * @return Number of received bytes (0 if no packet)
     */
    size_t readPacket(uint8_t *buf, size_t maxLen, int &rssi, float &snr);

    bool isInitialized() const { return _initialized; }

private:
    bool _initialized;
    int _cs, _irq, _rst, _busy;
    SPIClass *_spi;
    LoRaConfig _config;
};

extern LoRaStream loraStream;

#endif // LORA_STREAM_H
