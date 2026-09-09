#ifndef POWER_MANAGER_H
#define POWER_MANAGER_H

#include <Arduino.h>

/**
 * @brief Centralized Power & Clock Manager for Receiver.
 * Controls Dynamic Frequency Scaling (DFS: 40 MHz sleep / 80 MHz active),
 * Bluetooth controller radio power gating, and provides an ActivityLock
 * to prevent sleep while egress transmission or queue draining is underway.
 */
class PowerManager {
public:
    using PowerCallback = void (*)();
    using SleepPredicate = bool (*)();

    static void begin(PowerCallback onPowerDown = nullptr, PowerCallback onPowerUp = nullptr);
    static void setOnPowerDown(PowerCallback cb);
    static void setOnPowerUp(PowerCallback cb);
    static void setSleepPredicate(SleepPredicate pred);

    static void acquireLock();
    static void releaseLock();
    static bool isLocked();
    static bool canSleep();

    static void powerDownRadio();
    static void powerUpRadio();
    static bool isRadioPoweredDown();

    static void setDfsEnabled(bool enabled);
    static bool isDfsEnabled();

private:
    static volatile uint32_t _lockCount;
    static volatile bool _radioPoweredDown;
    static PowerCallback _onPowerDown;
    static PowerCallback _onPowerUp;
    static SleepPredicate _sleepPredicate;
};

#endif // POWER_MANAGER_H
