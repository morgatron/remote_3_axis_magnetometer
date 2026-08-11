#include "OLEDDisplay.h"

#define SSD1306_I2C_ADDR 0x3C

// Basic 5x7 ASCII font table (ASCII 32 to 126)
static const uint8_t font5x7[][5] PROGMEM = {
    {0x00, 0x00, 0x00, 0x00, 0x00}, // 32 ' '
    {0x00, 0x00, 0x5F, 0x00, 0x00}, // 33 '!'
    {0x00, 0x07, 0x00, 0x07, 0x00}, // 34 '"'
    {0x14, 0x7F, 0x14, 0x7F, 0x14}, // 35 '#'
    {0x24, 0x2A, 0x7F, 0x2A, 0x12}, // 36 '$'
    {0x23, 0x13, 0x08, 0x64, 0x62}, // 37 '%'
    {0x36, 0x49, 0x55, 0x22, 0x50}, // 38 '&'
    {0x00, 0x05, 0x03, 0x00, 0x00}, // 39 '\''
    {0x00, 0x1C, 0x22, 0x41, 0x00}, // 40 '('
    {0x00, 0x41, 0x22, 0x1C, 0x00}, // 41 ')'
    {0x14, 0x08, 0x3E, 0x08, 0x14}, // 42 '*'
    {0x08, 0x08, 0x3E, 0x08, 0x08}, // 43 '+'
    {0x00, 0x50, 0x30, 0x00, 0x00}, // 44 ','
    {0x08, 0x08, 0x08, 0x08, 0x08}, // 45 '-'
    {0x00, 0x60, 0x60, 0x00, 0x00}, // 46 '.'
    {0x20, 0x10, 0x08, 0x04, 0x02}, // 47 '/'
    {0x3E, 0x51, 0x49, 0x45, 0x3E}, // 48 '0'
    {0x00, 0x42, 0x7F, 0x40, 0x00}, // 49 '1'
    {0x42, 0x61, 0x51, 0x49, 0x46}, // 50 '2'
    {0x21, 0x41, 0x45, 0x4B, 0x31}, // 51 '3'
    {0x18, 0x14, 0x12, 0x7F, 0x10}, // 52 '4'
    {0x27, 0x45, 0x45, 0x45, 0x39}, // 53 '5'
    {0x3C, 0x4A, 0x49, 0x49, 0x30}, // 54 '6'
    {0x01, 0x71, 0x09, 0x05, 0x03}, // 55 '7'
    {0x36, 0x49, 0x49, 0x49, 0x36}, // 56 '8'
    {0x06, 0x49, 0x49, 0x29, 0x1E}, // 57 '9'
    {0x00, 0x36, 0x36, 0x00, 0x00}, // 58 ':'
    {0x00, 0x56, 0x36, 0x00, 0x00}, // 59 ';'
    {0x08, 0x14, 0x22, 0x41, 0x00}, // 60 '<'
    {0x14, 0x14, 0x14, 0x14, 0x14}, // 61 '='
    {0x00, 0x41, 0x22, 0x14, 0x08}, // 62 '>'
    {0x02, 0x01, 0x51, 0x09, 0x06}, // 63 '?'
    {0x32, 0x49, 0x79, 0x41, 0x3E}, // 64 '@'
    {0x7E, 0x11, 0x11, 0x11, 0x7E}, // 65 'A'
    {0x7F, 0x49, 0x49, 0x49, 0x36}, // 66 'B'
    {0x3E, 0x41, 0x41, 0x41, 0x22}, // 67 'C'
    {0x7F, 0x41, 0x41, 0x22, 0x1C}, // 68 'D'
    {0x7F, 0x49, 0x49, 0x49, 0x41}, // 69 'E'
    {0x7F, 0x09, 0x09, 0x09, 0x01}, // 70 'F'
    {0x3E, 0x41, 0x49, 0x49, 0x7A}, // 71 'G'
    {0x7F, 0x08, 0x08, 0x08, 0x7F}, // 72 'H'
    {0x00, 0x41, 0x7F, 0x41, 0x00}, // 73 'I'
    {0x20, 0x40, 0x41, 0x3F, 0x01}, // 74 'J'
    {0x7F, 0x08, 0x14, 0x22, 0x41}, // 75 'K'
    {0x7F, 0x40, 0x40, 0x40, 0x40}, // 76 'L'
    {0x7F, 0x02, 0x0C, 0x02, 0x7F}, // 77 'M'
    {0x7F, 0x04, 0x08, 0x10, 0x7F}, // 78 'N'
    {0x3E, 0x41, 0x41, 0x41, 0x3E}, // 79 'O'
    {0x7F, 0x09, 0x09, 0x09, 0x06}, // 80 'P'
    {0x3E, 0x41, 0x51, 0x21, 0x5E}, // 81 'Q'
    {0x7F, 0x09, 0x19, 0x29, 0x46}, // 82 'R'
    {0x26, 0x49, 0x49, 0x49, 0x32}, // 83 'S'
    {0x01, 0x01, 0x7F, 0x01, 0x01}, // 84 'T'
    {0x3F, 0x40, 0x40, 0x40, 0x3F}, // 85 'U'
    {0x1F, 0x20, 0x40, 0x20, 0x1F}, // 86 'V'
    {0x3F, 0x40, 0x38, 0x40, 0x3F}, // 87 'W'
    {0x63, 0x14, 0x08, 0x14, 0x63}, // 88 'X'
    {0x07, 0x08, 0x70, 0x08, 0x07}, // 89 'Y'
    {0x61, 0x51, 0x49, 0x45, 0x43}, // 90 'Z'
    {0x00, 0x7F, 0x41, 0x41, 0x00}, // 91 '['
    {0x02, 0x04, 0x08, 0x10, 0x20}, // 92 '\'
    {0x00, 0x41, 0x41, 0x7F, 0x00}, // 93 ']'
    {0x04, 0x02, 0x01, 0x02, 0x04}, // 94 '^'
    {0x40, 0x40, 0x40, 0x40, 0x40}, // 95 '_'
    {0x00, 0x01, 0x02, 0x04, 0x00}, // 96 '`'
    {0x20, 0x54, 0x54, 0x54, 0x78}, // 97 'a'
    {0x7F, 0x48, 0x44, 0x44, 0x38}, // 98 'b'
    {0x38, 0x44, 0x44, 0x44, 0x20}, // 99 'c'
    {0x38, 0x44, 0x44, 0x48, 0x7F}, // 100 'd'
    {0x38, 0x54, 0x54, 0x54, 0x18}, // 101 'e'
    {0x08, 0x7E, 0x09, 0x01, 0x02}, // 102 'f'
    {0x0C, 0x52, 0x52, 0x52, 0x3E}, // 103 'g'
    {0x7F, 0x08, 0x04, 0x04, 0x78}, // 104 'h'
    {0x00, 0x44, 0x7D, 0x40, 0x00}, // 105 'i'
    {0x20, 0x40, 0x44, 0x3D, 0x00}, // 106 'j'
    {0x7F, 0x10, 0x28, 0x44, 0x00}, // 107 'k'
    {0x00, 0x41, 0x7F, 0x40, 0x00}, // 108 'l'
    {0x7C, 0x04, 0x18, 0x04, 0x78}, // 109 'm'
    {0x7C, 0x08, 0x04, 0x04, 0x78}, // 110 'n'
    {0x38, 0x44, 0x44, 0x44, 0x38}, // 111 'o'
    {0x7C, 0x14, 0x14, 0x14, 0x08}, // 112 'p'
    {0x08, 0x14, 0x14, 0x18, 0x7C}, // 113 'q'
    {0x7C, 0x08, 0x04, 0x04, 0x08}, // 114 'r'
    {0x48, 0x54, 0x54, 0x54, 0x20}, // 115 's'
    {0x04, 0x3E, 0x44, 0x40, 0x20}, // 116 't'
    {0x3C, 0x40, 0x40, 0x20, 0x7C}, // 117 'u'
    {0x1C, 0x20, 0x40, 0x20, 0x1C}, // 118 'v'
    {0x3C, 0x40, 0x30, 0x40, 0x3C}, // 119 'w'
    {0x44, 0x28, 0x10, 0x28, 0x44}, // 120 'x'
    {0x0C, 0x50, 0x50, 0x50, 0x3C}, // 121 'y'
    {0x44, 0x64, 0x54, 0x4C, 0x44}  // 122 'z'
};

