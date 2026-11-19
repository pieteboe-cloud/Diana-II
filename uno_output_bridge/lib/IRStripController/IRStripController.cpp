#include "IRStripController.h"
// Colour presets for the 24-key remote.
const IRStripController::RGB IRStripController::_colours24[16] =
{
    {255,   0,   0},  // RED
    {  0, 255,   0},  // GREEN
    {  0,   0, 255},  // BLUE
    {255, 255, 255},  // WHITE
    {255, 128,   0},  // ORANGE
    {144, 238, 144},  // LIGHT_GREEN
    {  0, 139, 139},  // DARK_CYAN
    {255, 179,  71},  // LIGHT_ORANGE
    {  0, 255, 255},  // CYAN
    { 75,   0, 130},  // DARK_PURPLE
    {255, 165,   0},  // YELLOW_ORANGE
    {173, 216, 230},  // LIGHT_BLUE
    {128,   0, 128},  // PURPLE
    {255, 255,   0},  // YELLOW
    {  0, 128, 128},  // TEAL
    {255,   0, 255}   // MAGENTA
};
// Colour presets for the 44-key remote.
const IRStripController::RGB IRStripController::_colours44[20] =
{
    {255,   0,   0},  // RED
    {  0, 255,   0},  // GREEN
    {  0,   0, 255},  // BLUE
    {255, 255, 255},  // WHITE
    {255, 128,   0},  // ORANGE
    {  0, 128, 128},  // TEAL
    {128,   0, 128},  // PURPLE
    {255, 105, 180},  // PINK
    {255, 165,   0},  // YELLOW_ORANGE
    {  0, 255, 255},  // AQUA
    {255,   0, 255},  // MAGENTA
    {255, 182, 193},  // LIGHT_PINK
    {255, 255,   0},  // YELLOW
    {  0, 255, 255},  // CYAN
    {148,   0, 211},  // VIOLET
    {135, 206, 235},  // SKY_BLUE
    {255, 255, 224},  // LIGHT_YELLOW
    {224, 255, 255},  // LIGHT_CYAN
    {255, 182, 255},  // LIGHT_MAGENTA
    {135, 206, 250}   // LIGHT_SKY_BLUE
};

// Prepare the RF and IR transmitters.
void IRStripController::begin(uint8_t rfTxPin, uint8_t irTxPin)
{
    _rfSender.begin(rfTxPin, 1);
    _irSender.begin(irTxPin);

    configureStrips();
}

// Load the configuration for all LED strips.
void IRStripController::configureStrips()
{
    // Strip 1: Salon table (24-key remote).
    _strips[0] = {
        4477265,  // RF ON
        4477268,  // RF OFF
        24,       // RF bits
        1,        // RF protocol
        16,       // Brightness steps
        RemoteType::REMOTE_24_KEY
    };
}

// Find the nearest preset colour.
uint8_t IRStripController::findNearestColour(RemoteType remoteType, RGB colour)
{
    const RGB *presets;
    uint8_t presetCount;

    uint32_t smallestDistance = UINT32_MAX;
    uint8_t nearestIndex = 0;

    if (remoteType == RemoteType::REMOTE_24_KEY)
    {
        presets = _colours24;
        presetCount = 16;
    }
    else
    {
        presets = _colours44;
        presetCount = 20;
    }
    for (uint8_t i = 0; i < presetCount; i++)
{
    int16_t redDiff = colour.red - presets[i].red;
    int16_t greenDiff = colour.green - presets[i].green;
    int16_t blueDiff = colour.blue - presets[i].blue;

    uint32_t distance =
        (redDiff * redDiff) +
        (greenDiff * greenDiff) +
        (blueDiff * blueDiff);

    if (distance < smallestDistance)
    {
        smallestDistance = distance;
        nearestIndex = i;
    }
}
}

// Set a strip's colour and brightness using HSV values.
void IRStripController::setHSV(uint8_t stripNumber,
                               uint8_t hue,
                               uint8_t saturation,
                               uint8_t value)
{
    if (stripNumber < 1 || stripNumber > STRIP_COUNT)
        return;

    uint8_t index = stripNumber - 1;
    StripConfig &config = _strips[index];

    uint8_t brightnessLevel =
        1 + ((uint16_t)value * (config.brightnessSteps - 1)) / 255;
        
    _states[index].brightnessLevel = brightnessLevel;

    if (config.brightnessSteps == 0)
        return;

    if (value == 0)
    {
        // Turn off the selected strip.
        _rfSender.sendRaw(config.rfOffCode, config.rfBits, config.rfProtocol);
        return;
    }

    // Enable the IR receiver of the selected strip.
    _rfSender.sendRaw(config.rfOnCode, config.rfBits, config.rfProtocol);

    delay(RF_SETTLE_DELAY_MS);
}