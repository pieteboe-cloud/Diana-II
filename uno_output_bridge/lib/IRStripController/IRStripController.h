#pragma once

#include <Arduino.h>
#include "IRSender.h"
#include "RFSender.h"

class IRStripController
{
public:
    static constexpr uint8_t STRIP_COUNT = 10;

    // Prepare the RF and IR transmitters.
    void begin(uint8_t rfTxPin, uint8_t irTxPin);

    // Load the configuration for all LED strips.
    void configureStrips();

    // Set a strip's colour and brightness using HSV values.
    // stripNumber: 1 to 10
    // hue, saturation, value: 0 to 255
    void setHSV(uint8_t stripNumber, uint8_t hue,
                uint8_t saturation, uint8_t value);

private:
    // Supported IR remote types.
    enum class RemoteType : uint8_t
    {
        REMOTE_24_KEY,
        REMOTE_44_KEY
    };

    // RGB colour values.
struct RGB
{
    uint8_t red;
    uint8_t green;
    uint8_t blue;
};

// Find the nearest preset colour.
uint8_t findNearestColour(RemoteType remoteType, RGB colour);
// Colour presets for each remote type.
static const RGB _colours24[16];
static const RGB _colours44[20];

    // Individual settings for each LED strip.
    struct StripConfig
    {
        uint32_t rfOnCode;
        uint32_t rfOffCode;

        uint8_t rfBits;
        uint8_t rfProtocol;

        uint8_t brightnessSteps;

        RemoteType remoteType;
    };

    // Current state of an LED strip.
    struct StripState
    {
        uint8_t brightnessLevel = 0;
    };

    // Configuration for all LED strips.
    StripConfig _strips[STRIP_COUNT];

    // Current state of all LED strips.
    StripState _states[STRIP_COUNT];

    // RF settling time before sending IR commands.
    static constexpr uint16_t RF_SETTLE_DELAY_MS = 10;

    // Shared transmitters for all LED strips.
    IRSender _irSender;
    RFSender _rfSender;
};