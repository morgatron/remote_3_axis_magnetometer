#include "WiFiManager.h"
#include <Preferences.h>
#include <time.h>
#include <sys/time.h>
#include "esp_eap_client.h"
#include <esp_mac.h>
#include "ReceiverContext.h"

String WiFiManager::_ssid = "";
String WiFiManager::_pass = "";
String WiFiManager::_eapUser = "";
String WiFiManager::_eapPass = "";
String WiFiManager::_eapIdentity = "";
bool WiFiManager::_ntpSynced = false;
uint32_t WiFiManager::_lastCheckMs = 0;

static Preferences s_wifiPrefs;

void WiFiManager::begin() {
    loadSettings();

    WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t info) {
        Serial.println(F("\r\n[WIFI EVENT] Associated to AP, negotiating EAP/DHCP..."));
    }, ARDUINO_EVENT_WIFI_STA_CONNECTED);

    WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t info) {
        uint8_t reason = info.wifi_sta_disconnected.reason;
        Serial.printf("\r\n[WIFI EVENT] Disconnected from AP. Reason code: %u (%s)\r\n",
                      reason, WiFi.disconnectReasonName((wifi_err_reason_t)reason));
    }, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);

    WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t info) {
        wifiRelayConnected = true;
        IPAddress ip = IPAddress(info.got_ip.ip_info.ip.addr);
        Serial.printf("\r\n[WIFI SUCCESS] Station Connected! Obtained IP address: %s (Gateway: %s, RSSI: %d dBm)\r\n",
                      ip.toString().c_str(), WiFi.gatewayIP().toString().c_str(), WiFi.RSSI());
        startNtpSync();
    }, ARDUINO_EVENT_WIFI_STA_GOT_IP);
}

void WiFiManager::loadSettings() {
    s_wifiPrefs.begin("rcvr_v0", true);
    _ssid = s_wifiPrefs.getString("ssid", "");
    _pass = s_wifiPrefs.getString("pass", "");
    _eapUser = s_wifiPrefs.getString("eap_user", "");
    _eapPass = s_wifiPrefs.getString("eap_pass", "");
    _eapIdentity = s_wifiPrefs.getString("eap_id", "");
    s_wifiPrefs.end();

    // Mirror to legacy globals if needed
    wifiSSID = _ssid;
    wifiPass = _pass;
}

void WiFiManager::saveSettings() {
    s_wifiPrefs.begin("rcvr_v0", false);
    s_wifiPrefs.putString("ssid", _ssid);
    s_wifiPrefs.putString("pass", _pass);
    s_wifiPrefs.putString("eap_user", _eapUser);
    s_wifiPrefs.putString("eap_pass", _eapPass);
    s_wifiPrefs.putString("eap_id", _eapIdentity);
    s_wifiPrefs.end();

    wifiSSID = _ssid;
    wifiPass = _pass;
}

void WiFiManager::setupSoftAP() {
    WiFi.mode(WIFI_AP_STA);
    delay(100);

    uint8_t mac[6];
    WiFi.macAddress(mac);
    char buf[32];
    snprintf(buf, sizeof(buf), "MAG_GATEWAY_%02X%02X", mac[4], mac[5]);
    apSSID = String(buf);
    IPAddress apIP(192, 168, 4, 1);
    IPAddress gateway(192, 168, 4, 1);
    IPAddress subnet(255, 255, 255, 0);
    WiFi.softAPConfig(apIP, gateway, subnet);
    bool apSuccess = WiFi.softAP(apSSID.c_str(), "magnetometer123", 1, 0, 8);

    if (apSuccess) {
        Serial.printf("[SOFTAP] Receiver Access Point Active: SSID '%s' (IP: 192.168.4.1)\r\n", apSSID.c_str());
    }
}

