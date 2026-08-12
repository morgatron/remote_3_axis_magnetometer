#include "BLEStream.h"
#include <NimBLEDevice.h>

#define SERVICE_UUID           "6E400001-B5A3-F393-E0A9-E50E24DCCA9E" // Nordic UART Service
#define CHARACTERISTIC_UUID_TX "6E400003-B5A3-F393-E0A9-E50E24DCCA9E" // TX Characteristic

static NimBLEServer* pServer = nullptr;
static NimBLECharacteristic* pTxCharacteristic = nullptr;
static bool deviceConnected = false;

#if defined(NIMBLE_CPP_VERSION) && NIMBLE_CPP_VERSION >= 20000
class ServerCallbacks: public NimBLEServerCallbacks {
    void onConnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo) override {
        deviceConnected = true;
    }

    void onDisconnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo, int reason) override {
        deviceConnected = false;
#if CONFIG_BT_NIMBLE_EXT_ADV
        NimBLEDevice::startAdvertising(0);
#else
        NimBLEDevice::startAdvertising();
#endif
    }
};
#else
class ServerCallbacks: public NimBLEServerCallbacks {
    void onConnect(NimBLEServer* pServer) override {
        deviceConnected = true;
    }

    void onDisconnect(NimBLEServer* pServer) override {
        deviceConnected = false;
#if CONFIG_BT_NIMBLE_EXT_ADV
        NimBLEDevice::startAdvertising(0);
#else
        NimBLEDevice::startAdvertising();
#endif
    }
};
#endif

static volatile bool g_lastBatchAcked = false;

#if CONFIG_BT_NIMBLE_EXT_ADV
class ExtAdvCallbacks : public NimBLEExtAdvertisingCallbacks {
    void onScanRequest(NimBLEExtAdvertising* pAdv, uint8_t instId, NimBLEAddress addr) override {
        Serial.println("DEBUG: onScanRequest Hardware ACK received!");
        g_lastBatchAcked = true;
    }
    void onStopped(NimBLEExtAdvertising* pAdv, int reason, uint8_t instId) override {
        // onStopped fires when timer expires or when stop() is called.
        // g_lastBatchAcked is managed by onScanRequest (hardware ACK) and timeout task.
        Serial.println("DEBUG: Stopped...");
    }
};
#endif

static ExtAdvCallbacks extAdvCallbacks;

BLEStream::BLEStream() : _initialized(false) {}

void BLEStream::stopAdvertising() {
#if CONFIG_BT_NIMBLE_EXT_ADV
    NimBLEExtAdvertising* pAdvertising = NimBLEDevice::getAdvertising();
    if (pAdvertising) {
        pAdvertising->stop(0);
    }
#endif
}

bool BLEStream::isBatchAcked() const {
    return g_lastBatchAcked;
}

void BLEStream::clearBatchAck() {
    g_lastBatchAcked = false;
}

void BLEStream::begin(const String &deviceName) {
    if (_initialized) return;

    NimBLEDevice::init(deviceName.c_str());
    NimBLEDevice::setPower(15); // Set maximum TX power (+15 dBm) for maximum RF range

    pServer = NimBLEDevice::createServer();
    pServer->setCallbacks(new ServerCallbacks());

    NimBLEService* pService = pServer->createService(SERVICE_UUID);
    pTxCharacteristic = pService->createCharacteristic(
        CHARACTERISTIC_UUID_TX,
        NIMBLE_PROPERTY::NOTIFY
    );

#if defined(NIMBLE_CPP_VERSION) && NIMBLE_CPP_VERSION < 20000
    pService->start();
#endif

#if CONFIG_BT_NIMBLE_EXT_ADV
    NimBLEExtAdvertising* pAdvertising = NimBLEDevice::getAdvertising();
    pAdvertising->setCallbacks(&extAdvCallbacks);
    NimBLEExtAdvertisement advData;
    advData.setLegacyAdvertising(false); // Enable Bluetooth 5.0 Extended Advertising
    advData.setConnectable(false);       // Non-connectable
    advData.setScannable(true);          // Scannable
    advData.setPrimaryPhy(BLE_HCI_LE_PHY_CODED);   // LE Coded PHY (S=8 Long Range)
    advData.setSecondaryPhy(BLE_HCI_LE_PHY_CODED); // LE Coded PHY (S=8 Long Range)
    advData.setMinInterval(160); // 100ms advertising interval
    advData.setMaxInterval(320); // 200ms max advertising interval
    pAdvertising->setInstanceData(0, advData);
    pAdvertising->start(0);
#else
    NimBLEAdvertising* pAdvertising = NimBLEDevice::getAdvertising();
    pAdvertising->setMinInterval(160); // 100ms advertising interval (power saving mode)
    pAdvertising->setMaxInterval(320); // 200ms max advertising interval
#if defined(NIMBLE_CPP_VERSION) && NIMBLE_CPP_VERSION >= 20000
    pAdvertising->enableScanResponse(true);
#else
    pAdvertising->setScanResponse(true);
#endif
    pAdvertising->start();
#endif
    _initialized = true;
}

