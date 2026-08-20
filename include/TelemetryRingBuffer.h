#ifndef TELEMETRY_RING_BUFFER_H
#define TELEMETRY_RING_BUFFER_H

#include <Arduino.h>
#include "TelemetryPacket.h"

/**
 * @brief Thread-safe, fixed-capacity ring buffer for sensor telemetry backlog.
 * Handles automatic FIFO eviction, batch packaging for BLE bursts, and ACK acknowledgments.
 */
class TelemetryRingBuffer {
public:
    static constexpr size_t CAPACITY = 600; // 10 minutes backlog @ 1 Hz

    TelemetryRingBuffer() : _head(0), _tail(0), _unackedCount(0) {
        memset(_buffer, 0, sizeof(_buffer));
    }

    /**
     * @brief Push a new 3-axis magnetometer sample into the buffer.
     */
    void push(uint32_t ts_ms, float x_nT, float y_nT, float z_nT) {
        _buffer[_head].ts_ms = ts_ms;
        _buffer[_head].x_nT = x_nT;
        _buffer[_head].y_nT = y_nT;
        _buffer[_head].z_nT = z_nT;

        _head = (_head + 1) % CAPACITY;
        if (_unackedCount < CAPACITY) {
            _unackedCount++;
        } else {
            // Buffer full: evict oldest un-ACKed sample
            _tail = (_tail + 1) % CAPACITY;
        }
    }

    /**
     * @brief Package un-ACKed backlog into a SensorBatchPacket.
     * @param outBatch Target batch structure.
     * @param deviceId Canonical node ID string.
     * @param maxBatchSize Maximum samples per burst (e.g. 10).
     * @return Number of samples packaged into outBatch.
     */
    uint8_t getBatch(SensorBatchPacket &outBatch, const char* deviceId, uint8_t maxBatchSize) const {
        if (_unackedCount == 0) return 0;
        uint8_t count = min((uint16_t)maxBatchSize, _unackedCount);

        memset(&outBatch, 0, sizeof(outBatch));
        if (deviceId && deviceId[0] != '\0') {
            strncpy(outBatch.device_id, deviceId, sizeof(outBatch.device_id) - 1);
        }
        size_t newest_idx = (_tail + count - 1) % CAPACITY;
        uint32_t latest_ts_ms = _buffer[newest_idx].ts_ms;
        uint32_t now_ms = millis();
        outBatch.latest_sample_age_ms = (now_ms >= latest_ts_ms) ? (now_ms - latest_ts_ms) : 0;
        outBatch.sample_interval_ms = 1000;
        outBatch.sample_count = count;

        for (uint8_t i = 0; i < count; i++) {
            size_t idx = (_tail + i) % CAPACITY;
            outBatch.samples[i].x_nT = _buffer[idx].x_nT;
            outBatch.samples[i].y_nT = _buffer[idx].y_nT;
            outBatch.samples[i].z_nT = _buffer[idx].z_nT;
        }
        return count;
    }

    /**
     * @brief Advance ring tail after receiving hardware AUX_SCAN_REQ ACK.
     * @param count Number of confirmed samples.
     */
    void confirmAck(uint8_t count) {
        if (count == 0 || _unackedCount == 0) return;
        uint8_t acked = min((uint16_t)count, _unackedCount);
        _tail = (_tail + acked) % CAPACITY;
        _unackedCount -= acked;
    }

    /**
     * @brief Get current un-ACKed sample backlog count.
     */
    uint16_t getUnackedCount() const {
        return _unackedCount;
    }

    /**
     * @brief Reset ring buffer state.
     */
    void clear() {
        _head = 0;
        _tail = 0;
        _unackedCount = 0;
    }

private:
    struct Sample {
        uint32_t ts_ms;
        float x_nT;
        float y_nT;
        float z_nT;
    };

    Sample _buffer[CAPACITY];
    size_t _head;
    size_t _tail;
    uint16_t _unackedCount;
};

#endif // TELEMETRY_RING_BUFFER_H
