// ============================================================================
// InputBridge — main.cpp
//
// This Uno is the "ears" of the system. It listens on two receivers and
// forwards any received codes to the ESP32 (the "brain") over UART:
//
//   1. IR receiver (38 kHz) → IRremote library
//   2. RF receiver (433 MHz) → rc-switch + KakuDecoder
//
// It does NOT send anything. A separate OutputBridge Uno handles the
// transmitters.
//
// ============================================================================
// PROTOCOL (newline-delimited text, this Uno → ESP32)
//
//   I,IR,<hex>\n
//       → IR code received, e.g. "I,IR,BF40FF00\n"
//
//   I,KAKU,<house>,<button>\n
//       → KAKU RF code decoded, e.g. "I,KAKU,A,5\n"
//         house = 'A'..'P', button = 1..32
//
//   I,RF,<hex>,<bits>\n
//       → Non-KAKU RF code (raw), e.g. "I,RF,4477265,24\n"
//
// All messages end with '\n'. The ESP32 reads one line at a time.
// ============================================================================

#include <Arduino.h>
#include <IRremote.hpp>   // new 4.x API (no deprecation banner)
#include <RCSwitch.h>
#include <SoftwareSerial.h>
#include "KakuDecoder.h"  // local library: lib/KakuDecoder/

// ============================================================================
// FORWARD DECLARATIONS
// ============================================================================

void handleIR(uint32_t code);
void handleRF(uint32_t code, unsigned int bits);
void onKakuCommand(const RFCommand& cmd);

// ============================================================================
// PIN DEFINITIONS
// ============================================================================

// --- IR receiver (38 kHz) ---
// Digital pin 2 = INT0 — IRremote's IRrecv needs an interrupt-capable pin.
constexpr uint8_t IR_RECEIVE_PIN = 2;

// --- RF receiver (433 MHz) ---
// Digital pin 3 = INT1 — rc-switch's RCSwitch needs an interrupt-capable pin.
// KAKU remote control codes are received here (pt2262/ev1527 encoding).
constexpr uint8_t RF_RECEIVE_PIN = 3;

// --- Status LEDs ---
// Green flashes on valid KAKU frame; red flashes on anything else
// (IR code or non-KAKU RF). Pins 8/9 are free — nothing else uses them.
constexpr uint8_t LED_GREEN_PIN = 8;  // KAKU received
constexpr uint8_t LED_RED_PIN = 9;    // IR or raw RF received

// --- UART link to the ESP32 ---
// The Uno only has ONE hardware serial (Serial = USB debug).
// We use SoftwareSerial on pins 4/5 for the ESP32 link so debug output
// can stay on the USB Serial Monitor.
//   TX pin 4 → ESP32 RX (via level shifter)
//   RX pin 5 ← ESP32 TX (via level shifter)
constexpr uint8_t ESP_TX_PIN = 4;
constexpr uint8_t ESP_RX_PIN = 5;
SoftwareSerial EspLink(ESP_RX_PIN, ESP_TX_PIN);

// ============================================================================
// PROTOCOL CONSTANTS
// ============================================================================

constexpr uint16_t LINE_BUFFER_SIZE = 64;

// ============================================================================
// OBJECTS & STATE
// ============================================================================

// IR receiver — IRremote 4.x uses a global singleton named IrReceiver.
// We configure it in setup() with the receive pin.
RCSwitch rfswitch;              // RF receiver object
KakuDecoder kakuDecoder;       // KAKU protocol decoder (house+button)

// ============================================================================
// KAKU CALLBACK
//
// Called by KakuDecoder when a valid KAKU frame is decoded.
// Outputs housecode + button number to the ESP32.
// ============================================================================

void onKakuCommand(const RFCommand& cmd) {
    // Build the text line: "I,KAKU,<house>,<button>\n"
    char line[LINE_BUFFER_SIZE];
    snprintf(line, sizeof(line), "I,KAKU,%c,%u\n", cmd.house, cmd.button);

    // Forward to ESP32
    EspLink.print(line);

    // Flash GREEN LED — valid KAKU frame received
    digitalWrite(LED_GREEN_PIN, HIGH);
    delay(60);
    digitalWrite(LED_GREEN_PIN, LOW);

    // ---- DEBUG ----
    Serial.print("[InputBridge] KAKU -> ");
    Serial.println(line);
    // -----------------
}

// ============================================================================
// IR HANDLER
//
// Called from loop() when IRremote's interrupt has captured a complete IR
// code. Converts the raw code to hex and forwards it to the ESP32.
// ============================================================================

