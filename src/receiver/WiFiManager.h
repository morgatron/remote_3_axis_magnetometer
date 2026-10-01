#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <Arduino.h>
#include <WiFi.h>

/**
 * @brief Manages Wi-Fi station connectivity, WPA2-Enterprise (802.1X PEAP),
 *        SoftAP fallback, and NTP network clock synchronization.
 */
class WiFiManager {
public:
    static void begin();
    static void connect();
    static void update();

    static bool isConnected();
    static bool isNtpSynced();
    static bool isEapConfigured();

    static String getMacAddress();
    static String getIpAddress();
    static int getRssi();
    static String getSsid();
    static String getEapUsername();
    static String getUtcIsoString(uint64_t timestamp_us = 0);

    static void setPskCredentials(const String& ssid, const String& pass);
    static void setEapCredentials(const String& ssid, const String& user, const String& pass, const String& id = "");
    static void clearEapCredentials();
    static void clearAllCredentials();

    static void saveSettings();
    static void loadSettings();

private:
    static void startNtpSync();
    static void setupSoftAP();

    static String _ssid;
    static String _pass;
    static String _eapUser;
    static String _eapPass;
    static String _eapIdentity;
    static bool _ntpSynced;
    static uint32_t _lastCheckMs;
};

#endif // WIFI_MANAGER_H