OLEDDisplay::OLEDDisplay() : _rstPin(21), _vextPin(36), _initialized(false) {
    memset(_buffer, 0, sizeof(_buffer));
}

void OLEDDisplay::sendCommand(uint8_t cmd) {
    Wire.beginTransmission(SSD1306_I2C_ADDR);
    Wire.write(0x00); // Command stream
    Wire.write(cmd);
    Wire.endTransmission();
}

void OLEDDisplay::sendData(const uint8_t* data, size_t len) {
    for (size_t i = 0; i < len; i += 31) {
        size_t chunk = (len - i > 31) ? 31 : (len - i);
        Wire.beginTransmission(SSD1306_I2C_ADDR);
        Wire.write(0x40); // Data stream
        Wire.write(data + i, chunk);
        Wire.endTransmission();
    }
}

bool OLEDDisplay::begin(int sda, int scl, int rst, int vext) {
    _rstPin = rst;
    _vextPin = vext;

    // Power on Vext rail for Heltec V3/V4 OLED display
    if (_vextPin >= 0) {
        pinMode(_vextPin, OUTPUT);
        digitalWrite(_vextPin, LOW); // Pull LOW to enable Vext bus power
        delay(50);
    }

    // Reset OLED panel
    if (_rstPin >= 0) {
        pinMode(_rstPin, OUTPUT);
        digitalWrite(_rstPin, LOW);
        delay(20);
        digitalWrite(_rstPin, HIGH);
        delay(20);
    }

    Wire.begin(sda, scl, 400000); // Fast 400kHz I2C bus

    // Check if SSD1306 responds on 0x3C
    Wire.beginTransmission(SSD1306_I2C_ADDR);
    if (Wire.endTransmission() != 0) {
        _initialized = false;
        return false;
    }

    // SSD1306 128x64 Initialization Sequence
    static const uint8_t initCmds[] = {
        0xAE,       // Display OFF
        0xD5, 0x80, // Set Display Clock Divide Ratio
        0xA8, 0x3F, // Set Multiplex Ratio (1 to 64)
        0xD3, 0x00, // Set Display Offset
        0x40,       // Set Display Start Line (0)
        0x8D, 0x14, // Enable Charge Pump
        0x20, 0x00, // Memory Addressing Mode: Horizontal
        0xA1,       // Segment Remap: col 127 mapped to SEG0
        0xC8,       // COM Output Scan Direction: Remapped
        0xDA, 0x12, // Set COM Pins Hardware Config
        0x81, 0xCF, // Set Contrast Control
        0xD9, 0xF1, // Set Pre-charge Period
        0xDB, 0x40, // Set VCOMH Deselect Level
        0xA4,       // Entire Display ON Resume
        0xA6,       // Set Normal Display (not inverted)
        0xAF        // Display ON
    };

    for (size_t i = 0; i < sizeof(initCmds); i++) {
        sendCommand(initCmds[i]);
    }

    _initialized = true;
    clear();
    update();
    return true;
}

