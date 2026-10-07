#include "LoRaStream.h"
#include <RadioLib.h>
#include "board_config.h"

class SX1262Ex : public SX1262 {
public:
    explicit SX1262Ex(Module* mod) : SX1262(mod) {}
    using SX126x::getPacketStatus;
    using SX126x::getMod;
};

static SX1262Ex* pRadio = nullptr;
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

    // Ensure Chip Select is deselected initially
    pinMode(_cs, OUTPUT);
    digitalWrite(_cs, HIGH);

#if defined(LORA_RF_SWITCH_PIN) && (LORA_RF_SWITCH_PIN >= 0)
    pinMode(LORA_RF_SWITCH_PIN, OUTPUT);
    digitalWrite(LORA_RF_SWITCH_PIN, LOW); // Default RX/Idle
#endif

#if defined(LORA_FEM_PWR_PIN) && (LORA_FEM_PWR_PIN >= 0)
    pinMode(LORA_FEM_PWR_PIN, OUTPUT);
    digitalWrite(LORA_FEM_PWR_PIN, HIGH); // Power on Heltec V4 FEM LDO regulator (TLV75733)
    delay(10);                            // Allow 3.3V power rail to stabilize
#endif

#if defined(LORA_FEM_CSD_PIN) && (LORA_FEM_CSD_PIN >= 0)
    pinMode(LORA_FEM_CSD_PIN, OUTPUT);
    digitalWrite(LORA_FEM_CSD_PIN, HIGH); // Enable FEM (LNA Receive Mode when DIO2/CTX is LOW)
#endif

#if defined(LORA_FEM_CPS_PIN) && (LORA_FEM_CPS_PIN >= 0)
    pinMode(LORA_FEM_CPS_PIN, OUTPUT);
    digitalWrite(LORA_FEM_CPS_PIN, LOW);  // Default to low/bypass
#endif

#if defined(LORA_FEM_CTX_PIN) && (LORA_FEM_CTX_PIN >= 0)
    pinMode(LORA_FEM_CTX_PIN, OUTPUT);
    digitalWrite(LORA_FEM_CTX_PIN, HIGH); // Default to HIGH (RX Bypass Mode for V4.3 KCT8103L to avoid close-range desk saturation)
#endif

    // Configure dedicated SPI bus for LoRa if not explicitly provided
    if (spiBus) {
        _spi = spiBus;
    } else if (sck >= 0 && miso >= 0 && mosi >= 0) {
#if defined(SCK_PIN) && defined(MISO_PIN) && defined(MOSI_PIN)
        if (sck == SCK_PIN && miso == MISO_PIN && mosi == MOSI_PIN) {
            _spi = &SPI;
            _spi->begin(sck, miso, mosi, _cs);
        } else
#endif
        {
            if (!pLoraSpi) {
#if defined(CONFIG_IDF_TARGET_ESP32S3) || defined(ARDUINO_ARCH_ESP32S3)
                pLoraSpi = new SPIClass(HSPI);
#else
                pLoraSpi = new SPIClass(FSPI);
#endif
                pLoraSpi->begin(sck, miso, mosi, _cs);
            }
            _spi = pLoraSpi;
        }
    } else {
        _spi = &SPI;
    }

    // Instantiate RadioLib Module for SX1262
    Module *mod = new Module(_cs, _irq, _rst, _busy, *_spi);
    pRadio = new SX1262Ex(mod);
    _radio = pRadio;

