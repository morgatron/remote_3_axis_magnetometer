#include "IngestionPipeline.h"
#include "ReceiverContext.h"
#include "NodeTracker.h"

volatile uint32_t IngestionPipeline::_lastBatchRxMs = 0;
char IngestionPipeline::_lastBatchNodeId[32] = {0};
IngestionPipeline::BatchCallback IngestionPipeline::_batchCallback = nullptr;

void IngestionPipeline::setBatchCallback(BatchCallback cb) {
    _batchCallback = cb;
}

bool IngestionPipeline::isKnownSensorNodeId(const char *id) {
    if (!id || id[0] == '\0') return false;
    size_t len = 0;
    for (size_t i = 0; i < 16 && id[i] != '\0'; i++) {
        char c = id[i];
        if (!isalnum((unsigned char)c) && c != '_' && c != '-') {
            return false;
        }
        len++;
    }
    return len >= 3;
}

bool IngestionPipeline::isValidSensorPacket(const SensorBinaryPacket &pkt) {
    if (!isKnownSensorNodeId(pkt.device_id)) return false;
    if (pkt.packet_age_ms > 600000) return false; // Age <= 10 min
    if (isnan(pkt.x_nT) || isnan(pkt.y_nT) || isnan(pkt.z_nT)) return false;
    if (fabsf(pkt.x_nT) > 10000000.0f || fabsf(pkt.y_nT) > 10000000.0f || fabsf(pkt.z_nT) > 10000000.0f) return false;
    return true;
}

bool IngestionPipeline::isValidBatchPacket(const SensorBatchPacket &batch) {
    if (!isKnownSensorNodeId(batch.device_id)) return false;
    if (batch.sample_count == 0 || batch.sample_count > 18) return false;
    if (batch.sample_interval_ms != 1000) return false; // 1 Hz nominal
    if (batch.latest_sample_age_ms > 600000) return false; // Age <= 10 min
    if (batch.vbat_mv > 5500) return false;
    for (uint8_t i = 0; i < batch.sample_count; i++) {
        if (isnan(batch.samples[i].x_nT) || isnan(batch.samples[i].y_nT) || isnan(batch.samples[i].z_nT)) return false;
        if (fabsf(batch.samples[i].x_nT) > 10000000.0f || fabsf(batch.samples[i].y_nT) > 10000000.0f || fabsf(batch.samples[i].z_nT) > 10000000.0f) return false;
    }
    return true;
}

bool IngestionPipeline::ingestBatch(const SensorBatchPacket &batch, const uint8_t *mac, int rssi,
                                    const char *protocol, uint32_t transportDelayMs) {
    if (!isValidBatchPacket(batch)) return false;

    TelemetryItem item;
    memset(&item, 0, sizeof(item));
    if (mac) memcpy(item.mac, mac, 6);
    item.rssi = rssi;
    strncpy(item.protocol, protocol, sizeof(item.protocol) - 1);

    if (batch.device_id[0] != '\0') {
        strncpy(item.node_id, batch.device_id, sizeof(item.node_id) - 1);
    } else if (mac) {
        snprintf(item.node_id, sizeof(item.node_id), "NODE_%02X%02X%02X", mac[3], mac[4], mac[5]);
    } else {
        strncpy(item.node_id, "UNKNOWN", sizeof(item.node_id) - 1);
    }

    item.status = batch.status;
    item.temp = (batch.temp_c_x100 == 0x7FFF) ? 0.0f : ((float)batch.temp_c_x100 / 100.0f);
    item.vbat = (float)batch.vbat_mv / 1000.0f;

    uint32_t now_ms = millis();
    uint32_t total_delay_ms = batch.latest_sample_age_ms + transportDelayMs;
    uint32_t latest_sample_ts_ms = (now_ms >= total_delay_ms) ? (now_ms - total_delay_ms) : 0;
    uint32_t oldest_sample_offset = (batch.sample_count > 0) ? (batch.sample_count - 1) * batch.sample_interval_ms : 0;
    uint32_t oldest_sample_ts_ms = (latest_sample_ts_ms >= oldest_sample_offset) ? (latest_sample_ts_ms - oldest_sample_offset) : 0;

    int32_t s0_x = (batch.sample_count > 0) ? batch.samples[0].x_nT : 0;
    int32_t s0_y = (batch.sample_count > 0) ? batch.samples[0].y_nT : 0;
    int32_t s0_z = (batch.sample_count > 0) ? batch.samples[0].z_nT : 0;

    if (!nodeTracker.recordBatchSeen(item.node_id, item.mac, item.rssi, oldest_sample_ts_ms,
                                     batch.sample_count, item.vbat, protocol, s0_x, s0_y, s0_z)) {
        return false; // Duplicate scan/burst, already recorded
    }

    _lastBatchRxMs = now_ms;
    strncpy(_lastBatchNodeId, item.node_id, sizeof(_lastBatchNodeId) - 1);

    if (_batchCallback) {
        _batchCallback(item.node_id, batch.sample_count, rssi);
    }

    // Unpack individual samples with exact reconstructed timestamps
    for (uint8_t i = 0; i < batch.sample_count; i++) {
        uint32_t offset_from_newest = (batch.sample_count - 1 - i) * batch.sample_interval_ms;
        uint32_t sample_ts_ms = (latest_sample_ts_ms >= offset_from_newest) ? (latest_sample_ts_ms - offset_from_newest) : 0;
        item.timestamp_us = (uint64_t)sample_ts_ms * 1000ULL;
        item.x = (float)batch.samples[i].x_nT;
        item.y = (float)batch.samples[i].y_nT;
        item.z = (float)batch.samples[i].z_nT;

        item.formatCsvLine();
        if (telemetryQueue) {
            xQueueSend(telemetryQueue, &item, 0);
        }
    }
    return true;
}