void WiFiManager::connect() {
    // If egress mode doesn't require Wi-Fi, shut radio down to minimize power
    if (egressModeConfig == MODE_EGRESS_SERIAL || egressModeConfig == MODE_EGRESS_BLE) {
        WiFi.mode(WIFI_OFF);
        wifiRelayConnected = false;
        Serial.printf("[POWER] Egress mode is %s. Wi-Fi radio powered OFF.\r\n",
                      (egressModeConfig == MODE_EGRESS_BLE) ? "BLE" : "SERIAL");
        return;
    }

    if (_ssid.length() == 0) {
        Serial.println(F("[WIFI] No Wi-Fi SSID configured. Starting fallback SoftAP..."));
        setupSoftAP();
        return;
    }

    WiFi.disconnect(true);
    delay(100);

    if (isEapConfigured()) {
        // WPA2-Enterprise (802.1X PEAP-MSCHAPv2)
        Serial.printf("[WIFI EAP] Connecting to Enterprise Network '%s' as user '%s' (Identity: %s, pass len: %d)...\r\n",
                      _ssid.c_str(), _eapUser.c_str(), 
                      _eapIdentity.length() > 0 ? _eapIdentity.c_str() : "None",
                      (int)_eapPass.length());
        WiFi.mode(WIFI_STA);

        esp_eap_client_clear_ca_cert();
        esp_eap_client_clear_certificate_and_key();
        esp_eap_client_set_disable_time_check(true);

        if (_eapIdentity.length() > 0) {
            esp_eap_client_set_identity((const unsigned char*)_eapIdentity.c_str(), _eapIdentity.length());
        } else {
            esp_eap_client_clear_identity();
        }

        esp_eap_client_set_username((const unsigned char*)_eapUser.c_str(), _eapUser.length());
        esp_eap_client_set_password((const unsigned char*)_eapPass.c_str(), _eapPass.length());
        esp_wifi_sta_enterprise_enable();

        wl_status_t st = WiFi.begin(_ssid.c_str());
        Serial.printf("[WIFI EAP] Association requested. Initial status: %d\r\n", (int)st);
    } else {
        // Standard WPA2-Personal (PSK)
        setupSoftAP();
        Serial.printf("[WIFI PSK] Connecting to '%s'...\r\n", _ssid.c_str());
        WiFi.begin(_ssid.c_str(), _pass.c_str());
    }

    // Wait up to 35 seconds for association, enterprise handshake, and DHCP lease
    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - start < 35000)) {
        delay(500);
        Serial.print(".");
    }

    if (WiFi.status() == WL_CONNECTED) {
        wifiRelayConnected = true;
        Serial.println(F("\r\n[WIFI SUCCESS] Station Connected!"));
        Serial.printf("  Local IP: %s\r\n", WiFi.localIP().toString().c_str());
        Serial.printf("  Gateway:  %s\r\n", WiFi.gatewayIP().toString().c_str());
        Serial.printf("  RSSI:     %d dBm\r\n", WiFi.RSSI());
        Serial.printf("  MAC:      %s\r\n", WiFi.macAddress().c_str());

        startNtpSync();
    } else {
        wifiRelayConnected = false;
        Serial.println(F("\r\n[WIFI WARNING] Unable to connect to configured network within initial timeout. Background retry active."));
    }
}

void WiFiManager::startNtpSync() {
    Serial.println(F("[NTP] Initializing network time sync (ntp.anu.edu.au, pool.ntp.org)..."));
    configTime(0, 0, "ntp.anu.edu.au", "pool.ntp.org", "time.google.com");
}

void WiFiManager::update() {
    uint32_t now = millis();
    if (now - _lastCheckMs < 5000) return;
    _lastCheckMs = now;

    if (egressModeConfig == MODE_EGRESS_SERIAL || egressModeConfig == MODE_EGRESS_BLE) {
        return;
    }

    wifiRelayConnected = (WiFi.status() == WL_CONNECTED);

    // Check NTP sync status
    time_t nowSec = time(nullptr);
    if (nowSec > 1700000000) { // Valid time after Nov 2023
        if (!_ntpSynced) {
            _ntpSynced = true;
            struct tm timeinfo;
            gmtime_r(&nowSec, &timeinfo);
            char timeBuf[64];
            strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%dT%H:%M:%SZ", &timeinfo);
            Serial.printf("[NTP SUCCESS] Clock synchronized! UTC: %s\r\n", timeBuf);
        }
    } else {
        _ntpSynced = false;
    }

    // Auto-reconnect if dropped or not yet connected
    static uint32_t s_lastReconnectAttemptMs = 0;
    if (!wifiRelayConnected && _ssid.length() > 0) {
        if (now - s_lastReconnectAttemptMs >= 30000) {
            s_lastReconnectAttemptMs = now;
            Serial.printf("[WIFI NOTICE] Not connected. Attempting connection to '%s'...\r\n", _ssid.c_str());
            if (isEapConfigured()) {
                esp_eap_client_clear_ca_cert();
                esp_eap_client_clear_certificate_and_key();
                esp_eap_client_set_disable_time_check(true);
                if (_eapIdentity.length() > 0) {
                    esp_eap_client_set_identity((const unsigned char*)_eapIdentity.c_str(), _eapIdentity.length());
                } else {
                    esp_eap_client_clear_identity();
                }
                esp_eap_client_set_username((const unsigned char*)_eapUser.c_str(), _eapUser.length());
                esp_eap_client_set_password((const unsigned char*)_eapPass.c_str(), _eapPass.length());
                esp_wifi_sta_enterprise_enable();
                WiFi.begin(_ssid.c_str());
            } else {
                WiFi.begin(_ssid.c_str(), _pass.c_str());
            }
        }
    }
}

