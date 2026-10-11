#ifndef CLI_H
#define CLI_H

#include <Arduino.h>
#include "SensorConfig.h"
#include "SensorManager.h"

class CLI {
public:
    using ChangeCallback = void (*)();

    /**
     * @brief Construct a new CLI object
     * 
     * @param sensorManager Reference to SensorManager
     * @param config Reference to sensor configuration object
     * @param changeCallback Function pointer called when settings are modified
     */
    CLI(SensorManager& sensorManager, SensorConfig& config, ChangeCallback changeCallback = nullptr);

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
    SensorConfig& _config;
    ChangeCallback _changeCallback;
    String _inputBuffer;
};

#endif // CLI_H
