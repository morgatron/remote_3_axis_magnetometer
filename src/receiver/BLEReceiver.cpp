#include "BLEReceiver.h"
#if defined(ESP_PLATFORM)
#include "esp_bt.h"
#endif
#include <NimBLEDevice.h>
#include "ReceiverContext.h"
#include "TelemetryPacket.h"
#include "NodeTracker.h"
#include "BLEEgress.h"
#include "IngestionPipeline.h"
#include "PowerManager.h"

#ifndef DEBUG_BLE_SCHEDULER
#define DEBUG_BLE_SCHEDULER 1
#endif

#if DEBUG_BLE_SCHEDULER
  #define SCHED_PRINTF(...) do { if (g_debugScheduler) Serial.printf(__VA_ARGS__); } while(0)
  #define SCHED_PRINTLN(...) do { if (g_debugScheduler) Serial.println(__VA_ARGS__); } while(0)
#else
  #define SCHED_PRINTF(...) ((void)0)
  #define SCHED_PRINTLN(...) ((void)0)
#endif

class BLEReceiverCallbacks : public NimBLEScanCallbacks {
    void onResult(const NimBLEAdvertisedDevice* advertisedDevice) override {
        std::string name = advertisedDevice->getName();
        std::string manuData = advertisedDevice->getManufacturerData();
        int rssi = advertisedDevice->getRSSI();
        NimBLEAddress addr = advertisedDevice->getAddress();
        const uint8_t* nativeAddr = (const uint8_t*)addr.getBase();
        
        uint8_t mac[6] = {0};
        if (nativeAddr) {
            for (int i = 0; i < 6; i++) mac[i] = nativeAddr[5 - i];
        }

        size_t mlen = manuData.length();
        const char* mptr = manuData.data();

        if (mlen >= 19) {
            SCHED_PRINTF("[BLE RX EVENT] name='%s' mlen=%u rssi=%d\r\n", name.c_str(), (unsigned)mlen, rssi);
        }

        // 1. Process SensorBatchPacket (Extended Advertising Coded PHY burst, 1 to 18 samples)
        size_t minBatchSize = offsetof(SensorBatchPacket, samples) + sizeof(CompactSample);
        if (mlen >= minBatchSize) {
            size_t maxOffset = (mlen > minBatchSize) ? min((size_t)4, mlen - minBatchSize) : 0;
            for (size_t offset = 0; offset <= maxOffset; offset++) {
                SensorBatchPacket batch;
                memset(&batch, 0, sizeof(batch));
                size_t copyLen = min(sizeof(batch), mlen - offset);
                memcpy(&batch, mptr + offset, copyLen);
                if (IngestionPipeline::ingestBatch(batch, mac, rssi, "BLE")) {
                    bleRxCount = bleRxCount + batch.sample_count;
                    SCHED_PRINTF("[BLE RX BATCH ACCEPTED] Node: '%s', Samples: %d, RSSI: %d\r\n",
                                 batch.device_id, batch.sample_count, rssi);
                    return;
                }
            }
            return; // Never fall through to single legacy packet if mlen >= minBatchSize
        } else if (mlen >= sizeof(SensorBinaryPacket)) {
            // 2. Fallback check for single legacy SensorBinaryPacket only for short advertisements
            size_t maxOffset = (mlen > sizeof(SensorBinaryPacket)) ? min((size_t)4, mlen - sizeof(SensorBinaryPacket)) : 0;
            for (size_t offset = 0; offset <= maxOffset; offset++) {
                SensorBinaryPacket pkt;
                memcpy(&pkt, mptr + offset, sizeof(pkt));
                if (IngestionPipeline::ingestSingle(pkt, mac, rssi, "BLE")) {
                    bleRxCount = bleRxCount + 1;
                    break;
                }
            }
        }
    }
};

static BLEReceiverCallbacks s_bleScanCallbacks;

BLEReceiver::BLEReceiver() {}

void BLEReceiver::onRadioPowerDown() {
    NimBLEScan* pScan = NimBLEDevice::getScan();
    if (pScan && pScan->isScanning()) {
        pScan->stop();
    }
}

void BLEReceiver::onRadioPowerUp() {
    if (!NimBLEDevice::isInitialized()) {
        NimBLEDevice::init("MAG_GATEWAY");
    }
    NimBLEDevice::setPower(9);
    NimBLEScan* pScan = NimBLEDevice::getScan();
    pScan->setScanCallbacks(&s_bleScanCallbacks);
    pScan->setDuplicateFilter(false);
    pScan->setActiveScan(true);
    pScan->setInterval(50);
    pScan->setWindow(50);
#if CONFIG_BT_NIMBLE_EXT_ADV
    pScan->setPhy(NimBLEScan::Phy::SCAN_CODED);
#endif
}

void BLEReceiver::startScanning() {
    NimBLEScan* pScan = NimBLEDevice::getScan();
    if (pScan && !pScan->isScanning()) {
        pScan->start(0, false);
    }
}

void BLEReceiver::stopScanning() {
    NimBLEScan* pScan = NimBLEDevice::getScan();
    if (pScan && pScan->isScanning()) {
        pScan->stop();
    }
}

bool BLEReceiver::isScanning() {
    NimBLEScan* pScan = NimBLEDevice::getScan();
    return pScan && pScan->isScanning();
}

void BLEReceiver::begin() {
#if defined(ESP_PLATFORM)
    if (esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_INITED) {
        esp_bt_controller_enable(ESP_BT_MODE_BLE);
    }
#endif
    PowerManager::begin(onRadioPowerDown, onRadioPowerUp);
    onRadioPowerUp();

    _scheduler.init(startScanning, stopScanning, isScanning);
    _scheduler.begin();

    Serial.println(F("[BLE RECEIVER SUCCESS] Active LE Coded PHY 100% duty cycle scan enabled."));
}

void BLEReceiver::poll() {
    _scheduler.poll();
}

void BLEReceiver::setRendezvousEnabled(bool enabled) {
    _scheduler.setRendezvousEnabled(enabled);
}

bool BLEReceiver::isRendezvousEnabled() const {
    return _scheduler.isRendezvousEnabled();
}
