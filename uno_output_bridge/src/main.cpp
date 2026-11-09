// ============================================================================
// OutputBridge — main.cpp
//
// This Uno is the "hands" of the system. It receives commands from the
// ESP32 (the "brain") over UART and drives three output types:
//
//   1. CC2500 radio → Philips Living Color lamps (2006, pre-Hue)
//   2. RF 433 MHz   → KAKU / remote-control devices
//   3. IR 38 kHz    → IR LED blaster (12 LEDs)
//
// It does NOT receive input. A separate InputBridge Uno handles
// RF/IR/KAKU receivers and forwards events to the ESP32.
//
// ============================================================================
// PROTOCOL (newline-delimited text, ESP32 → this Uno)
//
//   O,LAMP,<H1>,<S1>,<V1>,<H2>,<S2>,<V2>,...,<H12>,<S12>,<V12>\n
//       → Send one full frame to all 12 lamps (80 Hz)
//
//   O,IR,<hex>\n
//       → Send an IR code (e.g. O,IR,BF40FF00\n)
//
//   O,RF,<hex>\n
//       → Send an RF code (e.g. O,RF,4477265\n)
//
// All messages end with '\n'. The parser reads one line at a time.
//
// ============================================================================
// WIRING
//
//   CC2500:  CS=10, SCK=13, MOSI=11, GDO0=2 (int), GDO2→LED (open-drain)
//   RF:      A2 (16) → RF module data pin
//   IR:      A1 (15) → IR blaster data pin
//   LED OK:  A0 (14) → green LED (220Ω to 5V)
//   LED ERR: A1... wait, A1 is IR. Let's use A4 (18) for ERR.
//
//   Actually:
//   LED OK:  A0 (14) → green
//   LED ERR: A4 (18) → red
//
// ============================================================================

#include <Arduino.h>
#include <CC2500LivingColor.h>
#include <IRSender.h>
#include <RFSender.h>

// ============================================================================
// FORWARD DECLARATIONS
// ============================================================================

void processLine(char* line);
void handleLampFrame(char* data);
void handleIR(char* hexStr);
void handleRF(char* hexStr);

// ============================================================================
// PIN DEFINITIONS
// ============================================================================

// --- CC2500 (radio to Living Color lamps) ---
// SPI pins are fixed by Uno hardware: SCK=13, MOSI=11, MISO=12
// CS is configurable, GDO0 is hardcoded in the library (pin 2)
constexpr uint8_t CC2500_CS_PIN = 10;

// --- RF transmitter (433 MHz, rc-switch) ---
constexpr uint8_t RF_TRANSMIT_PIN = 16;   // A2

// --- IR transmitter (38 kHz, IRremote) ---
constexpr uint8_t IR_TRANSMIT_PIN = 15;   // A1

// --- Status LEDs ---
constexpr uint8_t LED_OK_PIN  = 14;       // A0 — green: all systems initialized
constexpr uint8_t LED_ERR_PIN = 18;       // A4 — red: CC2500 not found

// ============================================================================
// PROTOCOL CONSTANTS
// ============================================================================

// Line buffer size: "O,LAMP," + 12×3 values + 11 commas + "\n" ≈ 80 bytes
// Add margin for future commands
constexpr uint16_t LINE_BUFFER_SIZE = 128;

// Lamp count (from the radio library)
constexpr uint8_t LAMP_COUNT = CC2500LivingColor::LAMP_COUNT;

// ============================================================================
// OBJECTS & STATE
// ============================================================================

CC2500LivingColor radio;    // CC2500 → Living Color lamps
IRSender ir;                // IR blaster
RFSender rf;                // RF 433 MHz

bool radioReady = false;    // true once CC2500 is verified and initialized

// Line parser state
char lineBuffer[LINE_BUFFER_SIZE] = {};
uint16_t lineLen = 0;

// ============================================================================
// RF/IR TEST TIMING (only used when the test block is uncommented)
// ============================================================================

constexpr unsigned long RF_INTERVAL_MS  = 12000;   // fire every 12 s
constexpr unsigned long RF_OFF_DELAY_MS = 5000;    // turn off after 5 s
unsigned long lastRfTime = 0;
bool rfPendingOff = false;
unsigned long rfOffAt = 0;

// ============================================================================
// LINE PARSER
//
// Reads bytes from Serial until a '\n' is received, then calls
// processLine() with the complete line.
//
// This is deliberately simple: no framing, no checksums.
// The ESP32 is the only sender, and the protocol is one-directional.
// If a byte is corrupted, the worst case is a malformed line that
// processLine() silently ignores (no matching prefix).
// ============================================================================

void receiveCommands() {
    while (Serial.available() > 0) {
        const char c = static_cast<char>(Serial.read());

        if (c == '\n') {
            // End of line — process it
            lineBuffer[lineLen] = '\0';
            processLine(lineBuffer);
            lineLen = 0;
        } else if (c == '\r') {
            // Ignore carriage returns (in case ESP32 sends \r\n)
        } else if (lineLen < LINE_BUFFER_SIZE - 1) {
            // Accumulate characters
            lineBuffer[lineLen++] = c;
        }
        // If lineLen >= LINE_BUFFER_SIZE-1, drop bytes (overflow protection)
    }
}

// ============================================================================
// LINE DISPATCHER
//
// Looks at the first few characters to determine the command type,
// then routes to the appropriate handler.
//
// Expected formats:
//   "O,LAMP,<36 comma-separated values>\n"
//   "O,IR,<hex>\n"
//   "O,RF,<hex>\n"
// ============================================================================

