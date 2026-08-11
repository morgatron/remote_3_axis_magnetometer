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
        NimBLEDevice::startAdvertising();
    }
};
#else
class ServerCallbacks: public NimBLEServerCallbacks {
    void onConnect(NimBLEServer* pServer) override {
        deviceConnected = true;
    }

    void onDisconnect(NimBLEServer* pServer) override {
        deviceConnected = false;
        NimBLEDevice::startAdvertising();
    }
};
#endif

BLEStream::BLEStream() : _initialized(false) {}

void BLEStream::begin(const String &deviceName) {
    if (_initialized) return;

    NimBLEDevice::init(deviceName.c_str());
    NimBLEDevice::setPower(ESP_PWR_LVL_P9); // Maximum +9dBm TX power for maximum range

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

    NimBLEAdvertising* pAdvertising = NimBLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(SERVICE_UUID);
    pAdvertising->setMinInterval(32); // 20ms advertising interval
    pAdvertising->setMaxInterval(48); // 30ms advertising interval
#if defined(NIMBLE_CPP_VERSION) && NIMBLE_CPP_VERSION >= 20000
    pAdvertising->enableScanResponse(true);
#else
    pAdvertising->setScanResponse(true);
#endif
    pAdvertising->start();
    _initialized = true;
}

void BLEStream::notify(const char *data) {
    if (!_initialized) return;

    NimBLEAdvertising* pAdvertising = NimBLEDevice::getAdvertising();
    if (pAdvertising) {
        pAdvertising->setManufacturerData(std::string(data));
        pAdvertising->refreshAdvertisingData();
    }

    if (deviceConnected && pTxCharacteristic != nullptr) {
        pTxCharacteristic->setValue((const uint8_t*)data, strlen(data));
        pTxCharacteristic->notify();
    }
}

void BLEStream::notifyBinary(const SensorBinaryPacket &pkt) {
    if (!_initialized) return;

    NimBLEAdvertising* pAdvertising = NimBLEDevice::getAdvertising();
    if (pAdvertising) {
        pAdvertising->setManufacturerData((const uint8_t*)&pkt, sizeof(pkt));
        pAdvertising->refreshAdvertisingData();
    }
}

bool BLEStream::isConnected() const {
    return deviceConnected;
}

BLEStream bleStream;

