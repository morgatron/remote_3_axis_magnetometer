#include "BLEStream.h"
#include <NimBLEDevice.h>

#define SERVICE_UUID           "6E400001-B5A3-F393-E0A9-E50E24DCCA9E" // Nordic UART Service
#define CHARACTERISTIC_UUID_TX "6E400003-B5A3-F393-E0A9-E50E24DCCA9E" // TX Characteristic

static NimBLEServer* pServer = nullptr;
static NimBLECharacteristic* pTxCharacteristic = nullptr;
static bool deviceConnected = false;
static volatile bool g_lastBatchAcked = false;

class ServerCallbacks : public NimBLEServerCallbacks {
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

#if CONFIG_BT_NIMBLE_EXT_ADV
class ExtAdvCallbacks : public NimBLEExtAdvertisingCallbacks {
    void onScanRequest(NimBLEExtAdvertising* pAdv, uint8_t instId, NimBLEAddress addr) override {
        g_lastBatchAcked = true;
    }
    void onStopped(NimBLEExtAdvertising* pAdv, int reason, uint8_t instId) override {
        // Managed by onScanRequest (hardware ACK) and timeout task
    }
};

static ExtAdvCallbacks extAdvCallbacks;
#endif

BLEStream::BLEStream() : _initialized(false) {}

void BLEStream::stopAdvertising() {
#if CONFIG_BT_NIMBLE_EXT_ADV
    NimBLEExtAdvertising* pAdvertising = NimBLEDevice::getAdvertising();
    if (pAdvertising) {
        pAdvertising->stop(0);
    }
#else
    NimBLEAdvertising* pAdvertising = NimBLEDevice::getAdvertising();
    if (pAdvertising) {
        pAdvertising->stop();
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
    NimBLEDevice::setPower(BLEConfig::TX_POWER_DBM);

    pServer = NimBLEDevice::createServer();
    pServer->setCallbacks(new ServerCallbacks());

    NimBLEService* pService = pServer->createService(SERVICE_UUID);
    pTxCharacteristic = pService->createCharacteristic(
        CHARACTERISTIC_UUID_TX,
        NIMBLE_PROPERTY::NOTIFY
    );
    pService->start();

#if CONFIG_BT_NIMBLE_EXT_ADV
    NimBLEExtAdvertising* pAdvertising = NimBLEDevice::getAdvertising();
    pAdvertising->setCallbacks(&extAdvCallbacks);

    NimBLEExtAdvertisement advData;
    advData.setLegacyAdvertising(false); // Enable Bluetooth 5.0 Extended Advertising
    advData.setConnectable(false);       // Non-connectable
    advData.setScannable(true);          // Scannable
    advData.setPrimaryPhy(BLE_HCI_LE_PHY_CODED);   // LE Coded PHY (S=8 Long Range)
    advData.setSecondaryPhy(BLE_HCI_LE_PHY_CODED); // LE Coded PHY (S=8 Long Range)
    advData.setMinInterval(BLEConfig::ADV_MIN_INTERVAL_UNITS);
    advData.setMaxInterval(BLEConfig::ADV_MAX_INTERVAL_UNITS);

    pAdvertising->setInstanceData(0, advData);
    pAdvertising->start(0);
#else
    NimBLEAdvertising* pAdvertising = NimBLEDevice::getAdvertising();
    pAdvertising->setMinInterval(BLEConfig::ADV_MIN_INTERVAL_UNITS);
    pAdvertising->setMaxInterval(BLEConfig::ADV_MAX_INTERVAL_UNITS);
    pAdvertising->enableScanResponse(true);
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
        advData.setLegacyAdvertising(false);
        advData.setConnectable(false);
        advData.setScannable(true);
        advData.setPrimaryPhy(BLE_HCI_LE_PHY_CODED);
        advData.setSecondaryPhy(BLE_HCI_LE_PHY_CODED);
        advData.setManufacturerData((const uint8_t*)&pkt, sizeof(pkt));
        pAdvertising->setInstanceData(0, advData);
        if (!pAdvertising->isAdvertising()) {
            pAdvertising->start(0);
        }
    }
#else
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
#endif
}

void BLEStream::notifyBatchBinary(const SensorBatchPacket &batch) {
    if (!_initialized) return;

#if CONFIG_BT_NIMBLE_EXT_ADV
    NimBLEExtAdvertising* pAdvertising = NimBLEDevice::getAdvertising();
    if (pAdvertising) {
        pAdvertising->stop(0);

        NimBLEExtAdvertisement advData;
        advData.setLegacyAdvertising(false);
        advData.setConnectable(false);
        advData.setScannable(true);
        advData.enableScanRequestCallback(true); // Hardware AUX_SCAN_REQ ACK notification
        advData.setPrimaryPhy(BLE_HCI_LE_PHY_CODED);
        advData.setSecondaryPhy(BLE_HCI_LE_PHY_CODED);
        advData.setManufacturerData((const uint8_t*)&batch, sizeof(batch));
        advData.setMinInterval(BLEConfig::ADV_MIN_INTERVAL_UNITS);
        advData.setMaxInterval(BLEConfig::ADV_MAX_INTERVAL_UNITS);

        pAdvertising->setInstanceData(0, advData);
        g_lastBatchAcked = false;
        pAdvertising->start(0, BLEConfig::BURST_DURATION_MS);
    }
#endif
}

bool BLEStream::isConnected() const {
    return deviceConnected;
}

BLEStream bleStream;
