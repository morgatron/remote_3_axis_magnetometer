#include "BLEStream.h"
#include <NimBLEDevice.h>
#include "esp_bt.h"

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

BLEStream::BLEStream() : _initialized(false), _savedDeviceName("") {}

void BLEStream::stopAdvertising() {
    if (!NimBLEDevice::isInitialized()) return;

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

void BLEStream::powerDownModem() {
    if (deviceConnected) return; // Preserve active connection if central is connected

    stopAdvertising();

#if defined(ESP_PLATFORM)
    if (NimBLEDevice::isInitialized()) {
        NimBLEDevice::deinit(false);
    }
    if (esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_ENABLED) {
        esp_bt_controller_disable();
    }
#endif
    _initialized = false;
}

void BLEStream::powerUpModem(const String &deviceName) {
    if (_initialized && NimBLEDevice::isInitialized()) return;

    if (deviceName.length() > 0) {
        _savedDeviceName = deviceName;
    }

#if defined(ESP_PLATFORM)
    if (esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_INITED) {
        esp_bt_controller_enable(ESP_BT_MODE_BLE);
    }
#endif

    const char *nameToUse = (_savedDeviceName.length() > 0) ? _savedDeviceName.c_str() : "CREEK";
    if (!NimBLEDevice::isInitialized()) {
        NimBLEDevice::init(nameToUse);
    }
    NimBLEDevice::setPower(BLEConfig::TX_POWER_DBM);

#if CONFIG_BT_NIMBLE_EXT_ADV
    NimBLEExtAdvertising* pAdvertising = NimBLEDevice::getAdvertising();
    if (pAdvertising) {
        pAdvertising->setCallbacks(&extAdvCallbacks);
    }
#endif
    _initialized = true;
}

bool BLEStream::isBatchAcked() const {
    return g_lastBatchAcked;
}

void BLEStream::clearBatchAck() {
    g_lastBatchAcked = false;
}

void BLEStream::begin(const String &deviceName) {
    _savedDeviceName = deviceName;
    powerUpModem(deviceName);
}

bool BLEStream::isModemPowered() const {
    return _initialized;
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
