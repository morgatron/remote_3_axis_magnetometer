#ifndef RM3100_H
#define RM3100_H

#include <Arduino.h>
#include <SPI.h>
#include "Magnetometer.h"

class RM3100 : public Magnetometer {
public:
    RM3100(int csPin, int drdyPin = -1);
    
    // Magnetometer interface
    bool begin() override; // Default to SPI
    bool begin(SPIClass &spi);
    bool dataReady() override;
    void readXYZ(int32_t &x, int32_t &y, int32_t &z) override;
    void setContinuousMode(bool enable, uint8_t rate = 0x92) override;
    String getStatusString() override;
    String getSensorName() override { return "RM3100"; }

    // RM3100 specific
    void setCycleCount(uint16_t x, uint16_t y, uint16_t z);
    uint8_t getREVID();
    uint8_t runBIST();

private:
    int _csPin;
    int _drdyPin;
    SPIClass *_spi;
    SPISettings _spiSettings;
    uint16_t _cycleX;
    uint16_t _cycleY;
    uint16_t _cycleZ;

    void writeReg(uint8_t reg, uint8_t val);
    uint8_t readReg(uint8_t reg);
    void readRegs(uint8_t reg, uint8_t *buffer, uint8_t len);
};

#endif
