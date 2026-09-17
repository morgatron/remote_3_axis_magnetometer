#include "RendezvousScheduler.h"
#include "ReceiverContext.h"
#include "NodeTracker.h"
#include "IngestionPipeline.h"
#include "PowerManager.h"
#include "BLEEgress.h"

#ifndef DEBUG_BLE_SCHEDULER
#define DEBUG_BLE_SCHEDULER 1
#endif

#if DEBUG_BLE_SCHEDULER
  #define SCHED_PRINTF(...) do { if (g_debugScheduler) Serial.printf(__VA_ARGS__); } while(0)
  #define SCHED_PRINTLN(...) do { if (g_debugScheduler) Serial.println(__VA_ARGS__); } while(0)
#else
  #define SCHED_PRINTF(...) ((void)0)
  #define SCHED_PRINTLN(...) ((void)0)
#endif

RendezvousScheduler::RendezvousScheduler()
    : _state(STATE_DISCOVERY),
      _stateStartMs(0),
      _nextWakeMs(0),
      _currentTargetMs(0),
      _lastPeriodicDiscoveryMs(0),
      _missedCount(0),
      _rendezvousEnabled(true),
      _startScan(nullptr),
      _stopScan(nullptr),
      _isScanning(nullptr) {
    memset(_targetNodeId, 0, sizeof(_targetNodeId));
}

void RendezvousScheduler::init(ScanControlFn startScan, ScanControlFn stopScan, IsScanningFn isScanning) {
    _startScan = startScan;
    _stopScan = stopScan;
    _isScanning = isScanning;
}

void RendezvousScheduler::begin() {
    _state = STATE_DISCOVERY;
    _stateStartMs = millis();
    _lastPeriodicDiscoveryMs = millis();
    _missedCount = 0;
    if (_startScan) _startScan();
    SCHED_PRINTLN(F("[BLE SCHEDULER] Phase 1: Initial Discovery active for 15 seconds..."));
}

