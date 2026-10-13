#include "SensorManager.h"
#include "TelemetryEgress.h"
#include "board_config.h"
#include <SPI.h>
#include <esp_timer.h>

static SensorManager* s_sensorMgrInstance = nullptr;
static TaskHandle_t s_adcTaskHandle = nullptr;
static volatile uint64_t s_lastDrdyTimeUs = 0;
static volatile uint32_t s_lastDrdyIntervalUs = 0;
static volatile uint32_t s_drdyAnomalyCount = 0;
static volatile bool s_drdyInterruptFlag = false;

static void IRAM_ATTR drdyISR() {
    uint64_t now = esp_timer_get_time();
    if (s_lastDrdyTimeUs > 0) {
        uint32_t dt = (uint32_t)(now - s_lastDrdyTimeUs);
        s_lastDrdyIntervalUs = dt;
        if (dt < 800 || dt > 1200) {
            s_drdyAnomalyCount = s_drdyAnomalyCount + 1;
        }
    }
    s_lastDrdyTimeUs = now;
    s_drdyInterruptFlag = true;

    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    if (s_adcTaskHandle != NULL) {
        vTaskNotifyGiveFromISR(s_adcTaskHandle, &xHigherPriorityTaskWoken);
        if (xHigherPriorityTaskWoken) {
            portYIELD_FROM_ISR();
        }
    }
}

static void adcSamplingTask(void *pvParameters) {
    SensorManager* mgr = (SensorManager*)pvParameters;
    for (;;) {
        if (mgr->getConfig().sensorType == SENSOR_TYPE_MOCK) {
            vTaskDelay(pdMS_TO_TICKS(1000)); // 1 Hz tick for MockSensor
            if (mgr->getConfig().streaming && mgr->getActiveSensor() != NULL) {
                mgr->getActiveSensor()->readAndPushSample();
            }
        } else {
            // Wait for DRDY ISR notification, with a 13ms (~75 Hz) fallback timeout
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(13));
            if (mgr->getConfig().streaming && mgr->getActiveSensor() != NULL) {
                mgr->getActiveSensor()->readAndPushSample();
            }
        }
    }
}

SensorManager::SensorManager(SensorConfig& config)
    : _config(config),
      _sensorRM3100(CS_PIN, DRDY_PIN),
      _sensorFLC100(CS_PIN, DRDY_PIN, RESET_PIN),
      _sensorMock(),
      _activeSensor(&_sensorFLC100) {}

bool SensorManager::probeFLC100(int retries) {
    for (int retry = 0; retry < retries; retry++) {
        if (_sensorFLC100.begin()) {
            return true;
        }
        delay(200);
    }
    return false;
}

bool SensorManager::probeRM3100(int retries) {
    for (int retry = 0; retry < retries; retry++) {
        if (_sensorRM3100.begin()) {
            return true;
        }
        delay(150);
    }
    return false;
}

void SensorManager::attachDrdyInterrupt() {
    if (_config.sensorType == SENSOR_TYPE_FLC100) {
        attachInterrupt(digitalPinToInterrupt(DRDY_PIN), drdyISR, FALLING);
    } else if (_config.sensorType == SENSOR_TYPE_RM3100) {
        attachInterrupt(digitalPinToInterrupt(DRDY_PIN), drdyISR, RISING);
    } else {
        detachInterrupt(digitalPinToInterrupt(DRDY_PIN));
    }
}

