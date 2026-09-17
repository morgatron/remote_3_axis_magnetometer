#include "BLEEgress.h"
#include <NimBLEDevice.h>
#include "board_config.h"
#include "PowerManager.h"
#include "ReceiverContext.h"
#include "NodeTracker.h"

bool BLEEgress::_initialized = false;
uint32_t BLEEgress::_lastBroadcastMs = 0;

void BLEEgress::begin(const char* deviceName) {
    if (_initialized) return;

    if (!NimBLEDevice::isInitialized()) {
        NimBLEDevice::init(deviceName);
    }
    NimBLEDevice::setPower(9); // +9 dBm TX power

    _initialized = true;
    _lastBroadcastMs = millis();
    Serial.printf("[BLE EGRESS SUCCESS] Connectionless 1M Extended Advertising Broadcaster active (ID: '%s')\r\n", deviceName);
}

void BLEEgress::broadcast(const GatewayAdvPacket &pkt, uint32_t durationMs) {
    if (!_initialized) return;

    PowerManager::powerUpRadio();

    if (!NimBLEDevice::isInitialized()) {
        Serial.println(F("[BLE EGRESS] Broadcast failed: NimBLE not initialized"));
        return;
    }

    NimBLEExtAdvertising* pAdv = NimBLEDevice::getAdvertising();
    if (!pAdv) {
        Serial.println(F("[BLE EGRESS] Broadcast failed: no pAdv"));
        return;
    }

    pAdv->stop(0);

    NimBLEExtAdvertisement advData;
    advData.setLegacyAdvertising(false); // BLE 5 Extended Advertising
    advData.setConnectable(false);       // Non-connectable
    advData.setScannable(false);         // Non-scannable
    advData.setPrimaryPhy(BLE_HCI_LE_PHY_1M);
    advData.setSecondaryPhy(BLE_HCI_LE_PHY_1M);
    advData.setMinInterval(32);          // 20 ms interval
    advData.setMaxInterval(48);          // 30 ms interval

    size_t payloadLen = sizeof(GatewayAdvPacket) - (18 - pkt.sample_count) * sizeof(CompactSample);
    bool mfgOk = advData.setManufacturerData((const uint8_t*)&pkt, payloadLen);
    bool setOk = pAdv->setInstanceData(0, advData);
    bool startOk = false;
    if (setOk) {
        startOk = pAdv->start(0, durationMs); // Broadcast for durationMs (~30-50 advertising events per sec)
        _lastBroadcastMs = millis();
    }
    Serial.printf("[BLE EGRESS] Broadcast seq=%u count=%u payloadLen=%u mfgOk=%d setOk=%d startOk=%d dur=%ums\r\n",
                  pkt.packet_seq, pkt.sample_count, (unsigned)payloadLen, mfgOk, setOk, startOk, (unsigned)durationMs);
}

void BLEEgress::broadcastDiagnostic(uint8_t eventCode, uint8_t stateCode, uint8_t missCount, const char* targetNode, uint16_t metricVal) {
    if (!_initialized) return;

    GatewayAdvPacket diagPkt;
    memset(&diagPkt, 0, sizeof(diagPkt));
    diagPkt.company_id = 0xFFFF;
    diagPkt.magic[0] = 'M';
    diagPkt.magic[1] = 'G';
    static uint8_t diagSeq = 0;
    diagPkt.packet_seq = ++diagSeq;
    if (targetNode && targetNode[0] != '\0') {
        strncpy(diagPkt.node_id, targetNode, sizeof(diagPkt.node_id) - 1);
    } else {
        strncpy(diagPkt.node_id, "GW_IDLE", sizeof(diagPkt.node_id) - 1);
    }
    diagPkt.timestamp_us = (uint64_t)millis() * 1000ULL;
    diagPkt.sample_interval_ms = metricVal;
    diagPkt.sample_count = 0; // 0 samples denotes diagnostic/heartbeat packet
    diagPkt.status = ((uint16_t)eventCode << 12) | ((uint16_t)stateCode << 8) | (uint16_t)missCount;
    diagPkt.vbat_mv = getBatteryMilliVolts();
    diagPkt.rssi = (int8_t)nodeTracker.getLastRssi();
    diagPkt.gw_vbat_mv = getBatteryMilliVolts();

    PowerManager::acquireLock();
    broadcast(diagPkt, 120);
    vTaskDelay(pdMS_TO_TICKS(120));
    PowerManager::releaseLock();
}

void BLEEgress::poll() {
    if (!_initialized || !NimBLEDevice::isInitialized()) return;
    uint32_t now = millis();
    // Send a periodic heartbeat beacon if quiet for >= 60000 ms (or 15000 ms if no nodes ever seen)
    uint32_t quietTimeoutMs = (nodeTracker.getNodeCount() > 0) ? 60000 : 15000;
    if (now - _lastBroadcastMs >= quietTimeoutMs) {
        const char* target = (nodeTracker.getNodeCount() > 0) ? nodeTracker.getLastNodeId() : "GW_IDLE";
        broadcastDiagnostic(DIAG_EVENT_HEARTBEAT, 1 /* SLEEPING */, 0, target, 0);
    }
}

bool BLEEgress::isBroadcasting() {
    if (!NimBLEDevice::isInitialized()) return false;
    NimBLEExtAdvertising* pAdv = NimBLEDevice::getAdvertising();
    if (!pAdv) return false;
    return pAdv->isAdvertising();
}

bool BLEEgress::isConnected() {
    return false; // Connectionless advertising mode
}

bool BLEEgress::isInitialized() {
    return _initialized;
}
