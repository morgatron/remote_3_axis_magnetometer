#include "FLC100_ADS131.h"

FLC100_ADS131::FLC100_ADS131(int csPin, int drdyPin, int resetPin)
    : _csPin(csPin), _drdyPin(drdyPin), _resetPin(resetPin), _spi(NULL),
      _spiSettings(1000000, MSBFIRST, SPI_MODE1) {}

bool FLC100_ADS131::begin() {
    return begin(SPI);
}

bool FLC100_ADS131::begin(SPIClass &spi) {
    _spi = &spi;

    pinMode(_csPin, OUTPUT);
    digitalWrite(_csPin, HIGH);

    pinMode(_drdyPin, INPUT);

    if (_resetPin != -1) {
        pinMode(_resetPin, OUTPUT);
        digitalWrite(_resetPin, LOW);
        delayMicroseconds(10);
        digitalWrite(_resetPin, HIGH);
        delay(150); // Wait for power-up and VREF to stabilize
    }

    // Software reset fallback/reinforcement
    sendCommand(ADS131_CMD_RESET);
    delay(150); // Wait for reset to execute and registers to clear

    // Stop continuous read mode to allow writing to registers
    sendCommand(ADS131_CMD_SDATAC);
    delayMicroseconds(10);

    // Verify SPI communication by reading the Device ID register
    uint8_t id = readRegister(ADS131_REG_ID);
    if (id == 0x00 || id == 0xFF) {
        // SPI communication failed (no response)
        return false;
    }

    // Configure ADC registers
    // CONFIG1: 0x96 = 1kSPS, 24-bit (DR[2:0] = 110)
    writeRegister(ADS131_REG_CONFIG1, 0x96);

    // CONFIG2: 0xE0 = Default internal test source controls
    writeRegister(ADS131_REG_CONFIG2, 0xE0);

    // CONFIG3: Enable internal reference buffer
    // 0xE0 enables VREF = 4.0V (requires AVDD >= 4.3V)
    // 0xC0 enables VREF = 2.4V (standard for 3.3V systems)
    writeRegister(ADS131_REG_CONFIG3, _vref > 3.0f ? 0xE0 : 0xC0);
    delay(150); // Wait for the internal reference to charge the VREFP capacitor

    // Configure Channel Settings
    // Bits [6:4] configure Programmable Gain Amplifier (PGA)
    // 0x00 = Gain 1, 0x10 = Gain 2, 0x20 = Gain 4, 0x30 = Gain 8, 0x40 = Gain 12
    uint8_t chSetting = 0x00;
    if (_gain == 2) chSetting = 0x10;
    else if (_gain == 4) chSetting = 0x20;
    else if (_gain == 8) chSetting = 0x30;
    else if (_gain == 12) chSetting = 0x40;

    writeRegister(ADS131_REG_CH1SET, chSetting);
    writeRegister(ADS131_REG_CH2SET, chSetting);
    writeRegister(ADS131_REG_CH3SET, chSetting);

    // Power down unused channels (Channels 4 to 8) to reduce noise and current
    // 0x81 = Channel power-down, input shorted internally
    for (uint8_t reg = ADS131_REG_CH4SET; reg <= ADS131_REG_CH8SET; reg++) {
        writeRegister(reg, 0x81);
    }

    // Start conversion
    sendCommand(ADS131_CMD_START);
    delayMicroseconds(10);

    // Enter read data continuous (RDATAC) mode
    sendCommand(ADS131_CMD_RDATAC);
    delayMicroseconds(10);

    return true;
}

bool FLC100_ADS131::dataReady() {
    // DRDY pin transitions LOW when conversion data is available
    return digitalRead(_drdyPin) == LOW;
}

void FLC100_ADS131::readXYZ(int32_t &x, int32_t &y, int32_t &z) {
    _spi->beginTransaction(_spiSettings);
    digitalWrite(_csPin, LOW);

    // Read the first 12 bytes: 3 bytes Status + 3 channels * 3 bytes data
    // Terminate transaction early by pulling CS HIGH (standard ADS131 optimization)
    uint8_t buffer[12];
    for (int i = 0; i < 12; i++) {
        buffer[i] = _spi->transfer(0x00);
    }

    digitalWrite(_csPin, HIGH);
    _spi->endTransaction();

    // Channel 1: X-Axis
    int32_t rawX = (int32_t)((buffer[3] << 16) | (buffer[4] << 8) | buffer[5]);
    if (rawX & 0x800000) rawX |= 0xFF000000; // Sign-extend 24-bit to 32-bit signed

    // Channel 2: Y-Axis
    int32_t rawY = (int32_t)((buffer[6] << 16) | (buffer[7] << 8) | buffer[8]);
    if (rawY & 0x800000) rawY |= 0xFF000000;

    // Channel 3: Z-Axis
    int32_t rawZ = (int32_t)((buffer[9] << 16) | (buffer[10] << 8) | buffer[11]);
    if (rawZ & 0x800000) rawZ |= 0xFF000000;

    // Convert raw ADC counts to physical magnetic field values in nanoTesla (nT)
    // Formula: Field (nT) = raw_code * ScaleFactor
    // ScaleFactor = VREF * 10^6 / (Gain * 2^23 * Sensitivity_uV_nT)
    double scale = ((double)_vref * 1000000.0) / ((double)_gain * 8388608.0 * (double)_sensitivity);

    x = (int32_t)(rawX * scale);
    y = (int32_t)(rawY * scale);
    z = (int32_t)(rawZ * scale);
}

