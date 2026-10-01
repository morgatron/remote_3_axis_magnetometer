#include "HttpBatchEgress.h"
#include <HTTPClient.h>
#include <Preferences.h>
#include "WiFiManager.h"
#include "board_config.h"

String HttpBatchEgress::_serverUrl = "";
String HttpBatchEgress::_apiKey = "";

static Preferences s_httpPrefs;

void HttpBatchEgress::begin() {
    loadSettings();
}

void HttpBatchEgress::loadSettings() {
    s_httpPrefs.begin("rcvr_v0", true);
    _serverUrl = s_httpPrefs.getString("srv_url", "");
    _apiKey = s_httpPrefs.getString("api_key", "");
    s_httpPrefs.end();
}

void HttpBatchEgress::saveSettings() {
    s_httpPrefs.begin("rcvr_v0", false);
    s_httpPrefs.putString("srv_url", _serverUrl);
    s_httpPrefs.putString("api_key", _apiKey);
    s_httpPrefs.end();
}

bool HttpBatchEgress::hasServerUrl() {
    return (_serverUrl.length() > 0 && _serverUrl.startsWith("http"));
}

String HttpBatchEgress::getServerUrl() {
    return _serverUrl;
}

String HttpBatchEgress::getApiKey() {
    return _apiKey;
}

void HttpBatchEgress::setServerUrl(const String& url) {
    _serverUrl = url;
    _serverUrl.trim();
    saveSettings();
}

void HttpBatchEgress::setApiKey(const String& key) {
    _apiKey = key;
    _apiKey.trim();
    saveSettings();
}

bool HttpBatchEgress::postBatch(const TelemetryItem* items, size_t count) {
    if (!hasServerUrl() || !WiFiManager::isConnected() || items == nullptr || count == 0) {
        return false;
    }

    const char* nodeId = (items[0].node_id[0] != '\0') ? items[0].node_id : "SPRINGBANK";

    // Build JSON Batch Payload
    String payload;
    payload.reserve(count * 200 + 64);
    payload += "{\"node_id\":\"";
    payload += nodeId;
    payload += "\",\"points\":[";

    for (size_t i = 0; i < count; i++) {
        if (i > 0) payload += ",";
        payload += "{";

        // Timestamp
        String isoTs = WiFiManager::getUtcIsoString(items[i].timestamp_us);
        if (isoTs.length() > 0) {
            payload += "\"timestamp\":\"";
            payload += isoTs;
            payload += "\",";
        } else {
            payload += "\"timestamp\":null,";
        }

        // Magnetic Field Readings (nT)
        char numBuf[32];
        snprintf(numBuf, sizeof(numBuf), "\"x\":%.2f,\"y\":%.2f,\"z\":%.2f,",
                 items[i].x, items[i].y, items[i].z);
        payload += numBuf;

        // Temperature
        if (!isnan(items[i].temp) && items[i].temp > -100.0f && items[i].temp < 150.0f) {
            snprintf(numBuf, sizeof(numBuf), "\"temp\":%.2f,", items[i].temp);
            payload += numBuf;
        } else {
            payload += "\"temp\":null,";
        }

        // Battery Voltage (mV)
        uint16_t vbatMv = (uint16_t)roundf(items[i].vbat * 1000.0f);
        if (vbatMv > 500 && vbatMv < 5500) {
            snprintf(numBuf, sizeof(numBuf), "\"vbat\":%u,", vbatMv);
            payload += numBuf;
        } else {
            payload += "\"vbat\":null,";
        }

        // Signal RSSI (dBm)
        snprintf(numBuf, sizeof(numBuf), "\"rssi\":%d,", items[i].rssi);
        payload += numBuf;

        // Status flags & sensor model
        char statusHex[16];
        snprintf(statusHex, sizeof(statusHex), "0x%06X", (unsigned int)(items[i].status & 0xFFFFFF));
        payload += "\"status_flags\":\"";
        payload += statusHex;
        payload += "\",";

        const char* model = (items[i].status & STATUS_FLAG_FLC100) ? "FLC100" :
                            (items[i].status & STATUS_FLAG_RM3100) ? "RM3100" : "FLC100";
        payload += "\"sensor_model\":\"";
        payload += model;
        payload += "\"}";
    }
    payload += "]}";

    // Send HTTP POST
    HTTPClient http;
    http.begin(_serverUrl);
    http.setTimeout(3000); // 3-second non-blocking timeout
    http.addHeader("Content-Type", "application/json");
    if (_apiKey.length() > 0) {
        http.addHeader("X-API-Key", _apiKey);
    }

    int httpCode = http.POST(payload);
    bool success = (httpCode == 201 || httpCode == 200);

    if (success) {
        Serial.printf("[HTTP EGRESS SUCCESS] POST %u samples from '%s' to Central Server (HTTP %d)\r\n",
                      (unsigned int)count, nodeId, httpCode);
    } else {
        Serial.printf("[HTTP EGRESS WARNING] POST to '%s' failed (Code: %d, Error: %s)\r\n",
                      _serverUrl.c_str(), httpCode, http.errorToString(httpCode).c_str());
    }

    http.end();
    return success;
}

bool HttpBatchEgress::testPost() {
    if (!hasServerUrl()) {
        Serial.println(F("[HTTP TEST ERROR] No server URL configured. Set using: SERVER <url>"));
        return false;
    }
    if (!WiFiManager::isConnected()) {
        Serial.println(F("[HTTP TEST ERROR] Wi-Fi is not connected."));
        return false;
    }

    TelemetryItem testItem;
    memset(&testItem, 0, sizeof(testItem));
    strncpy(testItem.node_id, "SPRINGBANK", sizeof(testItem.node_id) - 1);
    testItem.x = 12345.67f;
    testItem.y = -23456.78f;
    testItem.z = 45678.90f;
    testItem.temp = 22.5f;
    testItem.vbat = 3.95f;
    testItem.rssi = -75;
    testItem.status = STATUS_FLAG_FLC100;
    testItem.timestamp_us = 0; // Current time

    Serial.printf("[HTTP TEST] Sending test batch to: %s\r\n", _serverUrl.c_str());
    return postBatch(&testItem, 1);
}
