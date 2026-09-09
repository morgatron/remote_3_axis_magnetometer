#include "PowerManager.h"
#include "ReceiverContext.h"
#if defined(ESP_PLATFORM)
#include "esp_bt.h"
#endif

volatile uint32_t PowerManager::_lockCount = 0;
volatile bool PowerManager::_radioPoweredDown = false;
PowerManager::PowerCallback PowerManager::_onPowerDown = nullptr;
PowerManager::PowerCallback PowerManager::_onPowerUp = nullptr;
PowerManager::SleepPredicate PowerManager::_sleepPredicate = nullptr;

void PowerManager::begin(PowerCallback onPowerDown, PowerCallback onPowerUp) {
    _onPowerDown = onPowerDown;
    _onPowerUp = onPowerUp;
    _radioPoweredDown = false;
    _lockCount = 0;
}

void PowerManager::setOnPowerDown(PowerCallback cb) { _onPowerDown = cb; }
void PowerManager::setOnPowerUp(PowerCallback cb) { _onPowerUp = cb; }
void PowerManager::setSleepPredicate(SleepPredicate pred) { _sleepPredicate = pred; }

void PowerManager::acquireLock() {
    _lockCount = _lockCount + 1;
}

void PowerManager::releaseLock() {
    if (_lockCount > 0) {
        _lockCount = _lockCount - 1;
    }
}

bool PowerManager::isLocked() {
    return _lockCount > 0;
}

bool PowerManager::canSleep() {
    if (_lockCount > 0) return false;
    if (telemetryQueue && uxQueueMessagesWaiting(telemetryQueue) > 0) return false;
    if (_sleepPredicate && !_sleepPredicate()) return false;
    return true;
}

void PowerManager::powerDownRadio() {
    if (_radioPoweredDown) return;

    if (_onPowerDown) {
        _onPowerDown();
    }

#if defined(ESP_PLATFORM)
    if (esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_ENABLED) {
        esp_bt_controller_disable();
    }
#endif
    _radioPoweredDown = true;
    if (g_dfsEnabled && getCpuFrequencyMhz() > 40) {
        setCpuFrequencyMhz(40);
    }
    if (g_debugScheduler) {
        Serial.printf("[POWER] Radio powered down (BLE controller disabled, CPU %d MHz).\r\n", getCpuFrequencyMhz());
    }
}

void PowerManager::powerUpRadio() {
    if (!_radioPoweredDown) return;

    if (g_dfsEnabled && getCpuFrequencyMhz() < 80) {
        setCpuFrequencyMhz(80);
    }
#if defined(ESP_PLATFORM)
    if (esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_INITED) {
        esp_bt_controller_enable(ESP_BT_MODE_BLE);
    }
#endif
    _radioPoweredDown = false;

    if (_onPowerUp) {
        _onPowerUp();
    }
    if (g_debugScheduler) {
        Serial.println(F("[POWER] Radio powered up (BLE controller enabled, CPU 80 MHz)."));
    }
}

bool PowerManager::isRadioPoweredDown() {
    return _radioPoweredDown;
}

void PowerManager::setDfsEnabled(bool enabled) {
    g_dfsEnabled = enabled;
    if (!enabled && getCpuFrequencyMhz() < 80) {
        setCpuFrequencyMhz(80);
    }
}

bool PowerManager::isDfsEnabled() {
    return g_dfsEnabled;
}
