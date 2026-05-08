#include "RM3100.h"

// Register Addresses
#define RM3100_REG_POLL    0x00
#define RM3100_REG_CMM     0x01
#define RM3100_REG_CCX     0x04
#define RM3100_REG_CCY     0x06
#define RM3100_REG_CCZ     0x08
#define RM3100_REG_TMRC    0x0B
#define RM3100_REG_MX      0x24
#define RM3100_REG_STATUS  0x34
#define RM3100_REG_REVID   0x36

RM3100::RM3100(int csPin, int drdyPin) 
    : _csPin(csPin), _drdyPin(drdyPin), _spi(NULL), _spiSettings(1000000, MSBFIRST, SPI_MODE0) {}

bool RM3100::begin() {
    return begin(SPI);
}

bool RM3100::begin(SPIClass &spi) {
    _spi = &spi;
    pinMode(_csPin, OUTPUT);
    digitalWrite(_csPin, HIGH);
    
    if (_drdyPin != -1) {
        pinMode(_drdyPin, INPUT);
    }

    uint8_t rev = getREVID();
    if (rev != 0x22) {
        return false;
    }
    return true;
}

String RM3100::getStatusString() {
    return "Revision ID: 0x" + String(getREVID(), HEX);
}

void RM3100::setCycleCount(uint16_t x, uint16_t y, uint16_t z) {
    writeReg(RM3100_REG_CCX, (x >> 8) & 0xFF);
    writeReg(RM3100_REG_CCX + 1, x & 0xFF);
    writeReg(RM3100_REG_CCY, (y >> 8) & 0xFF);
    writeReg(RM3100_REG_CCY + 1, y & 0xFF);
    writeReg(RM3100_REG_CCZ, (z >> 8) & 0xFF);
    writeReg(RM3100_REG_CCZ + 1, z & 0xFF);
}

void RM3100::setContinuousMode(bool enable, uint8_t rate) {
    if (enable) {
        writeReg(RM3100_REG_TMRC, rate);
        writeReg(RM3100_REG_CMM, 0x79); // Alarm off, X,Y,Z enabled, Continuous on
    } else {
        writeReg(RM3100_REG_CMM, 0x00);
    }
}

bool RM3100::dataReady() {
    if (_drdyPin != -1) {
        return digitalRead(_drdyPin) == HIGH;
    }
    return (readReg(RM3100_REG_STATUS) & 0x80) != 0;
}

void RM3100::readXYZ(int32_t &x, int32_t &y, int32_t &z) {
    uint8_t buffer[9];
    readRegs(RM3100_REG_MX, buffer, 9);
    
    // 24-bit to 32-bit signed conversion
    x = (int32_t)((buffer[0] << 16) | (buffer[1] << 8) | buffer[2]);
    if (x & 0x800000) x |= 0xFF000000;
    
    y = (int32_t)((buffer[3] << 16) | (buffer[4] << 8) | buffer[5]);
    if (y & 0x800000) y |= 0xFF000000;
    
    z = (int32_t)((buffer[6] << 16) | (buffer[7] << 8) | buffer[8]);
    if (z & 0x800000) z |= 0xFF000000;
}

uint8_t RM3100::getREVID() {
    return readReg(RM3100_REG_REVID);
}

void RM3100::writeReg(uint8_t reg, uint8_t val) {
    _spi->beginTransaction(_spiSettings);
    digitalWrite(_csPin, LOW);
    _spi->transfer(reg & 0x7F);
    _spi->transfer(val);
    digitalWrite(_csPin, HIGH);
    _spi->endTransaction();
}

uint8_t RM3100::readReg(uint8_t reg) {
    uint8_t val;
    _spi->beginTransaction(_spiSettings);
    digitalWrite(_csPin, LOW);
    _spi->transfer(reg | 0x80);
    val = _spi->transfer(0x00);
    digitalWrite(_csPin, HIGH);
    _spi->endTransaction();
    return val;
}

void RM3100::readRegs(uint8_t reg, uint8_t *buffer, uint8_t len) {
    _spi->beginTransaction(_spiSettings);
    digitalWrite(_csPin, LOW);
    _spi->transfer(reg | 0x80);
    for (uint8_t i = 0; i < len; i++) {
        buffer[i] = _spi->transfer(0x00);
    }
    digitalWrite(_csPin, HIGH);
    _spi->endTransaction();
}
