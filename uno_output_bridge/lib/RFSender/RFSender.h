#pragma once
#include <Arduino.h>
#include <RCSwitch.h>

// Simple RF sender wrapper around RCSwitch
class RFSender {
public:
    // repeatCount is the number of complete code transmissions per sendRaw()
    // call; 5 is a shorter test burst than RCSwitch's default of 10.
    void begin(uint8_t txPin, uint8_t repeatCount = 3);
    void sendRaw(unsigned long value, int bits, int protocol);

private:
    RCSwitch _sw;
};
