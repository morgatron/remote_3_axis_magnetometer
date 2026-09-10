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

void BLEEgress::poll() {
    if (!_initialized || !NimBLEDevice::isInitialized()) return;
    uint32_t now = millis();
    // Send a periodic heartbeat beacon if quiet for >= 10000 ms (or 5000 ms if no nodes ever seen)
    uint32_t quietTimeoutMs = (nodeTracker.getNodeCount() > 0) ? 10000 : 5000;
    if (now - _lastBroadcastMs >= quietTimeoutMs) {
        GatewayAdvPacket hb;
        memset(&hb, 0, sizeof(hb));
        hb.company_id = 0xFFFF;
        hb.magic[0] = 'M';
        hb.magic[1] = 'G';
        static uint8_t hbSeq = 0;
        hb.packet_seq = ++hbSeq;
        strncpy(hb.node_id, "GW_IDLE", sizeof(hb.node_id) - 1);
        hb.timestamp_us = (uint64_t)now * 1000ULL;
        hb.sample_interval_ms = 1000;
        hb.sample_count = 0; // 0 samples indicates idle/heartbeat
        hb.status = 0x0001;
        hb.vbat_mv = getBatteryMilliVolts();
        hb.rssi = 0;
        hb.gw_vbat_mv = getBatteryMilliVolts();

        broadcast(hb);
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