bool SensorManager::begin() {
    s_sensorMgrInstance = this;

    // Provide virtual ground on GPIO 5 and GPIO 1 for easy RM3100 I2CEN grounding
#if defined(CONFIG_IDF_TARGET_ESP32C3) || defined(ARDUINO_ARCH_ESP32C3)
    pinMode(5, OUTPUT);
    digitalWrite(5, LOW);
    pinMode(1, OUTPUT);
    digitalWrite(1, LOW);
#endif

    // ALWAYS initialize SPI bus and DRDY pin regardless of initial configured sensor
    Serial.println("Initializing SPI Bus...");
    SPI.begin(SCK_PIN, MISO_PIN, MOSI_PIN, CS_PIN);
    pinMode(DRDY_PIN, INPUT_PULLUP);

    if (_config.sensorType == SENSOR_TYPE_FLC100) {
        Serial.println("Configured sensor: FLC100-ADS131E08. Probing hardware...");
        if (probeFLC100()) {
            _activeSensor = &_sensorFLC100;
            static_cast<FLC100_ADS131*>(_activeSensor)->setCalibration(2.4f, 20.0f, 1);
            attachDrdyInterrupt();
            Serial.println("Sensor initialized: FLC100-ADS131E08");
        } else {
            Serial.println("FLC100 not detected. Cross-probing RM3100...");
            if (probeRM3100()) {
                _config.sensorType = SENSOR_TYPE_RM3100;
                _activeSensor = &_sensorRM3100;
                _config.downsample = 1;
                static_cast<RM3100*>(_activeSensor)->setCycleCount(_config.cycleCount, _config.cycleCount, _config.cycleCount);
                attachDrdyInterrupt();
                Serial.printf("Auto-detected sensor: RM3100 (Cycle count: %u)\r\n", _config.cycleCount);
            } else {
                Serial.println("[ERROR] Physical sensors did not respond! Falling back to MOCK for this session.");
                _config.sensorType = SENSOR_TYPE_MOCK;
                _activeSensor = &_sensorMock;
                _activeSensor->begin();
            }
        }
    } else if (_config.sensorType == SENSOR_TYPE_RM3100) {
        Serial.println("Configured sensor: RM3100. Probing hardware...");
        if (probeRM3100()) {
            _activeSensor = &_sensorRM3100;
            static_cast<RM3100*>(_activeSensor)->setCycleCount(_config.cycleCount, _config.cycleCount, _config.cycleCount);
            attachDrdyInterrupt();
            Serial.printf("Sensor initialized: RM3100 (Cycle count: %u)\r\n", _config.cycleCount);
        } else {
            Serial.println("RM3100 not detected. Cross-probing FLC100...");
            if (probeFLC100()) {
                _config.sensorType = SENSOR_TYPE_FLC100;
                _activeSensor = &_sensorFLC100;
                _config.downsample = 1000;
                static_cast<FLC100_ADS131*>(_activeSensor)->setCalibration(2.4f, 20.0f, 1);
                attachDrdyInterrupt();
                Serial.println("Auto-detected sensor: FLC100-ADS131E08");
            } else {
                Serial.println("[ERROR] Physical sensors did not respond! Falling back to MOCK for this session.");
                _config.sensorType = SENSOR_TYPE_MOCK;
                _activeSensor = &_sensorMock;
                _activeSensor->begin();
            }
        }
    } else if (_config.sensorType == SENSOR_TYPE_MOCK) {
        _activeSensor = &_sensorMock;
        _activeSensor->begin();
        Serial.println("[MOCK MODE] Explicit Synthetic Sensor Mode Active (Range Testing)");
        Serial.println("Status Header: 0x80MOCK");
    } else {
        // SENSOR_TYPE_AUTO (255)
        Serial.println("Auto-probing physical sensor hardware...");
        if (probeFLC100()) {
            _activeSensor = &_sensorFLC100;
            _config.sensorType = SENSOR_TYPE_FLC100;
            static_cast<FLC100_ADS131*>(_activeSensor)->setCalibration(2.4f, 20.0f, 1);
            attachDrdyInterrupt();
            Serial.println("Auto-detected sensor: FLC100-ADS131E08");
            _config.save();
        } else if (probeRM3100()) {
            _activeSensor = &_sensorRM3100;
            _config.sensorType = SENSOR_TYPE_RM3100;
            static_cast<RM3100*>(_activeSensor)->setCycleCount(200, 200, 200);
            attachDrdyInterrupt();
            Serial.println("Auto-detected sensor: RM3100");
            _config.save();
        } else {
            Serial.print("[SENSOR NOTICE] No physical SPI sensor detected. Falling back to Synthetic MOCK mode.\r\n");
            Serial.print("[HINT] Connect RM3100 or FLC100-ADS131E08 and reboot, or send 'SENSOR RM3100' / 'SENSOR FLC100'.\r\n");
            _activeSensor = &_sensorMock;
            _config.sensorType = SENSOR_TYPE_MOCK;
            _activeSensor->begin();
        }
    }

    Serial.print("Active Sensor: ");
    Serial.println(_activeSensor->getSensorName());
    Serial.println(_activeSensor->getStatusString());

    // Resume continuous mode with saved rate
    _activeSensor->setContinuousMode(true, _config.rate);
    s_lastDrdyTimeUs = esp_timer_get_time();
    if (_activeSensor != nullptr) {
        _activeSensor->readAndPushSample(); // Clear pending DRDY latch on ASIC
    }

    // Create high-priority task for immediate ISR-notified ADC sampling
#if CONFIG_FREERTOS_UNICORE
    xTaskCreatePinnedToCore(adcSamplingTask, "ADC_Task", 4096, this, 10, &s_adcTaskHandle, 0);
#else
    xTaskCreatePinnedToCore(adcSamplingTask, "ADC_Task", 4096, this, configMAX_PRIORITIES - 1, &s_adcTaskHandle, 1);
#endif

    return true;
}

