#pragma once
// using     z3t0/IRremote@^4.4.1

#include <Arduino.h>

class IRSender {
public:
    void begin(uint8_t txPin);
    void sendHex(uint32_t code, int_fast8_t repeats = 0);
};
