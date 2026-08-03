#ifndef FLC100_ADS131_H
#define FLC100_ADS131_H

#include <Arduino.h>
#include <SPI.h>
#include "Magnetometer.h"

// ADS131E08 SPI Commands
#define ADS131_CMD_WAKEUP   0x02
#define ADS131_CMD_STANDBY  0x04
#define ADS131_CMD_RESET    0x06
#define ADS131_CMD_START    0x08
#define ADS131_CMD_STOP     0x0A
#define ADS131_CMD_RDATAC   0x10
#define ADS131_CMD_SDATAC   0x11
#define ADS131_CMD_RDATA    0x12
#define ADS131_CMD_RREG     0x20
#define ADS131_CMD_WREG     0x40

// ADS131E08 Register Map
#define ADS131_REG_ID          0x00
#define ADS131_REG_CONFIG1     0x01
#define ADS131_REG_CONFIG2     0x02
#define ADS131_REG_CONFIG3     0x03
#define ADS131_REG_FAULT       0x04
#define ADS131_REG_CH1SET      0x05
#define ADS131_REG_CH2SET      0x06
#define ADS131_REG_CH3SET      0x07
#define ADS131_REG_CH4SET      0x08
#define ADS131_REG_CH5SET      0x09
#define ADS131_REG_CH6SET      0x0A
#define ADS131_REG_CH7SET      0x0B
#define ADS131_REG_CH8SET      0x0C

struct ADCSample {
    uint64_t ts;
    int32_t x;
    int32_t y;
    int32_t z;
    uint32_t status;
};

/**
 * @brief Magnetometer implementation for 3-axis FLC-100 sensor read via an external 24-bit ADS131E08 ADC.
 * 
 * Connection schematic:
 *   - FLC-100 X Out -> ADS131E08 Channel 1 Positive (IN1P)
 *   - FLC-100 Y Out -> ADS131E08 Channel 2 Positive (IN2P)
 *   - FLC-100 Z Out -> ADS131E08 Channel 3 Positive (IN3P)
 *   - FLC-100 Ref Out -> ADS131E08 Channels 1-3 Negative (IN1N, IN2N, IN3N)
 * 
 * Using differential inputs eliminates the 2.5V common mode offset and avoids voltage dividers.
 */
class FLC100_ADS131 : public Magnetometer {
public:
    FLC100_ADS131(int csPin, int drdyPin, int resetPin = -1);

    // Magnetometer interface
    bool begin() override;
    bool begin(SPIClass &spi);
    bool dataReady() override;
    void readXYZ(int32_t &x, int32_t &y, int32_t &z) override;
    void readXYZ(int32_t &x, int32_t &y, int32_t &z, uint32_t &status) override;
    void setContinuousMode(bool enable, uint8_t rate_code = 0x06) override; // Default rate_code: 0x06 (1kSPS)
    String getStatusString() override;
    String getSensorName() override { return "FLC100-ADS131E08"; }

    // Configuration / Calibration
    void setCalibration(float vref_v, float sensitivity_uv_nt, uint8_t gain = 1);
    void setExternalReference(bool external);
    uint8_t readRegister(uint8_t reg);
    void writeRegister(uint8_t reg, uint8_t val);
    void setTestSignal(bool enable);

    // High-priority task & Ring Buffer API
    void readAndPushSample();
    bool popSample(ADCSample &sample);
    bool isBufferEmpty() const;

private:
    int _csPin;
    int _drdyPin;
    int _resetPin;
    SPIClass *_spi;
    SPISettings _spiSettings;

    float _vref = 2.4f;             // Default VREF = 2.4V
    float _sensitivity = 20.0f;     // FLC-100 sensitivity: 20 uV / nT (0.02 mV/nT)
    uint8_t _gain = 1;              // PGA Gain: 1, 2, 4, 8, 12 etc (default 1)
    bool _useExternalRef = true;    // True when external VREF reference IC is present on PCB

    // Lock-free ring buffer for ISR -> main loop sample transfer
    static const size_t RING_BUFFER_SIZE = 128; // Power of 2 for fast modulo masking
    ADCSample _ringBuffer[RING_BUFFER_SIZE];
    volatile size_t _ringHead = 0;
    volatile size_t _ringTail = 0;

    int32_t _lastValidX = 0;
    int32_t _lastValidY = 0;
    int32_t _lastValidZ = 0;
    uint32_t _lastValidStatus = 0xC00000;

    void sendCommand(uint8_t cmd);
    void stopContinuous();
};

#endif