void SensorManager::recoverSensor() {
    Serial.println("\r\n[WATCHDOG STALL DETECTED] No DRDY interrupt for >500ms! Recovering sensor...");
    detachInterrupt(digitalPinToInterrupt(DRDY_PIN));

    pinMode(CS_PIN, OUTPUT);
    digitalWrite(CS_PIN, HIGH);

    if (_activeSensor != nullptr) {
        _activeSensor->setContinuousMode(true, _config.rate);
        _activeSensor->readAndPushSample(); // Clear pending DRDY latch on ASIC
    }

    s_lastDrdyTimeUs = esp_timer_get_time();
    attachDrdyInterrupt();
    Serial.println("[WATCHDOG RECOVERY] Sensor streaming recovered successfully.\r\n");
}

void SensorManager::checkWatchdog() {
    uint32_t nowWdMs = millis();
    if (_config.streaming && _activeSensor != nullptr && _config.sensorType != SENSOR_TYPE_MOCK && (nowWdMs - _lastWdCheckMs >= 500)) {
        _lastWdCheckMs = nowWdMs;
        uint64_t nowUs = esp_timer_get_time();
        if (s_lastDrdyTimeUs > 0 && (nowUs - s_lastDrdyTimeUs > 500000ULL)) {
            recoverSensor();
        }
    } else if (_config.sensorType == SENSOR_TYPE_MOCK && (nowWdMs - _lastWdCheckMs >= 3000)) {
        // If in MOCK fallback, periodically probe if physical hardware has stabilized
        _lastWdCheckMs = nowWdMs;
        if (probeFLC100(1)) {
            Serial.println("[AUTOPROBE] Physical FLC100-ADS131E08 detected! Promoting from MOCK...");
            switchSensor(SENSOR_TYPE_FLC100);
        } else if (probeRM3100(1)) {
            Serial.println("[AUTOPROBE] Physical RM3100 detected! Promoting from MOCK...");
            switchSensor(SENSOR_TYPE_RM3100);
        }
    }
}

bool SensorManager::switchSensor(uint8_t newType) {
    _config.sensorType = newType;
    if (newType == SENSOR_TYPE_RM3100) {
        _activeSensor = &_sensorRM3100;
        _config.rate = 0x96; // Default RM3100 rate 37 Hz
    } else if (newType == SENSOR_TYPE_FLC100) {
        _activeSensor = &_sensorFLC100;
        _config.rate = 0x06; // Default ADS131 rate 1 kSPS
        if (_config.downsample <= 1) _config.downsample = 1000;
    } else {
        _activeSensor = &_sensorMock;
        _config.rate = 0x95; // Default Mock rate 75 Hz
    }

    _sumX = 0.0f;
    _sumY = 0.0f;
    _sumZ = 0.0f;
    _decimationCounter = 0;

    Serial.print("Active sensor set to: ");
    Serial.println(_activeSensor->getSensorName());

    // Ensure SPI bus is active
    SPI.begin(SCK_PIN, MISO_PIN, MOSI_PIN, CS_PIN);
    pinMode(DRDY_PIN, INPUT_PULLUP);

    attachDrdyInterrupt();

    if (!_activeSensor->begin()) {
        Serial.println("Warning: Selected sensor failed initialization!");
        return false;
    }

    if (newType == SENSOR_TYPE_FLC100) {
        static_cast<FLC100_ADS131*>(_activeSensor)->setCalibration(2.4f, 20.0f, 1);
    } else if (newType == SENSOR_TYPE_RM3100) {
        static_cast<RM3100*>(_activeSensor)->setCycleCount(_config.cycleCount, _config.cycleCount, _config.cycleCount);
    }

    _activeSensor->setContinuousMode(true, _config.rate);
    s_lastDrdyTimeUs = esp_timer_get_time();
    if (_activeSensor != nullptr) {
        _activeSensor->readAndPushSample(); // Clear pending DRDY latch on ASIC
    }
    Serial.println("Sensor initialized and continuous mode started.");
    _config.save();
    return true;
}