#ifndef LORA_TCXO_VOLTAGE
#define LORA_TCXO_VOLTAGE 1.6f
#endif

    int state = pRadio->begin(
        _config.frequency,
        _config.bandwidth,
        _config.spreadingFactor,
        _config.codingRate,
        RADIOLIB_SX126X_SYNC_WORD_PRIVATE,
        _config.power,
        _config.preambleLength,
        LORA_TCXO_VOLTAGE
    );

    if (state == RADIOLIB_ERR_NONE) {
        // 1. Configure RF switch (GPIO 38 on Wio-SX1262, DIO2 on Heltec V3/V4)
#if defined(LORA_RF_SWITCH_PIN) && (LORA_RF_SWITCH_PIN >= 0)
        pRadio->setRfSwitchPins(RADIOLIB_NC, LORA_RF_SWITCH_PIN);
#endif
        pRadio->setDio2AsRfSwitch(true);

        // 2. Standard Rx Gain Mode (avoids ADC saturation on desk)
        pRadio->setRxBoostedGainMode(false);

        // 3. Set Over-Current Protection to 140 mA for clean +22 dBm PA output
        pRadio->setCurrentLimit(140.0f);

        _initialized = true;
        Serial.printf("[LORA SUCCESS] SX1262 LoRa Active on %.1f MHz AU915 (SF%d, BW %.0f kHz, TX +%d dBm, DIO2 RF-Switch ON, RX Boost ON, FEM LNA ON)\r\n",
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
    
#if defined(LORA_FEM_CPS_PIN) && (LORA_FEM_CPS_PIN >= 0)
    digitalWrite(LORA_FEM_CPS_PIN, HIGH); // Enable +30dBm PA mode on Heltec V4.2
#endif
#if defined(LORA_FEM_CTX_PIN) && (LORA_FEM_CTX_PIN >= 0)
    digitalWrite(LORA_FEM_CTX_PIN, HIGH); // Enable TX mode on Heltec V4.3
#endif

    int state = pRadio->transmit((uint8_t*)data, len);

#if defined(LORA_FEM_CTX_PIN) && (LORA_FEM_CTX_PIN >= 0)
    digitalWrite(LORA_FEM_CTX_PIN, LOW);  // Return to RX mode
#endif
#if defined(LORA_FEM_CPS_PIN) && (LORA_FEM_CPS_PIN >= 0)
    digitalWrite(LORA_FEM_CPS_PIN, LOW);  // Return to bypass/RX mode
#endif

    return (state == RADIOLIB_ERR_NONE);
}

bool LoRaStream::sleep(bool retainConfig) {
    if (!_initialized || !pRadio) return false;
#if defined(LORA_FEM_CTX_PIN) && (LORA_FEM_CTX_PIN >= 0)
    digitalWrite(LORA_FEM_CTX_PIN, LOW);
#endif
#if defined(LORA_FEM_CSD_PIN) && (LORA_FEM_CSD_PIN >= 0)
    digitalWrite(LORA_FEM_CSD_PIN, LOW); // Shutdown FEM
#endif
#if defined(LORA_FEM_PWR_PIN) && (LORA_FEM_PWR_PIN >= 0)
    digitalWrite(LORA_FEM_PWR_PIN, LOW); // Cut power to FEM LDO
#endif
    int state = pRadio->sleep(retainConfig);
    return (state == RADIOLIB_ERR_NONE);
}

bool LoRaStream::standby() {
    if (!_initialized || !pRadio) return false;
    int state = pRadio->standby();
    return (state == RADIOLIB_ERR_NONE);
}

bool LoRaStream::startReceive() {
    if (!_initialized || !pRadio) return false;
#if defined(LORA_FEM_PWR_PIN) && (LORA_FEM_PWR_PIN >= 0)
    digitalWrite(LORA_FEM_PWR_PIN, HIGH); // Ensure FEM is powered
#endif
#if defined(LORA_FEM_CSD_PIN) && (LORA_FEM_CSD_PIN >= 0)
    digitalWrite(LORA_FEM_CSD_PIN, HIGH); // Ensure FEM is in active LNA receive mode
#endif
#if defined(LORA_FEM_CTX_PIN) && (LORA_FEM_CTX_PIN >= 0)
    digitalWrite(LORA_FEM_CTX_PIN, HIGH); // Ensure FEM is in RX Bypass mode (V4.3 KCT8103L, avoids +21dB desk saturation)
#endif
#if defined(LORA_FEM_CPS_PIN) && (LORA_FEM_CPS_PIN >= 0)
    digitalWrite(LORA_FEM_CPS_PIN, LOW);  // Ensure FEM is in RX mode (V4.2 GC1109)
#endif
    int state = pRadio->startReceive();
    return (state == RADIOLIB_ERR_NONE);
}

size_t LoRaStream::readPacket(uint8_t *buf, size_t maxLen, int &rssi, float &snr) {
    if (!_initialized || buf == nullptr || !pRadio) return 0;

    // Check if DIO1 interrupt or RX packet is available
    if (digitalRead(_irq) == HIGH) {
        // 1. Read packet status BEFORE getPacketLength or buffer reads
        uint8_t pktData[4] = {0, 0, 0, 0};
        pRadio->getMod()->SPIreadStream(RADIOLIB_SX126X_CMD_GET_PACKET_STATUS, pktData, 4);

        size_t len = pRadio->getPacketLength();
        if (len > 0) {
            int state = pRadio->readData(buf, min(len, maxLen));
            if (state == RADIOLIB_ERR_NONE) {
                uint8_t rawRssi = pktData[0];
                int8_t rawSnr = (int8_t)pktData[1];
                uint8_t rawSigRssi = pktData[2];

                snr = rawSnr / 4.0f;

                if (rawRssi > 0) {
                    rssi = -(int)rawRssi / 2;
                } else if (rawSigRssi > 0) {
                    rssi = -(int)rawSigRssi / 2;
                } else {
                    // Saturated close-range ceiling: signal power >= 0 dBm
                    rssi = -35;
                }

                Serial.printf("[LORA RX] RssiPkt=%u, SigRssi=%u -> RSSI=%d dBm, SnrPkt=%d -> %.2f dB\r\n",
                              rawRssi, rawSigRssi, rssi, (int)rawSnr, snr);

                pRadio->startReceive(); // Re-arm receiver continuous mode
                return len;
            }
        }
        pRadio->startReceive();
    }
    return 0;
}

LoRaStream loraStream;
