#include "LoRaStream.h"
#include <RadioLib.h>

static SX1262* pRadio = nullptr;
static SPIClass* pLoraSpi = nullptr;

LoRaStream::LoRaStream() : _initialized(false), _cs(-1), _irq(-1), _rst(-1), _busy(-1), _spi(nullptr), _radio(nullptr) {}

bool LoRaStream::begin(int cs, int irq, int rst, int busy, int sck, int miso, int mosi, SPIClass *spiBus, LoRaConfig cfg) {
    if (_initialized && pRadio) {
        return true;
    }

    _cs = cs;
    _irq = irq;
    _rst = rst;
    _busy = busy;
    _config = cfg;

    if (_cs < 0 || _irq < 0) {
        return false;
    }

    // Configure dedicated SPI bus for LoRa if not explicitly provided
    if (spiBus) {
        _spi = spiBus;
    } else if (sck >= 0 && miso >= 0 && mosi >= 0) {
        if (!pLoraSpi) {
            pLoraSpi = new SPIClass(FSPI);
            pLoraSpi->begin(sck, miso, mosi, _cs);
        }
        _spi = pLoraSpi;
    } else {
        _spi = &SPI;
    }

    // Instantiate RadioLib Module for Heltec V4 SX1262
    Module *mod = new Module(_cs, _irq, _rst, _busy, *_spi);
    pRadio = new SX1262(mod);
    _radio = pRadio;

    int state = pRadio->begin(
        _config.frequency,
        _config.bandwidth,
        _config.spreadingFactor,
        _config.codingRate,
        RADIOLIB_SX126X_SYNC_WORD_PRIVATE,
        _config.power,
        _config.preambleLength
    );

    if (state == RADIOLIB_ERR_NONE) {
        // 1. Enable DIO2 as RF switch control for Heltec V3/V4 onboard RF switch
        pRadio->setDio2AsRfSwitch(true);

        // 2. Enable SX1262 Rx Boosted Gain (+2 to 3 dB LNA sensitivity improvement)
        pRadio->setRxBoostedGainMode(true);

        // 3. Set Over-Current Protection to 140 mA for clean +22 dBm PA output
        pRadio->setCurrentLimit(140.0f);

        _initialized = true;
        Serial.printf("[LORA SUCCESS] SX1262 LoRa Active on %.1f MHz AU915 (SF%d, BW %.0f kHz, TX +%d dBm, DIO2 RF-Switch ON, RX Boost ON)\r\n",
                      _config.frequency, _config.spreadingFactor, _config.bandwidth, _config.power);
        return true;
    } else {
        Serial.printf("[LORA ERROR] Failed to initialize SX1262 module (code: %d)\r\n", state);
        _initialized = false;
        return false;
    }
}

bool LoRaStream::transmit(const uint8_t *data, size_t len) {
    if (!_initialized || len == 0 || data == nullptr || !pRadio) return false;
    
    int state = pRadio->transmit((uint8_t*)data, len);
    return (state == RADIOLIB_ERR_NONE);
}

bool LoRaStream::startReceive() {
    if (!_initialized || !pRadio) return false;
    int state = pRadio->startReceive();
    return (state == RADIOLIB_ERR_NONE);
}

size_t LoRaStream::readPacket(uint8_t *buf, size_t maxLen, int &rssi, float &snr) {
    if (!_initialized || buf == nullptr || !pRadio) return 0;

    // Check if DIO1 interrupt or RX packet is available
    if (digitalRead(_irq) == HIGH) {
        size_t len = pRadio->getPacketLength();
        if (len > 0) {
            int state = pRadio->readData(buf, min(len, maxLen));
            if (state == RADIOLIB_ERR_NONE) {
                rssi = (int)pRadio->getRSSI();
                snr = pRadio->getSNR();
                pRadio->startReceive(); // Re-arm receiver continuous mode
                return len;
            }
        }
        pRadio->startReceive();
    }
    return 0;
}

LoRaStream loraStream;