bool WiFiManager::isConnected() {
    return (WiFi.status() == WL_CONNECTED);
}

bool WiFiManager::isNtpSynced() {
    return _ntpSynced;
}

bool WiFiManager::isEapConfigured() {
    return (_eapUser.length() > 0 && _eapPass.length() > 0);
}

String WiFiManager::getMacAddress() {
    uint8_t baseMac[6] = {0};
    if (esp_read_mac(baseMac, ESP_MAC_WIFI_STA) == ESP_OK) {
        char macBuf[20];
        snprintf(macBuf, sizeof(macBuf), "%02X:%02X:%02X:%02X:%02X:%02X",
                 baseMac[0], baseMac[1], baseMac[2], baseMac[3], baseMac[4], baseMac[5]);
        return String(macBuf);
    }
    return WiFi.macAddress();
}

String WiFiManager::getIpAddress() {
    if (WiFi.status() == WL_CONNECTED) {
        return WiFi.localIP().toString();
    }
    return apSSID.length() > 0 ? "192.168.4.1" : "0.0.0.0";
}

int WiFiManager::getRssi() {
    return WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0;
}

String WiFiManager::getSsid() {
    return _ssid;
}

String WiFiManager::getEapUsername() {
    return _eapUser;
}

String WiFiManager::getEapIdentity() {
    return _eapIdentity;
}

int WiFiManager::getEapPasswordLength() {
    return (int)_eapPass.length();
}

String WiFiManager::getUtcIsoString(uint64_t timestamp_us) {
    time_t sec;
    uint32_t ms;

    if (timestamp_us > 1000000000000000ULL) {
        // Absolute UTC epoch timestamp in microseconds
        sec = (time_t)(timestamp_us / 1000000ULL);
        ms = (uint32_t)((timestamp_us % 1000000ULL) / 1000ULL);
    } else if (_ntpSynced) {
        // Current system time
        struct timeval tv;
        gettimeofday(&tv, nullptr);
        sec = tv.tv_sec;
        ms = tv.tv_usec / 1000;
    } else {
        return "";
    }

    struct tm timeinfo;
    gmtime_r(&sec, &timeinfo);
    char buf[36];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02d.%03luZ",
             timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
             timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec,
             (unsigned long)ms);
    return String(buf);
}

void WiFiManager::setPskCredentials(const String& ssid, const String& pass) {
    _ssid = ssid;
    _pass = pass;
    _eapUser = "";
    _eapPass = "";
    _eapIdentity = "";
    saveSettings();
}

void WiFiManager::setEapCredentials(const String& ssid, const String& user, const String& pass, const String& id) {
    _ssid = ssid;
    _pass = "";
    _eapUser = user;
    _eapPass = pass;
    _eapIdentity = id; // Leave empty if not explicitly provided
    saveSettings();
}

void WiFiManager::setEapUsername(const String& user, const String& id) {
    _eapUser = user;
    _eapIdentity = id;
    saveSettings();
}

void WiFiManager::setEapSsid(const String& ssid) {
    _ssid = ssid;
    saveSettings();
}

void WiFiManager::clearEapCredentials() {
    _eapUser = "";
    _eapPass = "";
    _eapIdentity = "";
    saveSettings();
}

void WiFiManager::clearAllCredentials() {
    _ssid = "";
    _pass = "";
    _eapUser = "";
    _eapPass = "";
    _eapIdentity = "";
    saveSettings();
}
