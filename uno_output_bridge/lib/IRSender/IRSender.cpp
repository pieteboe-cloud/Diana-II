#include "IRSender.h"
#include <IRremote.hpp>

void IRSender::begin(uint8_t txPin) {
    IrSender.begin(txPin);
}

void IRSender::sendHex(uint32_t code, int_fast8_t repeats) {
    IrSender.sendNECRaw(code, repeats);
}
