#include "RFSender.h"

void RFSender::begin(uint8_t txPin, uint8_t repeatCount) {
    _sw.enableTransmit(txPin);
    _sw.setRepeatTransmit(repeatCount);
}

void RFSender::sendRaw(unsigned long value, int bits, int protocol) {
    _sw.setProtocol(protocol);
    _sw.setPulseLength(315);
    _sw.send(value, bits);
}
