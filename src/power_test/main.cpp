#include <Arduino.h>
#include <WiFi.h>
#include <NimBLEDevice.h>
#include "esp_bt.h"
#include "TelemetryPacket.h"

static const int USER_LED_PIN = 15;

void broadcastBatchBurst(uint32_t burstMs) {
    Serial.println("\n=======================================================");
    Serial.println(">>> [BURST START] Restoring CPU to 80 MHz & Starting BLE Modem");
    Serial.println("=======================================================");

    setCpuFrequencyMhz(80);

    if (esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_INITED) {
        esp_bt_controller_enable(ESP_BT_MODE_BLE);
    }

    NimBLEDevice::init("CREEK");
    NimBLEDevice::setPower(15);

    NimBLEExtAdvertising* pAdvertising = NimBLEDevice::getAdvertising();
    if (pAdvertising) {
        SensorBatchPacket batch;
        memset(&batch, 0, sizeof(batch));
        strncpy(batch.device_id, "CREEK", sizeof(batch.device_id) - 1);
        batch.latest_sample_age_ms = 0;
        batch.sample_interval_ms = 1000;
        batch.sample_count = 10;
        batch.status = 0xC001;
        batch.vbat_mv = 4050;
        for (int i = 0; i < 10; i++) {
            batch.samples[i].x_nT = 123.4f + i;
            batch.samples[i].y_nT = -456.7f + i;
            batch.samples[i].z_nT = 789.0f + i;
        }

        NimBLEExtAdvertisement advData;
        advData.setLegacyAdvertising(false);
        advData.setConnectable(false);
        advData.setScannable(true);
        advData.enableScanRequestCallback(true);
        advData.setPrimaryPhy(BLE_HCI_LE_PHY_CODED);
        advData.setSecondaryPhy(BLE_HCI_LE_PHY_CODED);
        advData.setManufacturerData((const uint8_t*)&batch, sizeof(batch));
        advData.setMinInterval(32); // 20 ms
        advData.setMaxInterval(48); // 30 ms

        pAdvertising->setInstanceData(0, advData);
        pAdvertising->start(0);

        Serial.printf(">>> [BURST ACTIVE] Broadcasting Coded PHY batch for %u ms (CPU: %u MHz)...\n",
                      burstMs, (unsigned int)getCpuFrequencyMhz());
        Serial.flush();
        delay(burstMs);
        pAdvertising->stop(0);
    }

    Serial.println(">>> [BURST STOP] Deinitializing NimBLE & Powering Down BT Baseband...");
    NimBLEDevice::deinit(true);
    if (esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_ENABLED) {
        esp_bt_controller_disable();
    }

    Serial.println(">>> [IDLE START] Lowering CPU Frequency to 40 MHz...");
    setCpuFrequencyMhz(40);
    Serial.printf("[STATUS] CPU: %u MHz | BT Controller: %d (0=IDLE, 1=DISABLED)\n",
                  (unsigned int)getCpuFrequencyMhz(), (int)esp_bt_controller_get_status());
    Serial.printf("[STATUS] Free Heap: %u bytes\n", (unsigned int)ESP.getFreeHeap());
    Serial.flush();
}

void setup() {
    pinMode(USER_LED_PIN, OUTPUT);
    digitalWrite(USER_LED_PIN, HIGH);

    setCpuFrequencyMhz(80);

    Serial.begin(921600);
    uint32_t startWait = millis();
    while (!Serial && (millis() - startWait < 3000)) {
        delay(10);
    }

    Serial.println("\n#######################################################");
    Serial.println("   ESP32-C6 POWER PROFILING: REAL BURST CYCLE TEST     ");
    Serial.println("   Burst: 1.0s BLE Coded PHY Batch @ 80 MHz (~39 mA)   ");
    Serial.println("   Idle:  9.0s Modem OFF @ 40 MHz (~11 mA)             ");
    Serial.println("#######################################################");

    WiFi.mode(WIFI_OFF);
}

void loop() {
    // 1. Transmit 1.0s batch burst at 80 MHz
    broadcastBatchBurst(1000);

    // 2. Idle for 9.0s at 40 MHz with modem completely off
    for (int sec = 9; sec >= 1; sec--) {
        Serial.printf("[IDLE 40 MHz | MODEM OFF] %d s remaining...\n", sec);
        Serial.flush();
        delay(1000);
    }
}