void FLC100_ADS131::setContinuousMode(bool enable, uint8_t rate_code) {
    sendCommand(ADS131_CMD_SDATAC);
    delayMicroseconds(10);

    if (enable) {
        // rate_code bits [2:0] map directly to DR[2:0] configuration bits
        uint8_t config1Val = 0x90 | (rate_code & 0x07);
        writeRegister(ADS131_REG_CONFIG1, config1Val);

        sendCommand(ADS131_CMD_START);
        delayMicroseconds(10);
        sendCommand(ADS131_CMD_RDATAC);
        delayMicroseconds(10);
    } else {
        sendCommand(ADS131_CMD_STOP);
        delayMicroseconds(10);
    }
}

String FLC100_ADS131::getStatusString() {
    sendCommand(ADS131_CMD_SDATAC);
    delayMicroseconds(10);

    uint8_t id = readRegister(ADS131_REG_ID);
    uint8_t conf1 = readRegister(ADS131_REG_CONFIG1);
    uint8_t conf3 = readRegister(ADS131_REG_CONFIG3);

    sendCommand(ADS131_CMD_RDATAC);
    delayMicroseconds(10);

    String s = "Device ID: 0x" + String(id, HEX);
    s += " | CONFIG1: 0x" + String(conf1, HEX);
    s += " | CONFIG3: 0x" + String(conf3, HEX);
    s += " | VREF: " + String(_vref, 2) + "V";
    s += " | Gain: " + String(_gain);
    return s;
}

void FLC100_ADS131::setCalibration(float vref_v, float sensitivity_uv_nt, uint8_t gain) {
    _vref = vref_v;
    _sensitivity = sensitivity_uv_nt;
    _gain = gain;

    if (_spi) {
        sendCommand(ADS131_CMD_SDATAC);
        delayMicroseconds(10);

        writeRegister(ADS131_REG_CONFIG3, _vref > 3.0f ? 0xE0 : 0xC0);

        uint8_t chSetting = 0x00;
        if (_gain == 2) chSetting = 0x10;
        else if (_gain == 4) chSetting = 0x20;
        else if (_gain == 8) chSetting = 0x30;
        else if (_gain == 12) chSetting = 0x40;

        writeRegister(ADS131_REG_CH1SET, chSetting);
        writeRegister(ADS131_REG_CH2SET, chSetting);
        writeRegister(ADS131_REG_CH3SET, chSetting);

        sendCommand(ADS131_CMD_RDATAC);
        delayMicroseconds(10);
    }
}

void FLC100_ADS131::sendCommand(uint8_t cmd) {
    _spi->beginTransaction(_spiSettings);
    digitalWrite(_csPin, LOW);
    _spi->transfer(cmd);
    digitalWrite(_csPin, HIGH);
    _spi->endTransaction();
}

void FLC100_ADS131::writeRegister(uint8_t reg, uint8_t val) {
    _spi->beginTransaction(_spiSettings);
    digitalWrite(_csPin, LOW);
    _spi->transfer(ADS131_CMD_WREG | reg);
    _spi->transfer(0x00); // 1 register (num_regs - 1 = 0)
    _spi->transfer(val);
    digitalWrite(_csPin, HIGH);
    _spi->endTransaction();
}

uint8_t FLC100_ADS131::readRegister(uint8_t reg) {
    _spi->beginTransaction(_spiSettings);
    digitalWrite(_csPin, LOW);
    _spi->transfer(ADS131_CMD_RREG | reg);
    _spi->transfer(0x00); // 1 register (num_regs - 1 = 0)
    uint8_t val = _spi->transfer(0x00);
    digitalWrite(_csPin, HIGH);
    _spi->endTransaction();
    return val;
}
