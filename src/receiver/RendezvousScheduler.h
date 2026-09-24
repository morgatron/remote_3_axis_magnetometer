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

    static constexpr uint32_t DISCOVERY_DURATION_MS = 14000;          // 14s active discovery scan window (Stage 1)
    static constexpr uint32_t DISCOVERY_STAGE1_SLEEP_MS = 6000;       // 6s sleep in Stage 1 (active search)
    static constexpr uint32_t DISCOVERY_STAGE1_TIMEOUT_MS = 90000;    // 90s timeout before transitioning to Stage 2 backoff
    static constexpr uint32_t DISCOVERY_STAGE2_SCAN_MS = 12000;       // 12s scan window during prolonged outages
    static constexpr uint32_t DISCOVERY_STAGE2_SLEEP_MS = 48000;      // 48s sleep during prolonged outages (20% duty cycle, battery-safe)
    static constexpr uint32_t LEAD_TIME_MS = 350;                     // 350ms lead time before expected burst (absorbs ±3.5% crystal skew)
    static constexpr uint32_t WINDOW_TIMEOUT_MS = 1200;               // 1200ms fallback timeout (shuts down early upon RX)
    static constexpr uint32_t PERIODIC_DISCOVERY_INTERVAL_MS = 600000;// 10 minutes periodic lookout
    static constexpr uint32_t PERIODIC_DISCOVERY_DURATION_MS = 8000;  // 8s periodic lookout scan

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
    uint32_t _discoveryStartMs;
    char _targetNodeId[32];
    uint32_t _lastPeriodicDiscoveryMs;
    uint8_t _missedCount;
    bool _rendezvousEnabled;

    ScanControlFn _startScan;
    ScanControlFn _stopScan;
    IsScanningFn _isScanning;
};

#endif // RENDEZVOUS_SCHEDULER_H