void BLEStream::notify(const char *data) {
    if (!_initialized) return;

    if (deviceConnected && pTxCharacteristic != nullptr) {
        pTxCharacteristic->setValue((const uint8_t*)data, strlen(data));
        pTxCharacteristic->notify();
    }
}

void BLEStream::notifyBinary(const SensorBinaryPacket &pkt) {
    if (!_initialized) return;

#if CONFIG_BT_NIMBLE_EXT_ADV
    NimBLEExtAdvertising* pAdvertising = NimBLEDevice::getAdvertising();
    if (pAdvertising) {
        NimBLEExtAdvertisement advData;
        advData.setLegacyAdvertising(false); // Enable Bluetooth 5.0 Extended Advertising
        advData.setConnectable(false);       // Non-connectable
        advData.setScannable(true);          // Scannable
        advData.setPrimaryPhy(BLE_HCI_LE_PHY_CODED);   // LE Coded PHY (S=8 Long Range)
        advData.setSecondaryPhy(BLE_HCI_LE_PHY_CODED); // LE Coded PHY (S=8 Long Range)
        advData.setManufacturerData((const uint8_t*)&pkt, sizeof(pkt));
        pAdvertising->setInstanceData(0, advData);
        if (!pAdvertising->isAdvertising()) {
            pAdvertising->start(0);
        }
    }
#elif defined(NIMBLE_CPP_VERSION) && NIMBLE_CPP_VERSION >= 20000
    NimBLEAdvertising* pAdvertising = NimBLEDevice::getAdvertising();
    if (pAdvertising) {
        NimBLEAdvertisementData advData;
        advData.setFlags(0x06); // BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP
        advData.setManufacturerData((const uint8_t*)&pkt, sizeof(pkt));
        pAdvertising->setAdvertisementData(advData);

        if (!pAdvertising->isAdvertising()) {
            pAdvertising->start();
        } else {
            pAdvertising->refreshAdvertisingData();
        }
    }
#else
    NimBLEAdvertising* pAdvertising = NimBLEDevice::getAdvertising();
    if (pAdvertising) {
        if (pAdvertising->isAdvertising()) {
            pAdvertising->stop();
        }
        std::string mfr((const char*)&pkt, sizeof(pkt));
        NimBLEAdvertisementData advData;
        advData.setManufacturerData(mfr);
        pAdvertising->setAdvertisementData(advData);
        pAdvertising->start();
    }
#endif
}

void BLEStream::notifyBatchBinary(const SensorBatchPacket &batch) {
    if (!_initialized) return;

#if CONFIG_BT_NIMBLE_EXT_ADV
    NimBLEExtAdvertising* pAdvertising = NimBLEDevice::getAdvertising();
    if (pAdvertising) {
        pAdvertising->stop(0);

        NimBLEExtAdvertisement advData;
        advData.setLegacyAdvertising(false); // Enable Bluetooth 5.0 Extended Advertising
        advData.setConnectable(false);       // Non-connectable
        advData.setScannable(true);          // Scannable
        advData.enableScanRequestCallback(true); // Enable Hardware AUX_SCAN_REQ notification callback!
        advData.setPrimaryPhy(BLE_HCI_LE_PHY_CODED);   // LE Coded PHY (S=8 Long Range)
        advData.setSecondaryPhy(BLE_HCI_LE_PHY_CODED); // LE Coded PHY (S=8 Long Range)
        advData.setManufacturerData((const uint8_t*)&batch, sizeof(batch));
        //advData.setMinInterval(160); // 100ms min advertising interval
        //advData.setMaxInterval(320); // 200ms max advertising interval
        advData.setMinInterval(80); // 100ms min advertising interval
        advData.setMaxInterval(160); // 200ms max advertising interval

        pAdvertising->setInstanceData(0, advData);
        g_lastBatchAcked = false; // Reset ACK flag AFTER stop(0) to prevent synchronous onStopped pollution
        pAdvertising->start(0, 1000); // 1500ms duration (1.5 seconds awake window for transmission & ACKs)
        Serial.println("DEBUG: notifyBatchBinary-> sending");
    }
#endif
}

bool BLEStream::isConnected() const {
    return deviceConnected;
}

BLEStream bleStream;
