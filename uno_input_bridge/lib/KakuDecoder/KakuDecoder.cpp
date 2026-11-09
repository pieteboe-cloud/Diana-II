#include "KakuDecoder.h"

namespace
{
const char HOUSE_CODES[] = "ABCDEFGHIJKLMNOP";
const uint8_t FIRST_BUTTON = 1;
const uint8_t LAST_BUTTON = 32;
} // namespace

const char *KakuDecoder::getHouseCodes()
{
    return HOUSE_CODES;
}

uint8_t KakuDecoder::getFirstButton()
{
    return FIRST_BUTTON;
}

uint8_t KakuDecoder::getLastButton()
{
    return LAST_BUTTON;
}

KakuDecoder::KakuDecoder(uint32_t debounceMs)
    : _debounceMs(debounceMs), _lastCodeTime(0), _lastCodeValue(0), _callback(nullptr)
{
}

void KakuDecoder::onCommand(KakuCallback callback)
{
    _callback = callback;
}

char KakuDecoder::decodeHouseCode(byte n1, byte n2)
{
    // Each house address is encoded in two nibbles in the 24-bit frame.
    const byte address = (n1 << 4) | n2;
    switch (address) {
        case 0x00: return 'A';
        case 0x40: return 'B';
        case 0x10: return 'C';
        case 0x04: return 'D';
        case 0x44: return 'E';
        case 0x14: return 'F';
        case 0x54: return 'G';
        case 0x01: return 'H';
        case 0x41: return 'I';
        case 0x11: return 'J';
        case 0x51: return 'K';
        case 0x05: return 'L';
        case 0x45: return 'M';
        case 0x15: return 'N';
        case 0x55: return 'O';
        case 0x50: return 'P';
        default: return '?';
    }
}

bool KakuDecoder::isValidKaku24(unsigned long value)
{
    const byte houseHigh = (value >> 20) & 0x0F;
    const byte houseLow = (value >> 16) & 0x0F;
    const byte row = (value >> 12) & 0x0F;
    const byte slider = (value >> 8) & 0x0F;
    const byte marker = (value >> 4) & 0x0F;
    const byte state = value & 0x0F;

    // The KAKU marker is 1; address, row, and slider use only 0, 1, 4, or 5.
    const auto validNibble = [](byte nibble) {
        return nibble == 0 || nibble == 1 || nibble == 4 || nibble == 5;
    };

    return decodeHouseCode(houseHigh, houseLow) != '?' &&
           validNibble(row) &&
           validNibble(slider) &&
           marker == 1 &&
           (state == 4 || state == 5);
}

byte KakuDecoder::nibbleToPos(byte n)
{
    switch (n) {
        case 0: return 1;
        case 4: return 2;
        case 1: return 3;
        case 5: return 4;
        default: return 0;
    }
}

void KakuDecoder::processValue(unsigned long value)
{
    const byte houseHigh = (value >> 20) & 0x0F;
    const byte houseLow = (value >> 16) & 0x0F;
    const byte row = (value >> 12) & 0x0F;
    const byte slider = (value >> 8) & 0x0F;
    const bool isOn = (value & 0x0F) == 5;

    const byte rowPosition = nibbleToPos(row);
    const byte sliderPosition = nibbleToPos(slider);

    RFCommand cmd;
    cmd.house = decodeHouseCode(houseHigh, houseLow);
    // Every slider has four positions, each with separate ON and OFF IDs.
    cmd.button = (sliderPosition - 1) * 8 +
                 (rowPosition - 1) * 2 +
                 (isOn ? 1 : 2);
    cmd.timestamp = millis();

    if (_callback)
        _callback(cmd);
}

bool KakuDecoder::onRawData(unsigned long value, int bits, int protocol, int pulse)
{
    (void)protocol;
    (void)pulse;

    // Frame shape determines whether this is KAKU; keep valid repeats classified
    // as KAKU even when debounce prevents another callback.
    if (bits != 24 || !isValidKaku24(value))
        return false;

    const unsigned long now = millis();

    // Treat repeated frames as one held press. Extending the quiet window on
    // each repeat prevents long presses from generating another callback.
    if (value == _lastCodeValue && (now - _lastCodeTime) < _debounceMs) {
        _lastCodeTime = now;
        return true;
    }

    _lastCodeValue = value;
    _lastCodeTime = now;
    processValue(value);
    return true;
}