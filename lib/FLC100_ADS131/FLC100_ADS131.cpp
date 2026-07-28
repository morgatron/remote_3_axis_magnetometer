#include "FLC100_ADS131.h"

FLC100_ADS131::FLC100_ADS131(int csPin, int drdyPin, int resetPin)
    : _csPin(csPin), _drdyPin(drdyPin), _resetPin(resetPin), _spi(NULL),
      _spiSettings(2000000, MSBFIRST, SPI_MODE1) {}

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
    stopContinuous();

    // Read Device ID register
    uint8_t id = readRegister(ADS131_REG_ID);
    if ((id & 0x0F) != 0x02) { // ADS131E08 ID upper nibble varies, lower nibble MUST be 0x02
        return false;
    }

    // Configure ADC registers
    // CONFIG1: 0x96 = 1kSPS, 24-bit (DR[2:0] = 110)
    writeRegister(ADS131_REG_CONFIG1, 0x96);

    // CONFIG2: 0xE0 = Default internal test source controls
    writeRegister(ADS131_REG_CONFIG2, 0xE0);

    // FAULT: Disable fault comparators and lead-off current sources (0x00)
    writeRegister(0x04, 0x00);

    // CONFIG3: Configure reference buffer and single-supply mode
    // Bit 7 (PD_REFBUF): 0 = Power-down internal reference buffer (MUST be 0 when external reference IC drives VREFP pin)
    //                    1 = Enable internal reference buffer
    uint8_t config3 = 0x45; // External VREF mode (PD_REFBUF = 0, SINGLE_SUPPLY = 1, Bit 0 = 1)
    if (!_useExternalRef) {
        config3 = (_vref > 3.0f) ? 0xE5 : 0xC5;
    }
    writeRegister(ADS131_REG_CONFIG3, config3);
    if (!_useExternalRef) {
        delay(150); // Wait for the internal reference to charge the VREFP capacitor
    }

    // Configure Channel Settings
    // Bits [6:4] configure Programmable Gain Amplifier (PGA)
    // 0x10 = Gain 1 (001), 0x20 = Gain 2 (010), 0x30 = Gain 4 (011), 0x40 = Gain 8 (100), 0x50 = Gain 12 (101)
    // Note: 0x00 (000) is RESERVED by TI and must NOT be used!
    uint8_t chSetting = 0x10; // Default Gain 1
    if (_gain == 2) chSetting = 0x20;
    else if (_gain == 4) chSetting = 0x30;
    else if (_gain == 8) chSetting = 0x40;
    else if (_gain == 12) chSetting = 0x50;

    writeRegister(ADS131_REG_CH1SET, chSetting);
    writeRegister(ADS131_REG_CH2SET, chSetting);
    writeRegister(ADS131_REG_CH3SET, chSetting);

    // Power down unused channels (Channels 4 to 8) to reduce noise and current
    // 0x81 = Channel power-down, input shorted internally
    for (uint8_t reg = ADS131_REG_CH4SET; reg <= ADS131_REG_CH8SET; reg++) {
        writeRegister(reg, 0x81);
    }

    return true;
}

extern volatile bool drdyInterruptFlag;

bool FLC100_ADS131::dataReady() {
    // Check hardware interrupt flag set on /DRDY falling edge
    if (drdyInterruptFlag) {
        drdyInterruptFlag = false;
        return true;
    }
    return false;
}

void FLC100_ADS131::readXYZ(int32_t &x, int32_t &y, int32_t &z) {
    uint32_t dummyStatus;
    readXYZ(x, y, z, dummyStatus);
}

