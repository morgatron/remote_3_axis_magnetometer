#include "RelayEgress.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include "board_config.h"
#include "BLEEgress.h"
#include "PowerManager.h"
#include "WiFiManager.h"
#include "HttpBatchEgress.h"

void RelayEgress::begin() {
    HttpBatchEgress::begin();
    PowerManager::setSleepPredicate([]() {
        return !BLEEgress::isBroadcasting();
    });
    xTaskCreatePinnedToCore(
        relayTask,
        "RelayEgressTask",
        8192,
        NULL,
        2,
        NULL,
        tskNO_AFFINITY
    );
    Serial.println(F("[RELAY EGRESS SUCCESS] FreeRTOS Egress Relay Task Spawned"));
}

void RelayEgress::dispatchSerialWiFi(const TelemetryItem &item, WiFiUDP &egressUdp, char *batchBuf, size_t &batchLen) {
    // 1. Serial Egress (USB CDC output to host PC / gateway.py)
    if (egressModeConfig == MODE_EGRESS_SERIAL || egressModeConfig == MODE_EGRESS_BOTH) {
        if (Serial.availableForWrite() > 0) {
            Serial.print(item.line);
        }
    }

    // 2. UDP WiFi Egress (Fallback when no HTTP server URL is configured)
    if (!HttpBatchEgress::hasServerUrl() && (egressModeConfig == MODE_EGRESS_WIFI || egressModeConfig == MODE_EGRESS_BOTH) && WiFiManager::isConnected()) {
        size_t lineLen = strlen(item.line);
        if (batchLen + lineLen >= 2048 - 1) {
            flushWiFiBatch(egressUdp, batchBuf, batchLen);
            batchLen = 0;
        }
        memcpy(batchBuf + batchLen, item.line, lineLen);
        batchLen += lineLen;
        batchBuf[batchLen] = '\0';
    }
}

void RelayEgress::initAdvPacket(GatewayAdvPacket &advPkt, const TelemetryItem &item) {
    memset(&advPkt, 0, sizeof(advPkt));
    advPkt.company_id = 0xFFFF;
    advPkt.magic[0] = 'M';
    advPkt.magic[1] = 'G';
    static uint8_t g_advSeq = 0;
    size_t nodeLen = strlen(item.node_id);
    if (nodeLen > sizeof(advPkt.node_id)) nodeLen = sizeof(advPkt.node_id);
    memcpy(advPkt.node_id, item.node_id, nodeLen);
    advPkt.timestamp_us = item.timestamp_us;
    advPkt.sample_interval_ms = 1000;
    advPkt.status = (uint16_t)item.status;
    advPkt.vbat_mv = (uint16_t)(item.vbat * 1000.0f);
    advPkt.temp_c_x100 = (int16_t)roundf(item.temp * 100.0f);
    advPkt.rssi = (int8_t)item.rssi;
    advPkt.gw_vbat_mv = getBatteryMilliVolts();
    advPkt.samples[0].x_nT = item.x;
    advPkt.samples[0].y_nT = item.y;
    advPkt.samples[0].z_nT = item.z;
    advPkt.sample_count = 1;
}

void RelayEgress::appendSampleToAdvPacket(GatewayAdvPacket &advPkt, const TelemetryItem &item) {
    if (advPkt.sample_count >= 18) return;
    uint8_t idx = advPkt.sample_count++;
    advPkt.samples[idx].x_nT = item.x;
    advPkt.samples[idx].y_nT = item.y;
    advPkt.samples[idx].z_nT = item.z;
    advPkt.timestamp_us = item.timestamp_us;
}

void RelayEgress::relayTask(void *pvParameters) {
    TelemetryItem item;
    WiFiUDP egressUdp;
    static char batchBuf[2048];
    static size_t batchLen = 0;
    static uint32_t lastBatchFlushMs = 0;

    static TelemetryItem httpBatchItems[18];

    for (;;) {
        if (xQueueReceive(telemetryQueue, &item, pdMS_TO_TICKS(50)) == pdTRUE) {
            relayedPacketCount = relayedPacketCount + 1;
            lastOledActivityMs = millis(); // Refresh OLED screen activity timer on valid packet arrival

            dispatchSerialWiFi(item, egressUdp, batchBuf, batchLen);

            // Collect batch for HTTP POST and/or BLE advertisement
            size_t httpCount = 0;
            bool doHttp = HttpBatchEgress::hasServerUrl() && (egressModeConfig == MODE_EGRESS_WIFI || egressModeConfig == MODE_EGRESS_BOTH) && WiFiManager::isConnected();
            if (doHttp) {
                httpBatchItems[httpCount++] = item;
            }

            GatewayAdvPacket advPkt;
            bool doBle = (egressModeConfig == MODE_EGRESS_BLE || egressModeConfig == MODE_EGRESS_BOTH);
            if (doBle) {
                initAdvPacket(advPkt, item);
            }

            // Drain any pending items belonging to the same burst (up to 18 samples)
            TelemetryItem nextItem;
            while (httpCount < 18 && xQueueReceive(telemetryQueue, &nextItem, 0) == pdTRUE) {
                relayedPacketCount = relayedPacketCount + 1;
                dispatchSerialWiFi(nextItem, egressUdp, batchBuf, batchLen);
                if (doHttp) {
                    httpBatchItems[httpCount++] = nextItem;
                }
                if (doBle && advPkt.sample_count < 10) {
                    appendSampleToAdvPacket(advPkt, nextItem);
                }
            }

            // 1. Direct HTTP POST to Central Server
            if (doHttp && httpCount > 0) {
                HttpBatchEgress::postBatch(httpBatchItems, httpCount);
            }

            // 2. BLE 1M Extended Advertising Egress (Connectionless Broadcast)
            if (doBle) {
                PowerManager::acquireLock();
                uint32_t advDurMs = (uxQueueMessagesWaiting(telemetryQueue) > 0) ? 100 : 120;
                BLEEgress::broadcast(advPkt, advDurMs);
                vTaskDelay(pdMS_TO_TICKS(advDurMs));
                PowerManager::releaseLock();
            }
        }

        // Flush pending UDP batch every 500ms (if UDP mode active)
        if (batchLen > 0 && (millis() - lastBatchFlushMs >= 500)) {
            if (WiFiManager::isConnected()) {
                flushWiFiBatch(egressUdp, batchBuf, batchLen);
            }
            batchLen = 0;
            lastBatchFlushMs = millis();
        }
    }
}

void RelayEgress::flushWiFiBatch(WiFiUDP &udp, const char* buf, size_t len) {
    if (len == 0 || targetServerIP.length() == 0) return;

    IPAddress addr;
    if (addr.fromString(targetServerIP)) {
        udp.beginPacket(addr, targetServerPort);
        udp.write((const uint8_t*)buf, len);
        udp.endPacket();
    }
}
