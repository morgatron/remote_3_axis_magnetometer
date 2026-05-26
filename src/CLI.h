#ifndef CLI_H
#define CLI_H

#include <Arduino.h>
#include "Magnetometer.h"

class CLI {
public:
    /**
     * @brief Construct a new CLI object
     * 
     * @param sensor Pointer to the active magnetometer sensor
     * @param streaming Reference to the streaming state variable
     * @param current_rate Reference to the current sampling rate variable
     * @param saveCallback Function pointer to call when settings need to be saved
     */
    CLI(Magnetometer* sensor, bool& streaming, uint8_t& current_rate, void (*saveCallback)());

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

private:
    /**
     * @brief Parse and execute a command string
     * @param cmd The raw command string
     */
    void handleCommand(String cmd);
    
    Magnetometer* _sensor;
    bool& _streaming;
    uint8_t& _current_rate;
    void (*_saveCallback)();
    String _inputBuffer;
};

#endif
