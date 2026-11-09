#pragma once
#include <Arduino.h>
#include <SPI.h>

// ============================================================================
// CC2500LivingColor
//
// Drives Living Color lamps via a CC2500 2.4 GHz radio.
//
// Hardware wiring (Uno side):
//   - CS  → pin 10  (any digital pin, just keep it consistent)
//   - SCK → pin 13  (Uno hardware SPI)
//   - MISO→ pin 12  (unused — TX only)
//   - MOSI→ pin 11  (Uno hardware SPI)
//   - GDO0→ pin 2   (external interrupt 0 — TX complete signal)
//   - GND → GND
//
// The CC2500 GDO0 pin is configured to go LOW when a packet finishes
// transmitting. We catch that falling edge with an interrupt so the Uno
// knows exactly when the radio is free — no polling, no guessing.
// ============================================================================

class CC2500LivingColor {
public:
    static constexpr uint8_t LAMP_COUNT = 12;   // number of lamps in the system

    // --- Public API ---

    void begin(uint8_t csPin);                  // init SPI, CS, and GDO0 interrupt
    bool verifyRadio();                         // check that a CC2500 is actually present
    void initializeRadio();                     // configure all CC2500 registers
    void runStartupColorSequence();             // one-time startup light show
    void markTxComplete();

    void sendPacket(uint8_t lampIndex, uint8_t command,
                    uint8_t hue, uint8_t saturation, uint8_t value);
    void sendFrame(const uint8_t hue[LAMP_COUNT],
                   const uint8_t sat[LAMP_COUNT],
                   const uint8_t val[LAMP_COUNT]);  // send all lamps back-to-back

private:
    // --- Pins ---
    uint8_t _csPin = 10;          // chip select (active LOW)
    static constexpr uint8_t GDO0_PIN = 2;  // must be an external-interrupt pin (2 or 3)

    // --- State ---
    uint8_t _sequenceNumber = 0;  // increments per packet, helps lamps detect duplicates
    volatile bool _txComplete = true;  // set by ISR when GDO0 goes LOW (TX done)

    // --- Private helpers ---
    void writeRegister(uint8_t address, uint8_t value);
    uint8_t readStatusRegister(uint8_t address);
    void sendStrobe(uint8_t strobe);
    void waitTxComplete();         // spin until _txComplete is set (max ~800 µs)
    static void setInstance(CC2500LivingColor* inst);


    // --- ISR (must be a free function, not a method) ---
    // Defined in the .cpp; declared here so begin() can attach it.
    friend void cc2500TxCompleteISR();
};   