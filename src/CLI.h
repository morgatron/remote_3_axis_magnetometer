#ifndef CLI_H
#define CLI_H

#include <Arduino.h>
#include "SensorConfig.h"
#include "SensorManager.h"
#include "TelemetryEgress.h"

class CLI {
public:
    using ChangeCallback = void (*)();
    using ActivityCallback = void (*)();

    /**
     * @brief Construct a new CLI object
     * 
     * @param sensorManager Reference to SensorManager
     * @param telemetry Reference to TelemetryEgress
     * @param config Reference to sensor configuration object
     * @param changeCallback Function pointer called when settings are modified
     * @param activityCallback Function pointer called upon user CLI interaction (e.g. OLED screen wake)
     */
    CLI(SensorManager& sensorManager, TelemetryEgress& telemetry, SensorConfig& config, 
        ChangeCallback changeCallback = nullptr, ActivityCallback activityCallback = nullptr);

    /**
     * @brief Initialize the CLI (e.g., reserve buffer)
     */
    void begin();

    /**
     * @brief Update the CLI state, reading from Serial and processing commands
     */
    void update();

    /**
     * @brief Print the help menu to Serial
     */
    void printHelp();

    /**
     * @brief Parse and execute a command string
     * @param cmd The raw command string
     */
    void handleCommand(String cmd);

private:
    SensorManager& _sensorManager;
    TelemetryEgress& _telemetry;
    SensorConfig& _config;
    ChangeCallback _changeCallback;
    ActivityCallback _activityCallback;
    String _inputBuffer;
};

#endif // CLI_H
