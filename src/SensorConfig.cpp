#include "SensorConfig.h"
#include <Preferences.h>
#include <esp_mac.h>

#ifndef DEFAULT_OUTPUT_MODE
#define DEFAULT_OUTPUT_MODE MODE_BLE
#endif

#ifndef DEFAULT_RATE
#define DEFAULT_RATE 0x95
#endif

SensorConfig g_sensorConfig;

SensorConfig::SensorConfig()
    : streaming(true),
      rate(DEFAULT_RATE),
      downsample(1),
      sensorType(1),
      outputMode(DEFAULT_OUTPUT_MODE),
      batchSize(10),
      cycleCount(200),
      wifiSSID(""),
      wifiPass(""),
      targetIP(255, 255, 255, 255),
      targetPort(9876),
      udpListenPort(9876),
      deviceID("") {}

void SensorConfig::load() {
    Preferences prefs;
    prefs.begin("mcu_v0", true);
    streaming = prefs.getBool("streaming", true);
    sensorType = prefs.getUChar("sensor_type", 255);
    rate = prefs.getUChar("rate", DEFAULT_RATE);
    downsample = prefs.getUShort("downsample", (sensorType == 1) ? 1000 : 1);
    outputMode = prefs.getUChar("mode", DEFAULT_OUTPUT_MODE);
    batchSize = prefs.getUChar("batch_size", 10);
    if (batchSize < 1 || batchSize > 18) batchSize = 10;
    cycleCount = prefs.getUShort("cycle_count", 200);
    wifiSSID = prefs.getString("ssid", "");
    wifiPass = prefs.getString("pass", "");
    String tIP = prefs.getString("target", "255.255.255.255");
    targetIP.fromString(tIP);
    targetPort = prefs.getUShort("port", 9876);
    udpListenPort = 9876;
    deviceID = prefs.getString("device_id", "");
    prefs.end();

    if (deviceID.length() == 0) {
#if defined(DEFAULT_DEVICE_ID)
        deviceID = String(DEFAULT_DEVICE_ID);
#else
        uint8_t mac[6];
        esp_read_mac(mac, ESP_MAC_WIFI_STA);
        char macBuf[32];
        snprintf(macBuf, sizeof(macBuf), "NODE_%02X%02X%02X", mac[3], mac[4], mac[5]);
        deviceID = String(macBuf);
#endif
    }
}

void SensorConfig::save() {
    Preferences prefs;
    prefs.begin("mcu_v0", false);
    prefs.putBool("streaming", streaming);
    prefs.putUChar("rate", rate);
    prefs.putUShort("downsample", downsample);
    prefs.putUChar("sensor_type", sensorType);
    prefs.putUChar("mode", outputMode);
    prefs.putUChar("batch_size", batchSize);
    prefs.putUShort("cycle_count", cycleCount);
    prefs.putString("ssid", wifiSSID);
    prefs.putString("pass", wifiPass);
    prefs.putString("target", targetIP.toString());
    prefs.putString("device_id", deviceID);
    prefs.end();
}

const char* SensorConfig::getModeString() const {
    switch (outputMode) {
        case MODE_SERIAL: return "SERIAL";
        case MODE_WIFI:   return "WIFI";
        case MODE_BOTH:   return "BOTH (Serial+WiFi)";
        case MODE_BLE:    return "BLE (Bluetooth 5.0 Long Range)";
        case MODE_LORA:   return "LORA";
        default:          return "UNKNOWN";
    }
}

const char* SensorConfig::getSensorTypeString() const {
    switch (sensorType) {
        case SENSOR_TYPE_RM3100: return "RM3100";
        case SENSOR_TYPE_FLC100: return "FLC100-ADS131E08";
        case SENSOR_TYPE_MOCK:   return "Mock Synthetic Sensor";
        default:                 return "Auto-Detect";
    }
}