void OLEDDisplay::clear() {
    memset(_buffer, 0, sizeof(_buffer));
}

void OLEDDisplay::update() {
    if (!_initialized) return;
    sendCommand(0x21); // Set Column Address
    sendCommand(0);    // Start col 0
    sendCommand(127);  // End col 127
    sendCommand(0x22); // Set Page Address
    sendCommand(0);    // Start page 0
    sendCommand(7);    // End page 7
    sendData(_buffer, sizeof(_buffer));
}

void OLEDDisplay::drawChar(uint8_t x, uint8_t page, char c) {
    if (x > 122 || page > 7) return;
    if (c < 32 || c > 122) c = '?';
    uint8_t fontIdx = c - 32;

    for (uint8_t i = 0; i < 5; i++) {
        uint8_t line = pgm_read_byte(&font5x7[fontIdx][i]);
        _buffer[page * 128 + x + i] = line;
    }
    _buffer[page * 128 + x + 5] = 0x00; // 1px trailing spacing
}

void OLEDDisplay::drawString(uint8_t col, uint8_t line, const char* str) {
    if (line > 7) return;
    uint8_t x = col * 6;
    while (*str && x < 122) {
        drawChar(x, line, *str++);
        x += 6;
    }
}

void OLEDDisplay::drawHeader(const char* title) {
    // Top bar background
    for (uint8_t x = 0; x < 128; x++) {
        _buffer[x] = 0xFF; // Invert top row header
    }
    // Render title in top bar
    uint8_t x = 2;
    while (*title && x < 122) {
        char c = *title++;
        if (c >= 32 && c <= 122) {
            uint8_t fontIdx = c - 32;
            for (uint8_t i = 0; i < 5; i++) {
                _buffer[x + i] = ~pgm_read_byte(&font5x7[fontIdx][i]); // Inverted text
            }
            _buffer[x + 5] = 0xFF;
            x += 6;
        }
    }
}