void processLine(char* line) {
    // All commands start with "O," (Output)
    if (line[0] != 'O' || line[1] != ',') return;

    // --- Lamp frame: "O,LAMP,H,S,V,H,S,V,..." ---
    if (strncmp(line, "O,LAMP,", 7) == 0) {
        handleLampFrame(line + 7);
        return;
    }

    // --- IR command: "O,IR,<hex>" ---
    if (strncmp(line, "O,IR,", 5) == 0) {
        handleIR(line + 5);
        return;
    }

    // --- RF command: "O,RF,<hex>" ---
    if (strncmp(line, "O,RF,", 5) == 0) {
        handleRF(line + 5);
        return;
    }

    // Unknown command — silently ignore
}

// ============================================================================
// LAMP FRAME HANDLER
//
// Parses 36 comma-separated values (12 lamps × H,S,V) and sends
// the full frame to all lamps in one back-to-back radio burst.
//
// Input: "120,255,200,130,250,190,140,240,180,..."
//        ^lamp1 H  S  V  ^lamp2 H  S  V  ...
//
// Total radio time: 12 × 800 µs = 9.6 ms
// Total UART receive time: ~80 bytes / 115200 = 7 ms
// → The radio is the bottleneck, UART buffer never overflows.
// ============================================================================

void handleLampFrame(char* data) {
    if (!radioReady) return;

    // Parse 36 values into arrays
    uint8_t hue[LAMP_COUNT];
    uint8_t sat[LAMP_COUNT];
    uint8_t val[LAMP_COUNT];

    char* ptr = data;
    for (uint8_t i = 0; i < LAMP_COUNT; ++i) {
        hue[i] = (uint8_t)strtol(ptr, &ptr, 10);  // H
        if (*ptr == ',') ptr++;
        sat[i] = (uint8_t)strtol(ptr, &ptr, 10);  // S
        if (*ptr == ',') ptr++;
        val[i] = (uint8_t)strtol(ptr, &ptr, 10);  // V
        if (*ptr == ',') ptr++;
    }

    // Send all 12 lamps back-to-back (9.6 ms total air time)
    radio.sendFrame(hue, sat, val);
}

// ============================================================================
// IR HANDLER
//
// Parses a hex string and sends it as an IR code.
// Input: "BF40FF00" → ir.sendHex(0xBF40FF00, 0)
// ============================================================================

void handleIR(char* hexStr) {
    uint32_t code = (uint32_t)strtoul(hexStr, nullptr, 16);
    ir.sendHex(code, 0);
    // IR blaster has its own activity LED — no Uno pin needed
}

// ============================================================================
// RF HANDLER
//
// Parses a hex string and sends it as an RF code.
// Input: "4477265" → rf.sendRaw(0x4477265, 24, 1)
// ============================================================================

void handleRF(char* hexStr) {
    uint32_t code = (uint32_t)strtoul(hexStr, nullptr, 16);
    rf.sendRaw(code, 24, 1);
    // RF module has its own activity LED — no Uno pin needed
}

// ============================================================================
// SETUP
//
// Initializes all peripherals, verifies the CC2500, and sets the
// status LEDs. No Serial.println — the UART is the command channel.
// ============================================================================

void setup() {
    // UART to ESP32 (115200 baud, 8N1)
    // Bump to 250000 when scaling to 24 lamps
    Serial.begin(250000);

    // Initialize status LEDs (both off initially)
    pinMode(LED_OK_PIN, OUTPUT);
    pinMode(LED_ERR_PIN, OUTPUT);
    digitalWrite(LED_OK_PIN, LOW);
    digitalWrite(LED_ERR_PIN, LOW);

    // Initialize RF transmitter
    rf.begin(RF_TRANSMIT_PIN);

    // Initialize IR transmitter
    ir.begin(IR_TRANSMIT_PIN);

    // Initialize CC2500 radio
    radio.begin(CC2500_CS_PIN);

    // Verify and configure the radio
    if (radio.verifyRadio()) {
        radio.initializeRadio();
        radio.runStartupColorSequence();
        radioReady = true;
        digitalWrite(LED_OK_PIN, HIGH);   // GREEN solid: all good
    } else {
        digitalWrite(LED_ERR_PIN, HIGH);  // RED solid: CC2500 missing
    }

    // Schedule first RF test (only relevant when test block is active)
    lastRfTime = millis() - (RF_INTERVAL_MS - 5000);  // fire after 5 s
}

// ============================================================================
// LOOP
//
// The main loop does exactly one thing: drain the UART and process
// any received commands. The radio's GDO0 interrupt handles TX timing
// autonomously — no polling needed here.
//
// At 80 Hz lamp frames, each call to receiveCommands() reads ~80 bytes
// and triggers one sendFrame() (9.6 ms of radio air time).
// The loop runs ~80 times per second, each iteration taking ~10 ms.
// ============================================================================

void loop() {
    // Process all pending UART data
    receiveCommands();

    // ------------------------------------------------------------------
    // RF/IR TEST BLOCK (uncomment for testing, comment out for production)
    //
    // Sends a KAKU "on" code every 12 seconds, then "off" after 5 seconds.
    // Also sends corresponding IR codes.
    // ------------------------------------------------------------------
    /*
    const unsigned long now = millis();

    if (now - lastRfTime >= RF_INTERVAL_MS) {
        ir.sendHex(0xBF40FF00, 0);   // IR: power button
        rf.sendRaw(4477265, 24, 1);  // RF: turn on
        ir.sendHex(0xFD02EF00, 0);   // IR: off button

        rfPendingOff = true;
        rfOffAt = now + RF_OFF_DELAY_MS;
        lastRfTime = now;
    }

    if (rfPendingOff && now >= rfOffAt) {
        rf.sendRaw(4477268, 24, 1);  // RF: turn off
        ir.sendHex(0xFC03EF00, 0);   // IR: on button
        rfPendingOff = false;
    }
    */
}   