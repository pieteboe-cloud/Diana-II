#include <Arduino.h>
#include "LampController.h"

// Global serial object for the Uno link (must be at file scope in Arduino).
// Pin 27 = RX (from Uno TX via level shifter)
// Pin 25 = TX (to Uno RX via level shifter)
HardwareSerial UnoLink1(1);

// Instantiate the lamp controller with our pin assignments
LampController lamps(27, 25);

void setup() {
  // Open USB debug serial (visible in Arduino IDE Serial Monitor)
  Serial.begin(115200);
  while (!Serial) { ; }  // wait for monitor to connect (some boards need this)

  Serial.println("[ESP32] Booting up...");

  // Initialise the Uno UART link (waits for Uno to settle)
  lamps.begin();

  Serial.println("[ESP32] Ready. Cycling through 12 lamps...");
}

void loop() {
  // Cycle through all 12 lamps, one at a time.
  // Each lamp gets a distinct hue spread evenly around the 0-255 colour wheel,
  // so the progression is visually obvious (red → orange → green → ... → purple).
  for (uint8_t lamp = 0; lamp < LampController::LAMP_COUNT; lamp++) {
    // Evenly distribute hue across the wheel
    uint8_t hue = (uint8_t)(lamp * 256 / LampController::LAMP_COUNT);

    // Light this lamp at full saturation and full brightness
    lamps.sendFrame(lamp, hue, 255, 255);

    // Hold each lamp for 2 seconds
    delay(2000);
  }

  // Turn everything off between cycles
  lamps.sendFrame(0, 0, 0, 0);
  Serial.println("[ESP32] Cycle complete. Restarting...");

  // Short pause before the next cycle begins
  delay(1000);
}   