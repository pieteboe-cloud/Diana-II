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
  // Canvas starts with every lamp off
  for (uint8_t i = 0; i < LAMP_COUNT; i++) {
    _hue[i] = 0;
    _sat[i] = 0;
    _val[i] = 0;
  }

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
// setLamp() - update one lamp's HSV state in the local canvas
// ---------------------------------------------------------------------------
void LampController::setLamp(uint8_t lamp, uint8_t hue, uint8_t sat, uint8_t val) {
  if (lamp >= LAMP_COUNT) return;
  _hue[lamp] = hue;
  _sat[lamp] = sat;
  _val[lamp] = val;
}

// ---------------------------------------------------------------------------
// getLamp() - read one lamp's stored HSV state
// ---------------------------------------------------------------------------
bool LampController::getLamp(uint8_t lamp, uint8_t* hue, uint8_t* sat, uint8_t* val) const {
  if (lamp >= LAMP_COUNT) return false;
  if (hue) *hue = _hue[lamp];
  if (sat) *sat = _sat[lamp];
  if (val) *val = _val[lamp];
  return true;
}

// ---------------------------------------------------------------------------
// clearAll() - turn every lamp off in the local canvas
// ---------------------------------------------------------------------------
void LampController::clearAll() {
  for (uint8_t i = 0; i < LAMP_COUNT; i++) {
    _hue[i] = 0;
    _sat[i] = 0;
    _val[i] = 0;
  }
}

// ---------------------------------------------------------------------------
// rgbToHsv() - static utility: RGB (0-255) -> HSV (hue 0-255, sat 0-255, val 0-255)
// ---------------------------------------------------------------------------
void LampController::rgbToHsv(uint8_t r, uint8_t g, uint8_t b,
                              uint8_t& h, uint8_t& s, uint8_t& v)
{
  // Work in 0-1 range
  double rf = r / 255.0;
  double gf = g / 255.0;
  double bf = b / 255.0;

  double maxc = rf > gf ? (rf > bf ? rf : bf) : (gf > bf ? gf : bf);
  double minc = rf < gf ? (rf < bf ? rf : bf) : (gf < bf ? gf : bf);
  double range = maxc - minc;

  // Value placeholder — caller overwrites v with the master brightness slider
  v = 0;

  if (range == 0) {
    h = 0;  // achromatic (grey)
    s = 0;
  } else {
    s = (uint8_t)((range / maxc) * 255.0);

    double hh;
    if (maxc == rf) hh = (gf - bf) / range;
    else if (maxc == gf) hh = 2.0 + (bf - rf) / range;
    else hh = 4.0 + (rf - gf) / range;

    hh *= 60.0;
    if (hh < 0) hh += 360.0;
    h = (uint8_t)(hh / 360.0 * 255.0);
  }
}

// ---------------------------------------------------------------------------
// _buildFrame() - assemble the text protocol frame into _frameBuf
// ---------------------------------------------------------------------------
size_t LampController::_buildFrame() {
  size_t pos = 0;

  // Write the fixed prefix that identifies this as a lamp command
  const char* prefix = "O,LAMP,";
  while (*prefix) _frameBuf[pos++] = *prefix++;

  // Loop over all 12 lamps, writing each lamp's stored H,S,V triplet
  for (uint8_t i = 0; i < LAMP_COUNT; i++) {
    // Append "H,S,V" (each 0-255) from the stored canvas state
    pos += snprintf(_frameBuf + pos, sizeof(_frameBuf) - pos, "%u,%u,%u",
                    _hue[i], _sat[i], _val[i]);

    // Comma between lamp groups (but not after the last one)
    if (i < LAMP_COUNT - 1) _frameBuf[pos++] = ',';
  }

  // Terminate the frame with a newline so the Uno can detect a complete line
  _frameBuf[pos++] = '\n';
  _frameBuf[pos] = '\0';  // null-terminate for safety
  return pos;
}

// ---------------------------------------------------------------------------
// sendFrame() - log the canvas and transmit the full frame to the Uno
// ---------------------------------------------------------------------------
void LampController::sendFrame() {
  size_t len = _buildFrame();

  // ---- Debug output to USB Serial Monitor ----
  Serial.print("[ESP32] Frame ");
  Serial.print(len);
  Serial.println(" bytes");

  for (uint8_t i = 0; i < LAMP_COUNT; i++) {
    Serial.print(" L"); Serial.print(i);
    Serial.print(":H"); Serial.print(_hue[i]);
    Serial.print("S"); Serial.print(_sat[i]);
    Serial.print("V"); Serial.print(_val[i]);
    if (i < LAMP_COUNT - 1) Serial.print(",");
  }
  Serial.println();

  // Transmit the entire frame in one write() call.
  // The Uno expects the full line in a single burst (radio constraint).
  _link.write((const uint8_t*)_frameBuf, len);

  // Block until the TX FIFO is empty so we don't start the next frame
  // before this one has fully gone out
  _link.flush();
}
