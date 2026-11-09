#pragma once
#include <Arduino.h>

// Decoded KAKU command. ON and OFF have separate button IDs.
struct RFCommand {
    char house;           // 'A' - 'P'
    uint8_t button;       // 1 - 32
    uint32_t timestamp;   // Milliseconds since startup
};

// Return true from onRawData() when the frame is a valid KAKU frame,
// including frames suppressed as repeats by the debounce filter.
typedef void (*KakuCallback)(const RFCommand&);

class KakuDecoder {
public:
    KakuDecoder(uint32_t debounceMs = 300);

    bool onRawData(unsigned long value, int bits, int protocol, int pulse);
    void onCommand(KakuCallback callback);

    // -------------------------------------------------------------------------
    // Protocol metadata
    // -------------------------------------------------------------------------
    //
    // These describe what this decoder can emit, so no other layer has to
    // hardcode the ranges. WebUI serves them to the scene editor.
    //

    /** House letters in display order, no terminator. */
    static const char *getHouseCodes();

    /** First and last button number this decoder can produce. */
    static uint8_t getFirstButton();
    static uint8_t getLastButton();

private:
    uint32_t _debounceMs;
    unsigned long _lastCodeTime;
    unsigned long _lastCodeValue;
    KakuCallback _callback;

    static bool isValidKaku24(unsigned long value);
    static char decodeHouseCode(byte n1, byte n2);
    static byte nibbleToPos(byte n);
    void processValue(unsigned long value);
};
