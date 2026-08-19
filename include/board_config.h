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

inline void initBoardPower() {
#ifdef VEXT_PIN
    pinMode(VEXT_PIN, OUTPUT);
    digitalWrite(VEXT_PIN, LOW); // Pull Vext LOW to enable external power rail on Heltec V3/V4
    delay(50);
#endif
}

#endif // BOARD_CONFIG_H