void RendezvousScheduler::poll() {
    if (!_rendezvousEnabled) return;

    uint32_t now = millis();

    switch (_state) {
        case STATE_DISCOVERY: {
            if (_isScanning && !_isScanning() && _startScan) {
                SCHED_PRINTLN(F("[BLE SCHEDULER] Scanner inactive in Discovery mode, restarting..."));
                _startScan();
            }

            // Immediate exit upon valid batch capture: avoid wasting 12s of 75 mA continuous scan
            if (IngestionPipeline::getLastBatchRxMs() >= _stateStartMs && nodeTracker.getActiveNodeCount() > 0) {
                uint32_t discDuration = now - _stateStartMs;
                SCHED_PRINTF("[BLE SCHEDULER] Discovery lock: Captured batch from %s in %lu ms! Entering Slotted Rendezvous.\r\n",
                              IngestionPipeline::getLastBatchNodeId(), (unsigned long)discDuration);
                transitionToSleep(now);
                if (_state == STATE_SLEEPING) {
                    BLEEgress::broadcastDiagnostic(DIAG_EVENT_SYNC_ACQUIRED, STATE_SLEEPING, 0, _targetNodeId, (uint16_t)discDuration);
                }
                break;
            }

            if (now - _stateStartMs >= DISCOVERY_DURATION_MS) {
                if (nodeTracker.getActiveNodeCount() == 0) {
                    _stateStartMs = now;
                    SCHED_PRINTLN(F("[BLE SCHEDULER] Discovery complete: No active nodes heard yet. Continuing continuous scan..."));
                } else {
                    SCHED_PRINTF("[BLE SCHEDULER] Discovery complete: Found %d active node(s). Entering Slotted Rendezvous mode.\r\n",
                                  nodeTracker.getActiveNodeCount());
                    uint32_t discDuration = now - _stateStartMs;
                    transitionToSleep(now);
                    if (_state == STATE_SLEEPING) {
                        BLEEgress::broadcastDiagnostic(DIAG_EVENT_SYNC_ACQUIRED, STATE_SLEEPING, 0, _targetNodeId, (uint16_t)discDuration);
                    }
                }
            }
            break;
        }

        case STATE_SLEEPING: {
            if (!PowerManager::isRadioPoweredDown() && PowerManager::canSleep()) {
                PowerManager::powerDownRadio();
            }

            // Check periodic 10-minute lookout discovery
            if (now - _lastPeriodicDiscoveryMs >= PERIODIC_DISCOVERY_INTERVAL_MS) {
                PowerManager::powerUpRadio();
                _state = STATE_PERIODIC_DISCOVERY;
                _stateStartMs = now;
                _lastPeriodicDiscoveryMs = now;
                if (_startScan) _startScan();
                SCHED_PRINTLN(F("[BLE SCHEDULER] Starting Periodic Lookout Scan to discover new nodes..."));
                BLEEgress::broadcastDiagnostic(DIAG_EVENT_PERIODIC_LOOKOUT, STATE_PERIODIC_DISCOVERY, 0, "LOOKOUT", (uint16_t)PERIODIC_DISCOVERY_DURATION_MS);
                break;
            }

            // Check if time to wake for scheduled batch
            if ((long)(now - _nextWakeMs) >= 0) {
                PowerManager::powerUpRadio();
                _state = STATE_LISTENING;
                _stateStartMs = now;
                if (_startScan) _startScan();
                SCHED_PRINTF("[BLE SCHEDULER] Radio AWAKE for %s (CPU: %d MHz, burst expected in %ld ms)\r\n",
                              _targetNodeId, getCpuFrequencyMhz(), (long)(_currentTargetMs - now));
            }
            break;
        }

        case STATE_LISTENING: {
            if (IngestionPipeline::getLastBatchRxMs() >= _stateStartMs) {
                _missedCount = 0;
                uint32_t onTime = now - _stateStartMs;
                SCHED_PRINTF("[BLE SCHEDULER] Batch from %s captured in %lu ms! Stopping radio.\r\n",
                              IngestionPipeline::getLastBatchNodeId(), (unsigned long)onTime);

                uint32_t upcomingWake = 0, upcomingTarget = 0;
                char nextNode[32] = {0};
                if (nodeTracker.getNextExpectedWake(now + 100, LEAD_TIME_MS, upcomingWake, upcomingTarget, nextNode, sizeof(nextNode))) {
                    if ((long)(upcomingWake - now) <= 250) {
                        _nextWakeMs = upcomingWake;
                        _currentTargetMs = upcomingTarget;
                        strncpy(_targetNodeId, nextNode, sizeof(_targetNodeId) - 1);
                        _stateStartMs = now;
                        SCHED_PRINTF("[BLE SCHEDULER] Staying awake for adjacent node %s\r\n", _targetNodeId);
                        break;
                    }
                }

                transitionToSleep(now);
                break;
            }

            uint32_t activeTimeout = (_missedCount == 0) ? WINDOW_TIMEOUT_MS : (WINDOW_TIMEOUT_MS + 400);
            if (now - _stateStartMs >= activeTimeout) {
                _missedCount++;
                nodeTracker.recordMiss(_targetNodeId);
                uint32_t onTime = now - _stateStartMs;
                SCHED_PRINTF("[BLE SCHEDULER] Window timed out for %s (missed %d consecutive). Stopping radio.\r\n",
                              _targetNodeId, _missedCount);
                if (_missedCount >= 3) {
                    SCHED_PRINTLN(F("[BLE SCHEDULER] Lost sync (3 consecutive misses). Re-entering Discovery..."));
                    BLEEgress::broadcastDiagnostic(DIAG_EVENT_LOST_SYNC_DISCOVERY, STATE_DISCOVERY, _missedCount, _targetNodeId, (uint16_t)onTime);
                    PowerManager::powerUpRadio();
                    _state = STATE_DISCOVERY;
                    _stateStartMs = now;
                    _missedCount = 0;
                    if (_startScan && _isScanning && !_isScanning()) {
                        _startScan();
                    }
                } else {
                    BLEEgress::broadcastDiagnostic(DIAG_EVENT_WINDOW_TIMEOUT, STATE_SLEEPING, _missedCount, _targetNodeId, (uint16_t)onTime);
                    transitionToSleep(now);
                }
            }
            break;
        }

        case STATE_PERIODIC_DISCOVERY: {
            if (now - _stateStartMs >= PERIODIC_DISCOVERY_DURATION_MS) {
                SCHED_PRINTLN(F("[BLE SCHEDULER] Periodic Lookout complete. Resuming scheduled sleep."));
                transitionToSleep(now);
            }
            break;
        }

        default:
            PowerManager::powerUpRadio();
            _state = STATE_DISCOVERY;
            _stateStartMs = now;
            if (_startScan) _startScan();
            break;
    }
}

void RendezvousScheduler::transitionToSleep(uint32_t now) {
    if (_stopScan) _stopScan();
    _state = STATE_SLEEPING;

    uint32_t wake = 0, target = 0;
    char nextNode[32] = {0};
    if (nodeTracker.getNextExpectedWake(now, LEAD_TIME_MS, wake, target, nextNode, sizeof(nextNode))) {
        _nextWakeMs = wake;
        _currentTargetMs = target;
        strncpy(_targetNodeId, nextNode, sizeof(_targetNodeId) - 1);
        uint32_t sleepDuration = (_nextWakeMs > now) ? (_nextWakeMs - now) : 0;
        SCHED_PRINTF("[BLE SCHEDULER] Radio SLEEPING for %lu ms (next wake at %lu ms for %s)\r\n",
                      (unsigned long)sleepDuration, (unsigned long)_nextWakeMs, _targetNodeId);
    } else {
        _state = STATE_DISCOVERY;
        _stateStartMs = now;
        PowerManager::powerUpRadio();
        if (_startScan && _isScanning && !_isScanning()) {
            _startScan();
        }
        SCHED_PRINTLN(F("[BLE SCHEDULER] No active nodes to schedule. Returning to Discovery."));
    }
}

void RendezvousScheduler::setRendezvousEnabled(bool enabled) {
    _rendezvousEnabled = enabled;
    if (!enabled) {
        PowerManager::powerUpRadio();
        if (_startScan && _isScanning && !_isScanning()) {
            _startScan();
        }
        _state = STATE_DISCOVERY;
    }
}

bool RendezvousScheduler::isRendezvousEnabled() const { return _rendezvousEnabled; }
RendezvousScheduler::State RendezvousScheduler::getState() const { return _state; }
const char* RendezvousScheduler::getTargetNodeId() const { return _targetNodeId; }
