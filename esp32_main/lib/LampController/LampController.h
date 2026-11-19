#pragma once
#include <Arduino.h>

/**
 * LampController - manages communication with the Uno lamp board.
 *
 * Keeps the HSV state of all 12 lamps locally so several lamps can
 * stay lit at once. Each call to setLamp() updates one lamp's state
 * and sendFrame() transmits the full 12-lamp frame over UART1 to the
 * Uno, which relays the values to the lamps via CC2500 radio.
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
   * Set one lamp's HSV colour in the local canvas. Other lamps keep
   * their current state.
   */
  void setLamp(uint8_t lamp, uint8_t hue, uint8_t sat, uint8_t val);

  /**
   * Return the stored state of one lamp (hue/sat/val are set only if
   * the pointers are non-null). Returns false for an out-of-range lamp.
   */
  bool getLamp(uint8_t lamp, uint8_t* hue, uint8_t* sat, uint8_t* val) const;

  /**
   * Turn every lamp off in the local canvas.
   */
  void clearAll();

  /**
   * Convert RGB (0-255 each) to HSV (hue 0-255, sat 0-255, val 0-255).
   * Static utility — no instance needed. The caller sets v separately
   * if a master brightness slider overrides the colour-picker value.
   */
  static void rgbToHsv(uint8_t r, uint8_t g, uint8_t b,
                       uint8_t& h, uint8_t& s, uint8_t& v);

  /**
   * Build and send a full 12-lamp frame from the stored canvas state.
   * Every lamp keeps its last-set colour; lamps never set are off.
   */
  void sendFrame();

private:
  // Reference to the global HardwareSerial instance (must be global in Arduino)
  HardwareSerial& _link;

  // Live canvas: HSV state for every lamp
  uint8_t _hue[LAMP_COUNT];
  uint8_t _sat[LAMP_COUNT];
  uint8_t _val[LAMP_COUNT];

  // Scratch buffer used to assemble the text frame before sending.
  // Full frame: "O,LAMP," + 12 x 3 values + 35 commas + "\n" = ~151 bytes.
  char _frameBuf[192];

  /**
   * Fill _frameBuf with a protocol-compliant frame string from the
   * stored canvas state.
   * Format: "O,LAMP,<H1>,<S1>,<V1>,...,<H12>,<S12>,<V12>\n"
   * Returns the number of bytes written (excluding null terminator).
   */
  size_t _buildFrame();
};
