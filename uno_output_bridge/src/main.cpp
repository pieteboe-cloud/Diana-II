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
//   IR:      A0 (14) → IR blaster data pin
//   RF:      A1 (15) → RF module data pin
//   LED OK:  A2 (16) → green LED (220Ω to 5V)  heartbeat when ready
//   LED ERR: A3 (17) → red LED (220Ω to 5V)    solid=fail, flash=busy
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

// Mark the red "busy" LED as active for a short flash.
void setBusy(unsigned long ms);

// ============================================================================
// PIN DEFINITIONS
// ============================================================================

// --- CC2500 ,RF ,IR Arduino UNO pins 
// SPI pins are fixed by Uno hardware: SCK=13, MOSI=11, MISO=12
// CS is configurable, GDO0 is hardcoded in the library (pin 2)
// A0-A5 are used as digital pins 14-19 on the Uno
constexpr uint8_t CC2500_CS_PIN = 10;
constexpr uint8_t IR_TRANSMIT_PIN = 14;   // A0
constexpr uint8_t RF_TRANSMIT_PIN = 4;   // 
constexpr uint8_t LED_OK_PIN  = 16;       // A2 — green: heartbeat when ready
constexpr uint8_t LED_ERR_PIN = 17;       // A3 — red: solid=fail, flash=busy

// ============================================================================
// PROTOCOL CONSTANTS
// ============================================================================

// Line buffer size: "O,LAMP," + 12×3 values + 11 commas + "\n" ≈ 151 bytes.
// 192 gives headroom for the full frame plus future commands.
constexpr uint16_t LINE_BUFFER_SIZE = 192;

// Lamp count (from the radio library)
constexpr uint8_t LAMP_COUNT = CC2500LivingColor::LAMP_COUNT;

// Heartbeat period for the green OK LED (milliseconds per full blink cycle).
constexpr unsigned long HEARTBEAT_MS = 1000;

// How long the red ERR LED stays on for each busy flash (milliseconds).
constexpr unsigned long BUSY_FLASH_MS = 80;

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

// Green heartbeat state
unsigned long lastHeartbeatToggle = 0;
bool heartbeatOn = false;

// Red busy-flash state
unsigned long busyUntil = 0;

// ============================================================================
// LED HELPERS
// ============================================================================

// Call this whenever the Uno is doing work (UART command, RF, IR, CC2500).
// The red LED will stay on for BUSY_FLASH_MS milliseconds.
void setBusy(unsigned long ms) {
    busyUntil = millis() + ms;
    digitalWrite(LED_ERR_PIN, HIGH);
}

// Update both LEDs non-blockingly. Call from loop() every iteration.
void updateLeds() {
    const unsigned long now = millis();

    // ---- Green OK LED: heartbeat ----
    if (radioReady) {
        // Toggle every half-period so a full blink takes HEARTBEAT_MS.
        if (now - lastHeartbeatToggle >= HEARTBEAT_MS / 2) {
            heartbeatOn = !heartbeatOn;
            digitalWrite(LED_OK_PIN, heartbeatOn ? HIGH : LOW);
            lastHeartbeatToggle = now;
        }
    } else {
        // Not ready: blink red fast to show CC2500 is missing.
        if (now - lastHeartbeatToggle >= 150) {
            heartbeatOn = !heartbeatOn;
            digitalWrite(LED_ERR_PIN, heartbeatOn ? HIGH : LOW);
            lastHeartbeatToggle = now;
        }
        // Keep green off while in error state.
        digitalWrite(LED_OK_PIN, LOW);
        return;
    }

    // ---- Red ERR LED: busy flash ----
    // Only touch the red pin if we are not in the fast-error blink above.
    if (now < busyUntil) {
        digitalWrite(LED_ERR_PIN, HIGH);
    } else {
        digitalWrite(LED_ERR_PIN, LOW);
    }
}

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
            setBusy(BUSY_FLASH_MS);   // flash red: a command arrived
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
    setBusy(BUSY_FLASH_MS);   // flash red: CC2500 is transmitting
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
    setBusy(BUSY_FLASH_MS);   // flash red: IR is transmitting
    ir.sendHex(code, 0);
    // IR blaster has its own activity LED — no Uno pin needed
}

// ============================================================================
// RF HANDLER
//
// Parses a decimal string and sends it as an RF code.
// Input: "4477265" → rf.sendRaw(4477265, 24, 1)
// ============================================================================

void handleRF(char* hexStr) {
    // RF codes are decimal (e.g. 4477265, 4477268), not hex.
    uint32_t code = (uint32_t)strtoul(hexStr, nullptr, 10);
    setBusy(BUSY_FLASH_MS);   // flash red: RF is transmitting
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
    // UART to ESP32 (250000 baud, 8N1)
    Serial.begin(250000);

    // Initialize PINS as outputs and set them LOW
    
    pinMode(IR_TRANSMIT_PIN, OUTPUT);
    pinMode(RF_TRANSMIT_PIN, OUTPUT);
    digitalWrite(IR_TRANSMIT_PIN, LOW);
    digitalWrite(RF_TRANSMIT_PIN, LOW);
    
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
        // Green heartbeat starts in updateLeds(); red stays off.
    } else {
        radioReady = false;
        // Red fast-blink starts in updateLeds(); user can see CC2500 is missing.
    }

    lastHeartbeatToggle = millis();
}

// ============================================================================
// LOOP
//
// The main loop does exactly one thing: drain the UART and process
// any received commands. The radio's GDO0 interrupt handles TX timing
// autonomously — no polling needed here.
//
// LEDs are updated every iteration so the heartbeat and busy flashes
// run without blocking command processing.
//
// At 80 Hz lamp frames, each call to receiveCommands() reads ~80 bytes
// and triggers one sendFrame() (9.6 ms of radio air time).
// ============================================================================

void loop() {
    // Process all pending UART data
    receiveCommands();

    // Keep status LEDs blinking without blocking.
    updateLeds();
}
