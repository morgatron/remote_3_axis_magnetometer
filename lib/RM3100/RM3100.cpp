#include "RM3100.h"

// Register Addresses
#define RM3100_REG_POLL    0x00
#define RM3100_REG_CMM     0x01
#define RM3100_REG_CCX     0x04
#define RM3100_REG_CCY     0x06
#define RM3100_REG_CCZ     0x08
#define RM3100_REG_TMRC    0x0B
#define RM3100_REG_MX      0x24
#define RM3100_REG_BIST    0x33
#define RM3100_REG_STATUS  0x34
#define RM3100_REG_REVID   0x36

RM3100::RM3100(int csPin, int drdyPin) 
    : _csPin(csPin), _drdyPin(drdyPin), _spi(NULL), _spiSettings(1000000, MSBFIRST, SPI_MODE0),
      _cycleX(200), _cycleY(200), _cycleZ(200) {}

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
    Serial.print("RM3100 getREVID() readback: 0x");
    if (rev < 0x10) Serial.print("0");
    Serial.println(rev, HEX);
    
    if (rev != 0x22) {
        // RM3100 ASIC REVID register (0x36) MUST equal 0x22
        return false;
    }
    return true;
}

uint8_t RM3100::runBIST() {
    writeReg(RM3100_REG_BIST, 0x8F);
    delay(10);
    uint8_t res = readReg(RM3100_REG_BIST);
    writeReg(RM3100_REG_BIST, 0x00); // Clear BIST mode to resume normal measurement
    delay(5);
    return res;
}

String RM3100::getStatusString() {
    uint8_t rev = getREVID();
    uint8_t ccx1 = readReg(RM3100_REG_CCX);      // 0x04 MSB
    uint8_t ccx0 = readReg(RM3100_REG_CCX + 1);  // 0x05 LSB
    uint8_t ccy1 = readReg(RM3100_REG_CCY);
    uint8_t ccy0 = readReg(RM3100_REG_CCY + 1);
    uint8_t ccz1 = readReg(RM3100_REG_CCZ);
    uint8_t ccz0 = readReg(RM3100_REG_CCZ + 1);
    uint8_t tmrc = readReg(RM3100_REG_TMRC);
    uint8_t status = readReg(RM3100_REG_STATUS);
    uint8_t bist = runBIST();

    uint8_t raw[9];
    readRegs(RM3100_REG_MX, raw, 9);

    uint16_t ccx = (ccx1 << 8) | ccx0;
    uint16_t ccy = (ccy1 << 8) | ccy0;
    uint16_t ccz = (ccz1 << 8) | ccz0;

    String s = "RM3100 Register Status:\r\n";
    s += "  RevID: 0x" + String(rev, HEX) + "\r\n";
    s += "  TMRC: 0x" + String(tmrc, HEX) + "\r\n";
    s += "  CCX: " + String(ccx) + " [MSB:0x" + String(ccx1, HEX) + " LSB:0x" + String(ccx0, HEX) + "]\r\n";
    s += "  CCY: " + String(ccy) + " [MSB:0x" + String(ccy1, HEX) + " LSB:0x" + String(ccy0, HEX) + "]\r\n";
    s += "  CCZ: " + String(ccz) + " [MSB:0x" + String(ccz1, HEX) + " LSB:0x" + String(ccz0, HEX) + "]\r\n";
    s += "  STATUS: 0x" + String(status, HEX) + "\r\n";
    s += "  BIST: 0x" + String(bist, HEX) + " (XOK=" + String((bist & 0x10) ? "PASS" : "FAIL") + " YOK=" + String((bist & 0x20) ? "PASS" : "FAIL") + " ZOK=" + String((bist & 0x40) ? "PASS" : "FAIL") + ")\r\n";
    s += "  RAW BYTES: ";
    for (int i = 0; i < 9; i++) {
        if (raw[i] < 0x10) s += "0";
        s += String(raw[i], HEX) + " ";
    }
    return s;
}

