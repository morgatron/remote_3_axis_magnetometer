#ifndef INGESTION_PIPELINE_H
#define INGESTION_PIPELINE_H

#include <Arduino.h>
#include "TelemetryPacket.h"

class IngestionPipeline {
public:
    using BatchCallback = void (*)(const char *nodeId, uint32_t sampleCount, int rssi);

    static void setBatchCallback(BatchCallback cb);
    static bool isKnownSensorNodeId(const char *id);
    static bool isValidSensorPacket(const SensorBinaryPacket &pkt);
    static bool isValidBatchPacket(const SensorBatchPacket &batch);

    static bool ingestBatch(const SensorBatchPacket &batch, const uint8_t *mac, int rssi,
                            const char *protocol, uint32_t transportDelayMs = 0);
    static bool ingestSingle(const SensorBinaryPacket &pkt, const uint8_t *mac, int rssi,
                             const char *protocol, uint32_t transportDelayMs = 0);
    static bool ingestCsv(const char *csvLine, const uint8_t *mac, int rssi, const char *protocol);

    static uint32_t getLastBatchRxMs();
    static const char* getLastBatchNodeId();

private:
    static volatile uint32_t _lastBatchRxMs;
    static char _lastBatchNodeId[32];
    static BatchCallback _batchCallback;
};

#endif // INGESTION_PIPELINE_H