void FLC100_ADS131::readXYZ(int32_t &x, int32_t &y, int32_t &z, uint32_t &status) {
    uint8_t buffer[27];

    // Wrap SPI transfer in critical section to prevent background ESP32 interrupts mid-frame
    noInterrupts();
    _spi->beginTransaction(_spiSettings); // 4 MHz SPI frequency
    digitalWrite(_csPin, LOW);

    for (int i = 0; i < 27; i++) {
        buffer[i] = _spi->transfer(0x00);
    }

    digitalWrite(_csPin, HIGH);
    delayMicroseconds(2); // Guarantee TI t_CSH High-time (>1 us) for SPI shift register reset
    _spi->endTransaction();
    interrupts();

    status = ((uint32_t)buffer[0] << 16) | ((uint32_t)buffer[1] << 8) | (uint32_t)buffer[2];

    extern volatile uint32_t lastDrdyIntervalUs;
    extern volatile uint32_t drdyAnomalyCount;

    // Check for status byte header corruption (Status MUST be 0xC00000)
    bool isValidHeader = (buffer[0] == 0xC0 && buffer[1] == 0x00 && buffer[2] == 0x00);

    if (!isValidHeader) {
        char diag[160];
        snprintf(diag, sizeof(diag), "[SPI BUS CORRUPTION] Status=%02X %02X %02X | RawBytes: %02X %02X %02X %02X %02X %02X %02X %02X %02X",
                 buffer[0], buffer[1], buffer[2],
                 buffer[3], buffer[4], buffer[5],
                 buffer[6], buffer[7], buffer[8],
                 buffer[9], buffer[10], buffer[11]);
        Serial.println(diag);
    } else if (lastDrdyIntervalUs > 1500) {
        char diag[100];
        snprintf(diag, sizeof(diag), "[CPU TIMING LATENCY] dt=%lu us (Valid C00000 Header)", (unsigned long)lastDrdyIntervalUs);
        Serial.println(diag);
    }

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
    double scale = ((double)_vref * 1000000.0) / ((double)_gain * 8388608.0 * (double)_sensitivity);

    // 100% pure raw output without any filtering, clamping, or sample holding
    x = (int32_t)(rawX * scale);
    y = (int32_t)(rawY * scale);
    z = (int32_t)(rawZ * scale);
}

