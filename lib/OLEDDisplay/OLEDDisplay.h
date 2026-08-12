#ifndef OLED_DISPLAY_H
#define OLED_DISPLAY_H

#include <Arduino.h>
#include <Wire.h>

/**
 * @brief Lightweight SSD1306 128x64 OLED Display Driver for Heltec V3/V4.
 * 
 * Uses hardware I2C (SDA=17, SCL=18, RST=21) powered by Vext (GPIO 36).
 */
class OLEDDisplay {
public:
    OLEDDisplay();
    
    /**
     * @brief Initialize display hardware on Heltec V3/V4.
     * @return true if SSD1306 ACKed on I2C address 0x3C.
     */
    bool begin(int sda = 17, int scl = 18, int rst = 21, int vext = 36);
    
    void clear();
    void update();
    void displayOff();
    void displayOn();
    
    void drawString(uint8_t col, uint8_t line, const char* str);
    void drawHeader(const char* title);
    
    // Custom Status Views
    void updateSensorScreen(const char* deviceId, const char* sensorName, float bMag_nT, uint32_t sampleCount, float vbat, const char* mode);
    void updateReceiverScreen(uint8_t activeNodes, uint32_t totalPackets, int lastRssi, const char* lastMac, const char* egressIp, const char* mode);

private:
    uint8_t _buffer[1024]; // 128x64 / 8 = 1024 bytes frame buffer
    int _rstPin;
    int _vextPin;
    bool _initialized;
    
    void sendCommand(uint8_t cmd);
    void sendData(const uint8_t* data, size_t len);
    void drawChar(uint8_t x, uint8_t y, char c);
};

extern OLEDDisplay oledDisplay;

#endif // OLED_DISPLAY_H