void RM3100::setCycleCount(uint16_t x, uint16_t y, uint16_t z) {
    _cycleX = x;
    _cycleY = y;
    _cycleZ = z;

    uint8_t cmm = readReg(RM3100_REG_CMM);
    bool cmmActive = (cmm & 0x01) != 0;

    if (cmmActive) {
        // Stop CMM first so the ASIC accepts cycle count writes
        writeReg(RM3100_REG_CMM, 0x00);
        delay(20);
    }

    uint8_t ccData[6];
    ccData[0] = (_cycleX >> 8) & 0xFF; // X MSB
    ccData[1] = _cycleX & 0xFF;        // X LSB
    ccData[2] = (_cycleY >> 8) & 0xFF; // Y MSB
    ccData[3] = _cycleY & 0xFF;        // Y LSB
    ccData[4] = (_cycleZ >> 8) & 0xFF; // Z MSB
    ccData[5] = _cycleZ & 0xFF;        // Z LSB

    // Burst write 6 bytes starting at address 0x04 in a single CS transaction (Section 5.7.1)
    _spi->beginTransaction(_spiSettings);
    digitalWrite(_csPin, LOW);
    _spi->transfer(RM3100_REG_CCX & 0x7F);
    for (int i = 0; i < 6; i++) {
        _spi->transfer(ccData[i]);
    }
    digitalWrite(_csPin, HIGH);
    _spi->endTransaction();
    delay(5);

    if (cmmActive) {
        // Restart CMM mode cleanly
        uint8_t tmrc = readReg(RM3100_REG_TMRC);
        if ((tmrc & 0xF0) == 0x00) tmrc |= 0x90;
        writeReg(RM3100_REG_TMRC, tmrc);
        writeReg(RM3100_REG_CMM, 0x79);
        delay(5);
    }
}

void RM3100::setContinuousMode(bool enable, uint8_t rate) {
    if (enable) {
        uint8_t tmrcVal = rate;
        if ((tmrcVal & 0xF0) == 0x00) {
            tmrcVal |= 0x90; // Convert 0x05 -> 0x95, 0x02 -> 0x92, 0x03 -> 0x93, 0x04 -> 0x94, etc.
        }

        // Cap cycle counts for high sample rates to prevent ASIC measurement duration from exceeding rate interval
        uint16_t ccX = _cycleX, ccY = _cycleY, ccZ = _cycleZ;
        if (tmrcVal == 0x92 && ccX > 30)  { ccX = 30;  ccY = 30;  ccZ = 30; }  // 600 Hz limit (cc <= 30)
        if (tmrcVal == 0x93 && ccX > 50)  { ccX = 50;  ccY = 50;  ccZ = 50; }  // 300 Hz limit (cc <= 50)
        if (tmrcVal == 0x94 && ccX > 100) { ccX = 100; ccY = 100; ccZ = 100; } // 150 Hz limit (cc <= 100)

        writeReg(RM3100_REG_CCX, (ccX >> 8) & 0xFF);
        writeReg(RM3100_REG_CCX + 1, ccX & 0xFF);
        writeReg(RM3100_REG_CCY, (ccY >> 8) & 0xFF);
        writeReg(RM3100_REG_CCY + 1, ccY & 0xFF);
        writeReg(RM3100_REG_CCZ, (ccZ >> 8) & 0xFF);
        writeReg(RM3100_REG_CCZ + 1, ccZ & 0xFF);

        writeReg(RM3100_REG_TMRC, tmrcVal);
        writeReg(RM3100_REG_CMM, 0x79); // Alarm off, X,Y,Z enabled, Continuous ON
    } else {
        writeReg(RM3100_REG_CMM, 0x00);
    }
}

bool RM3100::dataReady() {
    return !isBufferEmpty();
}

bool RM3100::isBufferEmpty() const {
    return _ringHead == _ringTail;
}

void RM3100::readAndPushSample() {
    if (!_spi) return;

    uint8_t buffer[9];
    uint64_t now = esp_timer_get_time();

    readRegs(RM3100_REG_MX, buffer, 9);

    int32_t x = (int32_t)(((uint32_t)buffer[0] << 16) | ((uint32_t)buffer[1] << 8) | buffer[2]);
    if (x & 0x800000) x |= 0xFF000000;

    int32_t y = (int32_t)(((uint32_t)buffer[3] << 16) | ((uint32_t)buffer[4] << 8) | buffer[5]);
    if (y & 0x800000) y |= 0xFF000000;

    int32_t z = (int32_t)(((uint32_t)buffer[6] << 16) | ((uint32_t)buffer[7] << 8) | buffer[8]);
    if (z & 0x800000) z |= 0xFF000000;

    _lastValidX = x;
    _lastValidY = y;
    _lastValidZ = z;

    uint32_t status = 0xC00000;

    size_t nextHead = (_ringHead + 1) & (RING_BUFFER_SIZE - 1);
    if (nextHead != _ringTail) {
        _ringBuffer[_ringHead] = { now, x, y, z, status };
        _ringHead = nextHead;
    }
}

bool RM3100::popSample(ADCSample &sample) {
    if (_ringHead == _ringTail) {
        return false;
    }
    sample = _ringBuffer[_ringTail];
    _ringTail = (_ringTail + 1) & (RING_BUFFER_SIZE - 1);
    return true;
}

void RM3100::readXYZ(int32_t &x, int32_t &y, int32_t &z) {
    ADCSample sample;
    if (popSample(sample)) {
        x = sample.x;
        y = sample.y;
        z = sample.z;
    } else {
        x = _lastValidX;
        y = _lastValidY;
        z = _lastValidZ;
    }
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
