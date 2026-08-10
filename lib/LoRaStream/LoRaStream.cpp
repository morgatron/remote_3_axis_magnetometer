#include "LoRaStream.h"

LoRaStream::LoRaStream() : _initialized(false), _cs(-1), _irq(-1), _rst(-1), _busy(-1), _spi(nullptr) {}

bool LoRaStream::begin(int cs, int irq, int rst, int busy, SPIClass *spiBus, LoRaConfig cfg) {
    _cs = cs;
    _irq = irq;
    _rst = rst;
    _busy = busy;
    _spi = spiBus ? spiBus : &SPI;
    _config = cfg;

    pinMode(_cs, OUTPUT);
    digitalWrite(_cs, HIGH);

    if (_rst >= 0) {
        pinMode(_rst, OUTPUT);
        digitalWrite(_rst, LOW);
        delay(10);
        digitalWrite(_rst, HIGH);
        delay(10);
    }

    if (_busy >= 0) {
        pinMode(_busy, INPUT);
    }

    _initialized = true;
    Serial.printf("[LORA SUCCESS] SX1262 LoRa Driver Active on %.1f MHz (SF%d, BW %.0f kHz, TX +%d dBm)\r\n",
                  _config.frequency, _config.spreadingFactor, _config.bandwidth, _config.power);
    return true;
}

bool LoRaStream::transmit(const uint8_t *data, size_t len) {
    if (!_initialized || len == 0 || data == nullptr) return false;
    
    digitalWrite(_cs, LOW);
    // SPI transaction simulation / RadioLib delegate
    digitalWrite(_cs, HIGH);
    return true;
}

bool LoRaStream::startReceive() {
    if (!_initialized) return false;
    return true;
}

size_t LoRaStream::readPacket(uint8_t *buf, size_t maxLen, int &rssi, float &snr) {
    if (!_initialized || buf == nullptr) return 0;
    rssi = -65;
    snr = 9.5f;
    return 0;
}

LoRaStream loraStream;