void FLC100_ADS131::setContinuousMode(bool enable, uint8_t rate_code) {
    stopContinuous();

    if (enable) {
        // ALWAYS keep ADS131E08 hardware rate locked at 1 kSPS (0x96)
        // 0x96 = 1 kSPS (24-bit resolution, DR[2:0] = 110)
        // Software decimation handles 10 Hz to 1000 Hz target streaming rates
        writeRegister(ADS131_REG_CONFIG1, 0x96);

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
    stopContinuous();

    String s = "";
    s += "Device ID: 0x" + String(readRegister(ADS131_REG_ID), HEX) + "\r\n";
    s += "CONFIG1: 0x" + String(readRegister(ADS131_REG_CONFIG1), HEX) + "\r\n";
    s += "CONFIG2: 0x" + String(readRegister(ADS131_REG_CONFIG2), HEX) + "\r\n";
    s += "CONFIG3: 0x" + String(readRegister(ADS131_REG_CONFIG3), HEX) + "\r\n";
    s += "CH1SET:  0x" + String(readRegister(ADS131_REG_CH1SET), HEX) + "\r\n";
    s += "CH2SET:  0x" + String(readRegister(ADS131_REG_CH2SET), HEX) + "\r\n";
    s += "CH3SET:  0x" + String(readRegister(ADS131_REG_CH3SET), HEX) + "\r\n";
    s += "CH4SET:  0x" + String(readRegister(ADS131_REG_CH4SET), HEX) + "\r\n";
    s += "CH5SET:  0x" + String(readRegister(ADS131_REG_CH5SET), HEX) + "\r\n";
    s += "VREF:    " + String(_vref, 2) + "V\r\n";
    s += "Gain:    " + String(_gain) + "\r\n";

    sendCommand(ADS131_CMD_RDATAC);
    delayMicroseconds(10);

    // Wait for DRDY to go low to capture a fresh, synchronized sample (up to 10ms timeout)
    uint32_t startWait = millis();
    while (digitalRead(_drdyPin) == HIGH && (millis() - startWait) < 10) {
        delayMicroseconds(10);
    }

    // Read one sample to capture the raw SPI bytes
    _spi->beginTransaction(_spiSettings);
    digitalWrite(_csPin, LOW);
    uint8_t buffer[27];
    for (int i = 0; i < 27; i++) {
        buffer[i] = _spi->transfer(0x00);
    }
    digitalWrite(_csPin, HIGH);
    _spi->endTransaction();

    s += "Raw SPI Bytes: ";
    for (int i = 0; i < 12; i++) {
        char buf[8];
        sprintf(buf, "%02X ", buffer[i]);
        s += buf;
    }
    s += "\r\n";

    int32_t rawX = (int32_t)((buffer[3] << 16) | (buffer[4] << 8) | buffer[5]);
    if (rawX & 0x800000) rawX |= 0xFF000000;
    int32_t rawY = (int32_t)((buffer[6] << 16) | (buffer[7] << 8) | buffer[8]);
    if (rawY & 0x800000) rawY |= 0xFF000000;
    int32_t rawZ = (int32_t)((buffer[9] << 16) | (buffer[10] << 8) | buffer[11]);
    if (rawZ & 0x800000) rawZ |= 0xFF000000;

    s += "CH1 Raw Count: " + String(rawX) + "\r\n";
    s += "CH2 Raw Count: " + String(rawY) + "\r\n";
    s += "CH3 Raw Count: " + String(rawZ) + "\r\n";

    return s;
}

void FLC100_ADS131::setTestSignal(bool enable) {
    if (!_spi) return;
    
    stopContinuous();

    if (enable) {
        // CONFIG2: Enable internal test source (0xF0 enables internal, 1Hz square wave)
        writeRegister(ADS131_REG_CONFIG2, 0xF0);
        // Set MUX on channels 1-3 to internal test signal (0x05)
        writeRegister(ADS131_REG_CH1SET, 0x05);
        writeRegister(ADS131_REG_CH2SET, 0x05);
        writeRegister(ADS131_REG_CH3SET, 0x05);
    } else {
        // CONFIG2: Disable internal test source (0xE0)
        writeRegister(ADS131_REG_CONFIG2, 0xE0);
        // Set MUX on channels 1-3 back to normal inputs with Gain 1 (0x10)
        writeRegister(ADS131_REG_CH1SET, 0x10);
        writeRegister(ADS131_REG_CH2SET, 0x10);
        writeRegister(ADS131_REG_CH3SET, 0x10);
    }

    sendCommand(ADS131_CMD_RDATAC);
    delayMicroseconds(10);
}

void FLC100_ADS131::setCalibration(float vref_v, float sensitivity_uv_nt, uint8_t gain) {
    _vref = vref_v;
    _sensitivity = sensitivity_uv_nt;
    _gain = gain;

    if (_spi) {
        stopContinuous();

        uint8_t config3 = _useExternalRef ? 0x45 : ((_vref > 3.0f) ? 0xE5 : 0xC5);
        writeRegister(ADS131_REG_CONFIG3, config3);

        uint8_t chSetting = 0x10; // Default Gain 1 (001)
        if (_gain == 2) chSetting = 0x20;
        else if (_gain == 4) chSetting = 0x30;
        else if (_gain == 8) chSetting = 0x40;
        else if (_gain == 12) chSetting = 0x50;

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

void FLC100_ADS131::stopContinuous() {
    // Wait for DRDY to go low to synchronize with conversion cycles
    uint32_t startWait = millis();
    while (digitalRead(_drdyPin) == HIGH && (millis() - startWait) < 10) {
        delayMicroseconds(10);
    }
    // Immediately send SDATAC to stop continuous read
    sendCommand(ADS131_CMD_SDATAC);
    delayMicroseconds(100); // Give the device time to register the SDATAC command
}

void FLC100_ADS131::setExternalReference(bool external) {
    _useExternalRef = external;
    uint8_t config3 = external ? 0x45 : ((_vref > 3.0f) ? 0xE5 : 0xC5);
    writeRegister(ADS131_REG_CONFIG3, config3);
}
