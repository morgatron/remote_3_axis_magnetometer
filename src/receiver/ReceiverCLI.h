#ifndef RECEIVER_CLI_H
#define RECEIVER_CLI_H

#include <Arduino.h>
#include "NodeTracker.h"

typedef void (*SaveCallback)();

class ReceiverCLI {
public:
    ReceiverCLI(SaveCallback saveCb = nullptr);
    void begin();
    void process();

private:
    String _inputBuffer;
    SaveCallback _saveCallback;

    void handleCommand(const String &cmd);
    void printHelp();
    void printStatus();
};

#endif // RECEIVER_CLI_H
