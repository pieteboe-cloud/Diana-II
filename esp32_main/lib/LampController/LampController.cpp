#include "LampController.h"

// The HardwareSerial object must live in the .ino (Arduino requirement);
// we grab a reference to it here so the class can use it without owning it.
extern HardwareSerial UnoLink1;

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------
LampController::LampController(int rxPin, int txPin, int baud)
  : _link(UnoLink1)  // bind to the global serial instance
{
  // Open the UART link: 8 data bits, no parity, 1 stop bit
  _link.begin(baud, SERIAL_8N1, rxPin, txPin);
}

// ---------------------------------------------------------------------------
// begin() - call after constructor to let the Uno power up / settle
// ---------------------------------------------------------------------------
void LampController::begin() {
  delay(500);  // give the Uno time to stabilise before we start sending
}

// ---------------------------------------------------------------------------
// _buildFrame() - assemble the text protocol frame into _frameBuf
// ---------------------------------------------------------------------------
size_t LampController::_buildFrame(uint8_t lamp, uint8_t h, uint8_t s, uint8_t v) {
  size_t pos = 0;

  // Write the fixed prefix that identifies this as a lamp command
  const char* prefix = "O,LAMP,";
  while (*prefix) _frameBuf[pos++] = *prefix++;

  // Loop over all 12 lamps, writing H,S,V triplets separated by commas
  for (uint8_t i = 0; i < LAMP_COUNT; i++) {
    // Active lamp gets the requested colour; all others are off
    uint8_t hh = (i == lamp) ? h : 0;
    uint8_t ss = (i == lamp) ? s : 0;
    uint8_t vv = (i == lamp) ? v : 0;

    // Append "H,S,V" (each 0-255)
    pos += snprintf(_frameBuf + pos, sizeof(_frameBuf) - pos, "%u,%u,%u", hh, ss, vv);

    // Comma between lamp groups (but not after the last one)
    if (i < LAMP_COUNT - 1) _frameBuf[pos++] = ',';
  }

  // Terminate the frame with a newline so the Uno can detect a complete line
  _frameBuf[pos++] = '\n';
  _frameBuf[pos] = '\0';  // null-terminate for safety
  return pos;
}

// ---------------------------------------------------------------------------
// sendFrame() - build the frame, log it, and transmit to the Uno
// ---------------------------------------------------------------------------
void LampController::sendFrame(uint8_t lamp, uint8_t hue, uint8_t sat, uint8_t val) {
  size_t len = _buildFrame(lamp, hue, sat, val);

  // ---- Debug output to USB Serial Monitor ----
  Serial.print("[ESP32] Lamp ");
  Serial.print(lamp);
  Serial.print(" -> H:"); Serial.print(hue);
  Serial.print(" S:");    Serial.print(sat);
  Serial.print(" V:");    Serial.print(val);
  Serial.print(" | ");    Serial.print(len);
  Serial.println(" bytes");

  // Transmit the entire frame in one write() call.
  // The Uno expects the full line in a single burst (radio constraint).
  _link.write((const uint8_t*)_frameBuf, len);

  // Block until the TX FIFO is empty so we don't start the next frame
  // before this one has fully gone out
  _link.flush();
}   