bool IngestionPipeline::ingestSingle(const SensorBinaryPacket &pkt, const uint8_t *mac, int rssi,
                                     const char *protocol, uint32_t transportDelayMs) {
    if (!isValidSensorPacket(pkt)) return false;

    TelemetryItem item;
    memset(&item, 0, sizeof(item));
    if (mac) memcpy(item.mac, mac, 6);
    item.rssi = rssi;
    strncpy(item.protocol, protocol, sizeof(item.protocol) - 1);

    if (pkt.device_id[0] != '\0') {
        strncpy(item.node_id, pkt.device_id, sizeof(item.node_id) - 1);
    } else if (mac) {
        snprintf(item.node_id, sizeof(item.node_id), "NODE_%02X%02X%02X", mac[3], mac[4], mac[5]);
    } else {
        strncpy(item.node_id, "UNKNOWN", sizeof(item.node_id) - 1);
    }

    uint32_t now_ms = millis();
    uint32_t total_delay_ms = pkt.packet_age_ms + transportDelayMs;
    uint32_t sample_ts_ms = (now_ms >= total_delay_ms) ? (now_ms - total_delay_ms) : 0;

    item.timestamp_us = (uint64_t)sample_ts_ms * 1000ULL;
    item.x = pkt.x_nT;
    item.y = pkt.y_nT;
    item.z = pkt.z_nT;
    item.status = pkt.status;
    item.temp = 0.0f;
    item.vbat = 0.0f;

    bool isNew = nodeTracker.recordPacket(item.node_id, item.mac, item.rssi,
                                         item.x, item.y, item.z, item.temp, item.vbat,
                                         protocol, sample_ts_ms);
    if (isNew) {
        item.formatCsvLine();
        if (telemetryQueue) {
            xQueueSend(telemetryQueue, &item, 0);
        }
        return true;
    }
    return false;
}

bool IngestionPipeline::ingestCsv(const char *csvLine, const uint8_t *mac, int rssi, const char *protocol) {
    if (!csvLine || strlen(csvLine) < 10) return false;

    TelemetryItem item;
    memset(&item, 0, sizeof(item));
    if (mac) memcpy(item.mac, mac, 6);
    item.rssi = rssi;
    strncpy(item.protocol, protocol, sizeof(item.protocol) - 1);

    if (TelemetryItem::parseCsvLine(csvLine, item)) {
        item.formatCsvLine();
    } else {
        if (mac) {
            snprintf(item.node_id, sizeof(item.node_id), "IP_%d_%d_%d_%d", mac[0], mac[1], mac[2], mac[3]);
        } else {
            strncpy(item.node_id, "RAW", sizeof(item.node_id) - 1);
        }
        snprintf(item.line, sizeof(item.line), "%s\n", csvLine);
    }

    bool isNew = nodeTracker.recordPacket(item.node_id, item.mac, item.rssi,
                                         item.x, item.y, item.z, item.temp, item.vbat, protocol);
    if (isNew) {
        if (telemetryQueue) {
            xQueueSend(telemetryQueue, &item, 0);
        }
        return true;
    }
    return false;
}

uint32_t IngestionPipeline::getLastBatchRxMs() {
    return _lastBatchRxMs;
}

const char* IngestionPipeline::getLastBatchNodeId() {
    return _lastBatchNodeId;
}
