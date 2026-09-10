#include "RelayEgress.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include "board_config.h"
#include "BLEEgress.h"
#include "PowerManager.h"

void RelayEgress::begin() {
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
    if (egressModeConfig == MODE_EGRESS_SERIAL || egressModeConfig == MODE_EGRESS_BOTH || egressModeConfig == MODE_EGRESS_BLE) {
        if (Serial.availableForWrite() > 0) {
            Serial.print(item.line);
        }
    }

    // 2. WiFi Egress (Forward to Central Server or UDP listener over WiFi network)
    if ((egressModeConfig == MODE_EGRESS_WIFI || egressModeConfig == MODE_EGRESS_BOTH) && wifiRelayConnected) {
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
    advPkt.packet_seq = ++g_advSeq;
    strncpy(advPkt.node_id, item.node_id, sizeof(advPkt.node_id) - 1);
    advPkt.timestamp_us = item.timestamp_us;
    advPkt.sample_interval_ms = 1000;
    advPkt.status = (uint16_t)item.status;
    advPkt.vbat_mv = (uint16_t)(item.vbat * 1000.0f);
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

    for (;;) {
        if (xQueueReceive(telemetryQueue, &item, pdMS_TO_TICKS(50)) == pdTRUE) {
            relayedPacketCount = relayedPacketCount + 1;
            lastOledActivityMs = millis(); // Refresh OLED screen activity timer on valid packet arrival

            dispatchSerialWiFi(item, egressUdp, batchBuf, batchLen);

            // 3. BLE 1M Extended Advertising Egress (Connectionless Broadcast)
            if (egressModeConfig == MODE_EGRESS_BLE || egressModeConfig == MODE_EGRESS_BOTH) {
                PowerManager::acquireLock();
                GatewayAdvPacket advPkt;
                initAdvPacket(advPkt, item);

                // Bundle up to 10 samples per Extended Advertising packet (host BT adapter MTU limit)
                TelemetryItem nextItem;
                while (advPkt.sample_count < 10 && xQueueReceive(telemetryQueue, &nextItem, 0) == pdTRUE) {
                    relayedPacketCount = relayedPacketCount + 1;
                    dispatchSerialWiFi(nextItem, egressUdp, batchBuf, batchLen);
                    appendSampleToAdvPacket(advPkt, nextItem);
                }

                // If more samples are pending in queue (catch-up backlog), broadcast for 500 ms then yield to next packet
                uint32_t advDurMs = (uxQueueMessagesWaiting(telemetryQueue) > 0) ? 500 : 800;
                BLEEgress::broadcast(advPkt, advDurMs);
                vTaskDelay(pdMS_TO_TICKS(advDurMs));
                PowerManager::releaseLock();
            }
        }

        // Flush pending WiFi batch every 500ms
        if (batchLen > 0 && (millis() - lastBatchFlushMs >= 500)) {
            if (wifiRelayConnected) {
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
