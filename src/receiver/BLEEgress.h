#ifndef BLE_EGRESS_H
#define BLE_EGRESS_H

#include <Arduino.h>
#include <NimBLEDevice.h>

#define NUS_SERVICE_UUID           "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define NUS_CHARACTERISTIC_UUID_TX "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"
#define NUS_CHARACTERISTIC_UUID_RX "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"

class BLEEgressServerCallbacks : public NimBLEServerCallbacks {
public:
    void onConnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo) override {
        Serial.printf("[BLE EGRESS] Host PC connected! Conn Handle: %d, Peer: %s\r\n",
                      connInfo.getConnHandle(), connInfo.getAddress().toString().c_str());
    }

    void onDisconnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo, int reason) override {
        Serial.println(F("[BLE EGRESS] Host PC disconnected. Restarting 1M PHY advertising..."));
#if CONFIG_BT_NIMBLE_EXT_ADV
        NimBLEDevice::startAdvertising(0);
#else
        NimBLEDevice::startAdvertising();
#endif
    }
};

class BLEEgress {
public:
    static void begin(const char* deviceName = "MAG_GATEWAY") {
        if (_initialized) return;

        if (!NimBLEDevice::isInitialized()) {
            NimBLEDevice::init(deviceName);
        }
        NimBLEDevice::setPower(9); // +9 dBm TX power

        _pServer = NimBLEDevice::createServer();
        _pServer->setCallbacks(new BLEEgressServerCallbacks());

        NimBLEService* pService = _pServer->createService(NUS_SERVICE_UUID);
        _pTxCharacteristic = pService->createCharacteristic(
            NUS_CHARACTERISTIC_UUID_TX,
            NIMBLE_PROPERTY::NOTIFY
        );

#if CONFIG_BT_NIMBLE_EXT_ADV
        NimBLEExtAdvertising* pAdvertising = NimBLEDevice::getAdvertising();
        if (pAdvertising) {
            NimBLEExtAdvertisement advData;
            advData.setLegacyAdvertising(true);  // Legacy advertising on 1M PHY for universal PC/Mac compatibility
            advData.setConnectable(true);        // Connectable GATT Peripheral
            advData.setScannable(true);          // Scannable
            advData.setFlags(0x06);              // General discoverable
            advData.setCompleteServices(NimBLEUUID(NUS_SERVICE_UUID));
            advData.setPrimaryPhy(BLE_HCI_LE_PHY_1M);
            advData.setSecondaryPhy(BLE_HCI_LE_PHY_1M);
            advData.setMinInterval(80);          // 50 ms
            advData.setMaxInterval(160);         // 100 ms

            NimBLEExtAdvertisement scanResp;
            scanResp.setLegacyAdvertising(true);
            scanResp.setName(deviceName);

            pAdvertising->setInstanceData(0, advData);
            pAdvertising->setScanResponseData(0, scanResp);
            pAdvertising->start(0);
        }
#else
        NimBLEAdvertising* pAdvertising = NimBLEDevice::getAdvertising();
        if (pAdvertising) {
            pAdvertising->setName(deviceName);
            pAdvertising->addServiceUUID(NUS_SERVICE_UUID);
            pAdvertising->enableScanResponse(true);
            pAdvertising->setMinInterval(80);  // 50 ms
            pAdvertising->setMaxInterval(160); // 100 ms
            pAdvertising->start();
        }
#endif

        _initialized = true;
        Serial.printf("[BLE EGRESS SUCCESS] GATT NUS Server active on 1M PHY (Name: '%s')\r\n", deviceName);
    }

    static void notify(const char* data, size_t len) {
        if (!_initialized || !_pTxCharacteristic || !_pServer) return;
        if (_pServer->getConnectedCount() > 0) {
            _pTxCharacteristic->notify((const uint8_t*)data, len);
        }
    }

    static bool isConnected() {
        return _pServer && (_pServer->getConnectedCount() > 0);
    }

private:
    static inline bool _initialized = false;
    static inline NimBLEServer* _pServer = nullptr;
    static inline NimBLECharacteristic* _pTxCharacteristic = nullptr;
};

#endif // BLE_EGRESS_H
