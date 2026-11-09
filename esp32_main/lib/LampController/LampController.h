#pragma once
#include <Arduino.h>

/**
 * LampController - manages communication with the Uno lamp board.
 *
 * Builds a text frame describing the HSV state of all 12 lamps and
 * transmits it over a UART link (Serial1) to the Uno, which relays
 * the values to the actual lamp hardware via radio.
 */
class LampController {
public:
  // Total number of lamps the controller can address
  static constexpr uint8_t LAMP_COUNT = 12;

  /**
   * @param rxPin  ESP32 GPIO pin wired to Uno TX (via level shifter)
   * @param txPin  ESP32 GPIO pin wired to Uno RX (via level shifter)
   * @param baud   Baud rate for the Uno link (default 250000)
   */
  LampController(int rxPin, int txPin, int baud = 250000);

  /**
   * Call once after the constructor. Waits for the Uno to settle
   * so the first frame isn't lost.
   */
  void begin();

  /**
   * Build and send a full 12-lamp frame.
   * The specified lamp gets the given HSV values; all others go to (0,0,0).
   */
  void sendFrame(uint8_t lamp, uint8_t hue, uint8_t sat, uint8_t val);

private:
  // Reference to the global HardwareSerial instance (must be global in Arduino)
  HardwareSerial& _link;

  // Scratch buffer used to assemble the text frame before sending
  char _frameBuf[128];

  /**
   * Fill _frameBuf with a protocol-compliant frame string.
   * Format: "O,LAMP,<H1>,<S1>,<V1>,...,<H12>,<S12>,<V12>\n"
   * Returns the number of bytes written (excluding null terminator).
   */
  size_t _buildFrame(uint8_t lamp, uint8_t h, uint8_t s, uint8_t v);
};   