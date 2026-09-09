#ifndef BLE_RECEIVER_H
#define BLE_RECEIVER_H

#include <Arduino.h>
#include "ITelemetryReceiver.h"
#include "RendezvousScheduler.h"

class BLEReceiver : public ITelemetryReceiver {
public:
    BLEReceiver();

    void begin() override;
    void poll() override;
    const char* getName() const override { return "BLE"; }

    void setRendezvousEnabled(bool enabled);
    bool isRendezvousEnabled() const;

    static void onRadioPowerDown();
    static void onRadioPowerUp();
    static void startScanning();
    static void stopScanning();
    static bool isScanning();

private:
    RendezvousScheduler _scheduler;
};

#endif // BLE_RECEIVER_H
