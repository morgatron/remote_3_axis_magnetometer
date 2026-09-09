#ifndef RENDEZVOUS_SCHEDULER_H
#define RENDEZVOUS_SCHEDULER_H

#include <Arduino.h>

/**
 * @brief Slotted Rendezvous Scheduler for ultra-low-power BLE sensor reception.
 * State machine coordinates DISCOVERY, SLEEPING, LISTENING, and PERIODIC_DISCOVERY
 * to achieve ~90% radio off-time while tracking multiple sensor cadence drifts.
 */
class RendezvousScheduler {
public:
    enum State {
        STATE_DISCOVERY = 0,
        STATE_SLEEPING,
        STATE_LISTENING,
        STATE_PERIODIC_DISCOVERY
    };

    static constexpr uint32_t DISCOVERY_DURATION_MS = 15000;
    static constexpr uint32_t LEAD_TIME_MS = 400;
    static constexpr uint32_t WINDOW_TIMEOUT_MS = 1600;
    static constexpr uint32_t PERIODIC_DISCOVERY_INTERVAL_MS = 300000; // 5 minutes
    static constexpr uint32_t PERIODIC_DISCOVERY_DURATION_MS = 12000;  // 12 seconds

    using ScanControlFn = void (*)();
    using IsScanningFn = bool (*)();

    RendezvousScheduler();

    void init(ScanControlFn startScan, ScanControlFn stopScan, IsScanningFn isScanning);
    void begin();
    void poll();
    void setRendezvousEnabled(bool enabled);
    bool isRendezvousEnabled() const;
    State getState() const;
    const char* getTargetNodeId() const;

private:
    void transitionToSleep(uint32_t now);

    State _state;
    uint32_t _stateStartMs;
    uint32_t _nextWakeMs;
    uint32_t _currentTargetMs;
    char _targetNodeId[32];
    uint32_t _lastPeriodicDiscoveryMs;
    uint8_t _missedCount;
    bool _rendezvousEnabled;

    ScanControlFn _startScan;
    ScanControlFn _stopScan;
    IsScanningFn _isScanning;
};

#endif // RENDEZVOUS_SCHEDULER_H
