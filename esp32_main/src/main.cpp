#include <Arduino.h>
#include "LampController.h"
#include "RoomWebTest.h"

// UART1 to the Uno: GPIO 27 = RX, GPIO 25 = TX, 250000 baud
HardwareSerial UnoLink1(1);

// The access point / web server lives in its own class (lib/RoomWebTest).
RoomWebTest web("RoomLights", "LightRoom42");

// The lamp controller keeps the HSV canvas for all 12 lamps (lib/LampController).
LampController lamps(27, 25);

// ---------------------------------------------------------------------------
// onWebLampChange() - browser clicked a lamp and sent RGB + brightness
// Converts RGB to HSV via LampController::rgbToHsv, stores it, sends frame.
// ---------------------------------------------------------------------------
void onWebLampChange(uint8_t lamp, uint8_t r, uint8_t g, uint8_t b, uint8_t brightness) {
  uint8_t h, s, v;
  LampController::rgbToHsv(r, g, b, h, s, v);
  v = brightness;   // master brightness slider overrides the picker's value

  // Store this lamp's HSV in the local canvas
  lamps.setLamp(lamp, h, s, v);

  // Transmit the full 12-lamp frame to the Uno over UART1.
  // Other lamps keep their last-set colour because LampController stores state.
  lamps.sendFrame();

  Serial.print("[ESP32] Lamp ");
  Serial.print(lamp);
  Serial.print(" set to H="); Serial.print(h);
  Serial.print(" S="); Serial.print(s);
  Serial.print(" V="); Serial.println(v);
}

// ---------------------------------------------------------------------------
// onWebRf() - browser pressed an RF test button.
// Writes "O,RF,<decimal>\n" to UART1 for the Uno to relay via 433 MHz.
// ---------------------------------------------------------------------------
void onWebRf(const char* decimalCode) {
  UnoLink1.print("O,RF,");
  UnoLink1.print(decimalCode);
  UnoLink1.println();
  UnoLink1.flush();

  Serial.print("[ESP32] RF ");
  Serial.println(decimalCode);
}

// ---------------------------------------------------------------------------
// onWebIr() - browser pressed an IR test button.
// Writes "O,IR,<hex>\n" to UART1 for the Uno to relay via IR blaster.
// ---------------------------------------------------------------------------
void onWebIr(const char* hexCode) {
  UnoLink1.print("O,IR,");
  UnoLink1.print(hexCode);
  UnoLink1.println();
  UnoLink1.flush();

  Serial.print("[ESP32] IR ");
  Serial.println(hexCode);
}

void setup() {
  Serial.begin(115200);

  // Wait for the Serial Monitor, but only 2 seconds so the AP starts
  // even if no monitor is attached.
  unsigned long serialWaitStart = millis();
  while (!Serial && (millis() - serialWaitStart < 2000)) {
    ; // timeout
  }

  Serial.println("[ESP32] Booting up...");

  // Wire the web page's callbacks to our handler functions.
  web.setLampHandler(onWebLampChange);
  web.setRfHandler(onWebRf);
  web.setIrHandler(onWebIr);

  // Start Wi-Fi access point + web server
  web.begin();

  // Initialise the Uno UART link (waits for Uno to settle)
  lamps.begin();

  Serial.println("[ESP32] Ready. Open http://192.168.4.1 in a browser.");
}

void loop() {
  // Serve web clients. Lamp/RF/IR changes happen inside the handlers
  // when the browser presses Send or a test button.
  web.handleClient();
}
