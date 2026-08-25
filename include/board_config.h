#ifndef BOARD_CONFIG_H
#define BOARD_CONFIG_H

#include <Arduino.h>

#if defined(HELTEC_V4) || defined(ARDUINO_heltec_wifi_lora_32_V3)
// Heltec WiFi LoRa 32 V3 / V4 (ESP32-S3 + SX1262)
#define BOARD_NAME   "Heltec WiFi LoRa 32 V4 (ESP32-S3)"
#define BOARD_HAS_LORA 1
#define SCK_PIN      41
#define MOSI_PIN     42
#define MISO_PIN     40
#define DRDY_PIN     38
#define CS_PIN       39
#define LED_PIN      35
#define VEXT_PIN     36

// LoRa Radio Pins for Heltec V4
#define LORA_CS_PIN   8
#define LORA_DIO1_PIN 14
#define LORA_RST_PIN  12
#define LORA_BUSY_PIN 13
#define LORA_SCK_PIN  9
#define LORA_MOSI_PIN 10
#define LORA_MISO_PIN 11

#elif defined(CONFIG_IDF_TARGET_ESP32C6) || defined(ARDUINO_ARCH_ESP32C6)
// Custom ESP32-C6 RISC-V PCB Pinout
#define BOARD_NAME   "ESP32-C6 RISC-V PCB"
#define SCK_PIN      6
#define MOSI_PIN     7
#define MISO_PIN     2
#define DRDY_PIN     3
#define CS_PIN       10
#define LED_PIN      -1

#elif defined(CONFIG_IDF_TARGET_ESP32C3) || defined(ARDUINO_ARCH_ESP32C3)
// Custom ESP32-C3 RISC-V PCB Pinout
#define BOARD_NAME   "ESP32-C3 RISC-V PCB"
#define SCK_PIN      6
#define MOSI_PIN     7
#define MISO_PIN     2
#define DRDY_PIN     3
#define CS_PIN       10
#define LED_PIN      -1

#else
// Legacy ESP32 Dev Module Pinout [DEPRECATED]
#define BOARD_NAME   "Legacy ESP32 Dev Module (WROOM)"
#define SCK_PIN      18
#define MISO_PIN     19
#define MOSI_PIN     23
#define CS_PIN       5
#define DRDY_PIN     4
#define LED_PIN      2
#endif

#define RESET_PIN   -1

#if defined(HELTEC_V4) || defined(ARDUINO_heltec_wifi_lora_32_V3)
// Battery Measurement Pins for Heltec WiFi LoRa 32 V3 / V4 (ESP32-S3)
#define VBAT_ADC_PIN   1
#define VBAT_CTRL_PIN  37
#define VBAT_DIVIDER_RATIO 4.90f // 390k / 100k divider + P-FET drop compensation (4.90x)
#endif

// Global battery cache
extern float g_cachedBatteryVoltage;

/**
 * @brief Measures real battery voltage via ADC and updates global cache.
 *        Should be called during the 10-second radio burst wake window.
 */
inline float sampleBatteryVoltage() {
#if defined(VBAT_ADC_PIN) && defined(VBAT_CTRL_PIN)
    pinMode(VBAT_CTRL_PIN, OUTPUT);
    digitalWrite(VBAT_CTRL_PIN, LOW); // Active-LOW gate on Heltec V3/V4 P-MOSFET
    delay(5);                          // 5ms required for 390k/100k RC filter capacitor to charge

    int raw = analogRead(VBAT_ADC_PIN);
    uint32_t rawMv = analogReadMilliVolts(VBAT_ADC_PIN);

    // Polarity fallback: if reading is ~0, check if this PCB revision uses active-HIGH control
    if (raw < 50) {
        digitalWrite(VBAT_CTRL_PIN, HIGH);
        delay(5);
        int rawHigh = analogRead(VBAT_ADC_PIN);
        if (rawHigh > raw) {
            raw = rawHigh;
            rawMv = analogReadMilliVolts(VBAT_ADC_PIN);
        }
    }

    pinMode(VBAT_CTRL_PIN, INPUT); // Disconnect divider to eliminate quiescent sleep drain

    if (raw > 50) {
        // eFuse factory-calibrated millivolt scaling with 4.90x divider factor
        float measured = (float)rawMv * (VBAT_DIVIDER_RATIO / 1000.0f);
        // Fallback for non-calibrated eFuse: standard 12-bit ADC scaling (raw / 238.7)
        if (measured < 2.0f && raw > 50) {
            measured = (float)raw / 238.7f;
        }

        if (g_cachedBatteryVoltage <= 0.0f) {
            g_cachedBatteryVoltage = measured;
        } else {
            g_cachedBatteryVoltage = 0.75f * g_cachedBatteryVoltage + 0.25f * measured; // Low-pass filter
        }
    }

    return (g_cachedBatteryVoltage > 0.0f) ? g_cachedBatteryVoltage : 0.0f;
#elif defined(VBAT_ADC_PIN)
    uint32_t rawMv = analogReadMilliVolts(VBAT_ADC_PIN);
    g_cachedBatteryVoltage = (float)rawMv * (2.0f / 1000.0f);
    return g_cachedBatteryVoltage;
#else
    g_cachedBatteryVoltage = 3.70f;
    return 3.70f;
#endif
}

/**
 * @brief Returns the cached battery voltage instantly (0ms latency, zero ADC/sleep overhead).
 */
inline float getBatteryVoltage() {
    return (g_cachedBatteryVoltage > 0.0f) ? g_cachedBatteryVoltage : 3.70f;
}

inline uint16_t sampleBatteryMilliVolts() {
    float v = sampleBatteryVoltage();
    return (uint16_t)(v * 1000.0f);
}

inline uint16_t getBatteryMilliVolts() {
    return (uint16_t)(getBatteryVoltage() * 1000.0f);
}

// Backward-compatible aliases
inline float readBatteryVoltage() { return getBatteryVoltage(); }
inline uint16_t readBatteryMilliVolts() { return getBatteryMilliVolts(); }

inline void initBoardPower() {
#ifdef VEXT_PIN
    pinMode(VEXT_PIN, OUTPUT);
    digitalWrite(VEXT_PIN, LOW); // Pull Vext LOW to enable external power rail on Heltec V3/V4
    delay(50);
#endif
#ifdef VBAT_CTRL_PIN
    pinMode(VBAT_CTRL_PIN, INPUT); // Default to High-Z / off
#endif
}

#endif // BOARD_CONFIG_H