void OLEDDisplay::updateSensorScreen(const char* deviceId, const char* sensorName, float bMag_nT, uint32_t sampleCount, float vbat, const char* mode) {
    if (!_initialized) return;
    clear();
    drawHeader(" MAGNETOMETER SENSOR");
    
    char lineBuf[24];
    snprintf(lineBuf, sizeof(lineBuf), "ID: %s", deviceId);
    drawString(0, 1, lineBuf);
    
    snprintf(lineBuf, sizeof(lineBuf), "Type: %s", sensorName);
    drawString(0, 2, lineBuf);
    
    snprintf(lineBuf, sizeof(lineBuf), "|B|: %.1f nT", bMag_nT);
    drawString(0, 4, lineBuf);

    snprintf(lineBuf, sizeof(lineBuf), "Pkts: %u", sampleCount);
    drawString(0, 5, lineBuf);

    snprintf(lineBuf, sizeof(lineBuf), "Vbat: %.2fV  Mode:%s", vbat, mode);
    drawString(0, 6, lineBuf);

    update();
}

void OLEDDisplay::updateReceiverScreen(uint8_t activeNodes, uint32_t totalPackets, int lastRssi, const char* lastMac, const char* egressIp, const char* mode) {
    if (!_initialized) return;
    clear();
    drawHeader(" GATEWAY RECEIVER");

    char lineBuf[24];
    snprintf(lineBuf, sizeof(lineBuf), "Active Nodes: %u", activeNodes);
    drawString(0, 1, lineBuf);

    snprintf(lineBuf, sizeof(lineBuf), "Total Pkts: %u", totalPackets);
    drawString(0, 2, lineBuf);

    snprintf(lineBuf, sizeof(lineBuf), "Last: %s", lastMac);
    drawString(0, 4, lineBuf);

    snprintf(lineBuf, sizeof(lineBuf), "RSSI: %d dBm", lastRssi);
    drawString(0, 5, lineBuf);

    snprintf(lineBuf, sizeof(lineBuf), "IP: %s", egressIp[0] ? egressIp : "USB Serial");
    drawString(0, 6, lineBuf);

    update();
}

OLEDDisplay oledDisplay;
