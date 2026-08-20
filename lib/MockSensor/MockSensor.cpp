#include "MockSensor.h"
#include <math.h>

// Status word indicating synthetic mock telemetry (Bit 23 set + ASCII 'M')
#define MOCK_STATUS_WORD 0x804D4F // 0x80'M''O'

MockSensor::MockSensor() : _enabled(false), _lastSampleTimeUs(0), _phase(0.0f) {}

MockSensor::~MockSensor() {}

bool MockSensor::begin() {
    _ringHead = 0;
    _ringTail = 0;
    _phase = 0.0f;
    _lastSampleTimeUs = micros();
    return true;
}

bool MockSensor::dataReady() {
    if (!_enabled) return false;
    uint64_t now = micros();
    if (now - _lastSampleTimeUs >= _sampleIntervalUs) {
        return true;
    }
    return false;
}

void MockSensor::readXYZ(int32_t &x, int32_t &y, int32_t &z) {
    uint32_t unusedStatus;
    readXYZ(x, y, z, unusedStatus);
}

void MockSensor::readXYZ(int32_t &x, int32_t &y, int32_t &z, uint32_t &status) {
    float noiseX = (float)random(-1500, 1600) / 100.0f;
    float noiseY = (float)random(-1500, 1600) / 100.0f;
    float noiseZ = (float)random(-1500, 1600) / 100.0f;
    x = (int32_t)((21500.0f + 250.0f * sinf(_phase) + noiseX) * 100.0f);
    y = (int32_t)((-3200.0f + 180.0f * cosf(_phase * 0.7f) + noiseY) * 100.0f);
    z = (int32_t)((43200.0f + 120.0f * sinf(_phase * 1.3f) + noiseZ) * 100.0f);
    status = MOCK_STATUS_WORD;
}

void MockSensor::setContinuousMode(bool enable, uint8_t rate_code) {
    _enabled = enable;
    switch (rate_code) {
        case 0x95: _sampleIntervalUs = 13333; break; // 75 Hz
        case 0x94: _sampleIntervalUs = 6666;  break; // 150 Hz
        case 0x93: _sampleIntervalUs = 3333;  break; // 300 Hz
        case 0x92: _sampleIntervalUs = 1666;  break; // 600 Hz
        default:   _sampleIntervalUs = 13333; break; // Default 75 Hz
    }
    _lastSampleTimeUs = micros();
}

String MockSensor::getStatusString() {
    return "MOCK_SENSOR_ACTIVE (Status: 0x80MOCK, Mode: Range Test Simulation)";
}

void MockSensor::generateSample() {
    uint64_t now = micros();
    _lastSampleTimeUs = now;

    // Generate realistic 3-axis geomagnetic field components (in nT with 0.01 nT precision)
    // Base Earth field ~ [21,500 nT, -3,200 nT, 43,200 nT] + sine wave + noise
    float noiseX = (float)random(-1500, 1600) / 100.0f;
    float noiseY = (float)random(-1500, 1600) / 100.0f;
    float noiseZ = (float)random(-1500, 1600) / 100.0f;

    int32_t xVal = (int32_t)((21500.0f + 250.0f * sinf(_phase) + noiseX) * 100.0f);
    int32_t yVal = (int32_t)((-3200.0f + 180.0f * cosf(_phase * 0.7f) + noiseY) * 100.0f);
    int32_t zVal = (int32_t)((43200.0f + 120.0f * sinf(_phase * 1.3f) + noiseZ) * 100.0f);

    _phase += 0.05f;
    if (_phase > 2.0f * M_PI) {
        _phase -= 2.0f * M_PI;
    }

    size_t nextHead = (_ringHead + 1) % RING_BUFFER_SIZE;
    if (nextHead != _ringTail) { // Prevent ring buffer overflow
        _ringBuffer[_ringHead].ts = now;
        _ringBuffer[_ringHead].x = xVal;
        _ringBuffer[_ringHead].y = yVal;
        _ringBuffer[_ringHead].z = zVal;
        _ringBuffer[_ringHead].status = MOCK_STATUS_WORD;
        _ringHead = nextHead;
    }
}

void MockSensor::readAndPushSample() {
    generateSample();
}

bool MockSensor::popSample(ADCSample &sample) {
    if (_ringHead == _ringTail) {
        return false; // Buffer empty
    }
    sample = _ringBuffer[_ringTail];
    _ringTail = (_ringTail + 1) % RING_BUFFER_SIZE;
    return true;
}

bool MockSensor::isBufferEmpty() const {
    return (_ringHead == _ringTail);
}