bool SensorManager::switchSensorByName(const String& sub) {
    if (sub == "RM3100" || sub == "0") {
        return switchSensor(SENSOR_TYPE_RM3100);
    } else if (sub == "FLC100" || sub == "ADS131" || sub == "1") {
        return switchSensor(SENSOR_TYPE_FLC100);
    } else if (sub == "MOCK" || sub == "2") {
        return switchSensor(SENSOR_TYPE_MOCK);
    }
    Serial.println("Unknown sensor type. Options: RM3100, FLC100, MOCK");
    return false;
}

bool SensorManager::setCycleCount(uint16_t count) {
    if (_activeSensor != nullptr && _activeSensor->getSensorName().indexOf("RM3100") >= 0 && count > 0) {
        static_cast<RM3100*>(_activeSensor)->setCycleCount(count, count, count);
        return true;
    }
    return false;
}

bool SensorManager::setPgaGain(uint8_t gain) {
    if (_activeSensor != nullptr && _activeSensor->getSensorName().indexOf("FLC100") >= 0) {
        static_cast<FLC100_ADS131*>(_activeSensor)->setCalibration(2.4f, 20.0f, gain);
        return true;
    }
    return false;
}

bool SensorManager::setVref(float vref) {
    if (_activeSensor != nullptr && _activeSensor->getSensorName().indexOf("FLC100") >= 0 && vref > 0.0f) {
        static_cast<FLC100_ADS131*>(_activeSensor)->setCalibration(vref, 20.0f, 1);
        return true;
    }
    return false;
}

bool SensorManager::setTestSignal(bool enable) {
    if (_activeSensor != nullptr && _activeSensor->getSensorName().indexOf("FLC100") >= 0) {
        static_cast<FLC100_ADS131*>(_activeSensor)->setTestSignal(enable);
        return true;
    }
    return false;
}

void SensorManager::processSamples(TelemetryEgress& telemetry) {
    if (!_config.streaming || _activeSensor == nullptr) return;

    float scaleFactor = _activeSensor->getScaleFactor();
    ADCSample sample;
    while (_activeSensor->popSample(sample)) {
        float x = (float)sample.x * scaleFactor;
        float y = (float)sample.y * scaleFactor;
        float z = (float)sample.z * scaleFactor;
        uint32_t status = sample.status;
        uint64_t ts = sample.ts;

        uint16_t decimationFactor = (_config.sensorType == SENSOR_TYPE_MOCK) ? 1 : _config.downsample;
        if (decimationFactor <= 1) {
            // Direct streaming without averaging
            telemetry.sendSample(ts, x, y, z, status);
        } else {
            _sumX += x;
            _sumY += y;
            _sumZ += z;
            _decimationCounter++;

            if (_decimationCounter >= decimationFactor) {
                float avgX = _sumX / (float)_decimationCounter;
                float avgY = _sumY / (float)_decimationCounter;
                float avgZ = _sumZ / (float)_decimationCounter;

                _sumX = 0.0f;
                _sumY = 0.0f;
                _sumZ = 0.0f;
                _decimationCounter = 0;

                telemetry.sendSample(ts, avgX, avgY, avgZ, status);
            }
        }
    }
}