void handleIR(uint32_t code) {
    // Build the text line: "I,IR,<hex>\n"
    char line[LINE_BUFFER_SIZE];
    snprintf(line, sizeof(line), "I,IR,%08lX\n", static_cast<unsigned long>(code));

    // Forward to ESP32
    EspLink.print(line);

    // Flash RED LED — IR code received (not KAKU)
    digitalWrite(LED_RED_PIN, HIGH);
    delay(60);
    digitalWrite(LED_RED_PIN, LOW);

    // ---- DEBUG ----
    Serial.print("[InputBridge] IR  -> ");
    Serial.println(line);
    // -----------------
}

// ============================================================================
// RF HANDLER
//
// Called from loop() when an RF code is NOT a valid KAKU frame.
// Outputs the raw code + bit length to the ESP32.
// ============================================================================

void handleRF(uint32_t code, unsigned int bits) {
    // Build the text line: "I,RF,<hex>,<bits>\n"
    char line[LINE_BUFFER_SIZE];
    snprintf(line, sizeof(line), "I,RF,%08lX,%u\n",
             static_cast<unsigned long>(code), bits);

    // Forward to ESP32
    EspLink.print(line);

    // Flash RED LED — non-KAKU RF code received
    digitalWrite(LED_RED_PIN, HIGH);
    delay(60);
    digitalWrite(LED_RED_PIN, LOW);

    // ---- DEBUG ----
    Serial.print("[InputBridge] RF  -> ");
    Serial.println(line);
    // -----------------
}

// ============================================================================
// SETUP
//
// Initializes both receivers and the UART link to the ESP32.
// Debug output goes to Serial (USB) so you can watch it in the Arduino
// Serial Monitor while the Uno is still connected to the ESP32.
// ============================================================================

void setup() {
    // Debug serial to computer (USB cable)
    Serial.begin(115200);
    Serial.println("[InputBridge] Booting up...");

    // UART link to the ESP32 (115200 baud, 8N1)
    EspLink.begin(115200);

    // --- IR receiver ---
    IrReceiver.begin(IR_RECEIVE_PIN);  // set receive pin and start IR receiver
    Serial.println("[InputBridge] IR receiver enabled on pin 2");

    // --- RF receiver ---
    rfswitch.enableReceive(digitalPinToInterrupt(RF_RECEIVE_PIN));
    // start the RF receiver interrupt on pin 3 (interrupt 1).
    // rc-switch captures raw 433 MHz signals; KakuDecoder filters KAKU frames.
    Serial.println("[InputBridge] RF receiver enabled on pin 3");

    // Register the KAKU command callback so decoded KAKU frames are forwarded.
    kakuDecoder.onCommand(onKakuCommand);

    // Status LEDs
    pinMode(LED_GREEN_PIN, OUTPUT);
    digitalWrite(LED_GREEN_PIN, LOW);
    pinMode(LED_RED_PIN, OUTPUT);
    digitalWrite(LED_RED_PIN, LOW);

    Serial.println("[InputBridge] Ready. Forwarding codes to ESP32...");
}

// ============================================================================
// LOOP
//
// Polls both receivers for pending codes. Both libraries use interrupts to
// capture the raw signal, so this loop just checks "is there a code ready?".
// No delays — the loop runs as fast as possible to minimize missed codes.
//
// RF flow:
//   rc-switch captures the raw 433 MHz signal → onRawData() checks if it's
//   a valid KAKU frame. If yes, the KakuCallback fires (house+button). If no,
//   we output the raw RF code.
// ============================================================================

void loop() {
    // --- Check IR receiver ---
    // decode() returns true when a full IR code has been captured.
    if (IrReceiver.decode()) {
        // A full IR code was captured — read it from the decoded data
        handleIR(IrReceiver.decodedIRData.decodedRawData);
        IrReceiver.resume();  // reset the receiver for the next code
    }

    // --- Check RF receiver ---
    // available() returns true when a full RF code has been captured.
    if (rfswitch.available()) {
        // A full RF code was captured
        unsigned long code = rfswitch.getReceivedValue();
        unsigned int bits = rfswitch.getReceivedBitlength();
        unsigned int protocol = rfswitch.getReceivedProtocol();
        unsigned int pulse = rfswitch.getReceivedDelay();

        // Ask KakuDecoder if this is a valid KAKU frame.
        // If yes → the callback already forwarded house+button.
        // If no  → not KAKU, output the raw code.
        if (!kakuDecoder.onRawData(code, bits, protocol, pulse)) {
            handleRF(code, bits);
        }

        rfswitch.resetAvailable();  // clear the "new code" flag
    }
}
