#ifndef LORA_RECEIVER_H
#define LORA_RECEIVER_H

#include <Arduino.h>
#include "ITelemetryReceiver.h"
#include "TelemetryPacket.h"
#include "ReceiverContext.h"
#include "NodeTracker.h"
#include "IngestionPipeline.h"
#include "../../lib/LoRaStream/LoRaStream.h"

class LoRaReceiver : public ITelemetryReceiver {
public:
    LoRaReceiver(int cs = 8, int irq = 14, int rst = 12, int busy = 13, int sck = 9, int miso = 11, int mosi = 10, SPIClass *spiBus = nullptr)
        : _cs(cs), _irq(irq), _rst(rst), _busy(busy), _sck(sck), _miso(miso), _mosi(mosi), _spiBus(spiBus) {}

    void begin() override {
        bool ok = loraStream.begin(_cs, _irq, _rst, _busy, _sck, _miso, _mosi, _spiBus);
        if (ok) {
            loraStream.startReceive();
            Serial.println(F("[LORA RECEIVER SUCCESS] Sub-GHz SX1262 Continuous Reception Active (868/915 MHz)"));
        }
    }

    void poll() override {
        if (!loraStream.isInitialized()) return;

        uint8_t buf[256];
        int rssi = 0;
        float snr = 0.0f;
        size_t len = loraStream.readPacket(buf, sizeof(buf) - 1, rssi, snr);
        if (len == 0) return;

        loraRxCount = loraRxCount + 1;
        // Semtech SX1262 True Signal Power formula: When SNR < 0, true RSSI = RSSI(noise floor) + SNR
        int trueRssi = (snr < 0.0f) ? (int)round((float)rssi + snr) : rssi;

        if (len == sizeof(SensorBatchPacket)) {
            constexpr uint32_t LORA_BATCH_TOA_MS = 230;
            SensorBatchPacket batch;
            memcpy(&batch, buf, sizeof(batch));
            IngestionPipeline::ingestBatch(batch, nullptr, trueRssi, "LORA_SX1262", LORA_BATCH_TOA_MS);
            return;
        } else if (len == sizeof(SensorBinaryPacket)) {
            constexpr uint32_t LORA_TOA_MS = 58;
            SensorBinaryPacket pkt;
            memcpy(&pkt, buf, sizeof(pkt));
            IngestionPipeline::ingestSingle(pkt, nullptr, trueRssi, "LORA_SX1262", LORA_TOA_MS);
            return;
        } else {
            buf[len] = '\0';
            IngestionPipeline::ingestCsv((char*)buf, nullptr, trueRssi, "LORA_SX1262");
        }
    }

    const char* getName() const override {
        return "LoRa";
    }

private:
    int _cs, _irq, _rst, _busy;
    int _sck, _miso, _mosi;
    SPIClass *_spiBus;
};

#endif // LORA_RECEIVER_H
