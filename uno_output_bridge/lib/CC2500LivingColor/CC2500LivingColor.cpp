#include "CC2500LivingColor.h"

// ============================================================================
// CC2500 REGISTER MAP (subset we use)
//
// The CC2500 has 64 addressable locations (0x00–0x3F).
// Addresses 0x00–0x2E are configuration registers.
// Addresses 0x30–0x31 are read-only status registers.
// Address  0x3F is the TX FIFO (write-only, burst capable).
//
// Command strobes (0x30–0x3F with bit 5 set) are one-shot commands:
//   SRES  = full chip reset
//   SCAL  = start frequency synthesizer calibration
//   SFLT  = start frequency offset calibration
//   STX   = start transmitting (only works from IDLE state)
//   SIDLE = enter IDLE state (safe state, no RF activity)
// ============================================================================

namespace {

// --- Configuration register addresses ---
constexpr uint8_t CC2500_REG_IOCFG2   = 0x00;  // GDO2 output pin configuration   
constexpr uint8_t CC2500_REG_IOCFG0   = 0x02;  // GDO0 pin function select
constexpr uint8_t CC2500_REG_FIFOTHR  = 0x03;  // TX/RX FIFO thresholds
constexpr uint8_t CC2500_REG_PKTLEN   = 0x06;  // packet length (fixed-length mode)
constexpr uint8_t CC2500_REG_PKTCTRL1 = 0x07;  // packet handling options
constexpr uint8_t CC2500_REG_PKTCTRL0 = 0x08;  // packet format, CRC enable
constexpr uint8_t CC2500_REG_ADDR     = 0x09;  // address for address-check feature
constexpr uint8_t CC2500_REG_CHANNR   = 0x0A;  // RF channel (0–255)
constexpr uint8_t CC2500_REG_FSCTRL1  = 0x0B;  // IF filter settings
constexpr uint8_t CC2500_REG_FSCTRL0  = 0x0C;  // frequency offset (calibrated by SFLT)
constexpr uint8_t CC2500_REG_FREQ2    = 0x0D;  // base frequency, high byte
constexpr uint8_t CC2500_REG_FREQ1    = 0x0E;  // base frequency, mid byte
constexpr uint8_t CC2500_REG_FREQ0    = 0x0F;  // base frequency, low byte
constexpr uint8_t CC2500_REG_MDMCFG4  = 0x10;  // channel bandwidth, data rate exponent
constexpr uint8_t CC2500_REG_MDMCFG3  = 0x11;  // data rate mantissa
constexpr uint8_t CC2500_REG_MDMCFG2  = 0x12;  // modulation format, sync mode
constexpr uint8_t CC2500_REG_MDMCFG1  = 0x13;  // FEC, preamble length, channel spacing exp
constexpr uint8_t CC2500_REG_MDMCFG0  = 0x14;  // channel spacing mantissa
constexpr uint8_t CC2500_REG_DEVIATN  = 0x15;  // FSK/MSK frequency deviation
constexpr uint8_t CC2500_REG_MCSM1    = 0x17;  // TX/RX state machine: post-TX/RX state
constexpr uint8_t CC2500_REG_MCSM0    = 0x18;  // TX/RX state machine: calibration behavior
constexpr uint8_t CC2500_REG_FOCCFG   = 0x19;  // frequency offset compensation
constexpr uint8_t CC2500_REG_BSCFG    = 0x1A;  // bit synchronization
constexpr uint8_t CC2500_REG_AGCCTRL2 = 0x1B;  // AGC target level
constexpr uint8_t CC2500_REG_AGCCTRL1 = 0x1C;  // AGC hysteresis
constexpr uint8_t CC2500_REG_AGCCTRL0 = 0x1D;  // AGC filtering, carrier sense
constexpr uint8_t CC2500_REG_FREND1   = 0x21;  // RX front-end (LNA, mixer, filter)
constexpr uint8_t CC2500_REG_FREND0   = 0x22;  // TX front-end (PA, mixer)
constexpr uint8_t CC2500_REG_FSCAL3   = 0x23;  // frequency synthesizer calibration
constexpr uint8_t CC2500_REG_FSCAL2   = 0x24;
constexpr uint8_t CC2500_REG_FSCAL1   = 0x25;
constexpr uint8_t CC2500_REG_FSCAL0   = 0x26;
constexpr uint8_t CC2500_REG_TEST2    = 0x2C;  // internal test
constexpr uint8_t CC2500_REG_TEST1    = 0x2D;
constexpr uint8_t CC2500_REG_TEST0    = 0x2E;
constexpr uint8_t CC2500_REG_PATABLE  = 0x3E;  // PA output power table
constexpr uint8_t CC2500_REG_TXFIFO   = 0x3F;  // TX FIFO (burst write)

// --- Read-only status registers ---
constexpr uint8_t CC2500_REG_PARTNUM  = 0x30;  // should read 0x80 (CC2500)
constexpr uint8_t CC2500_REG_VERSION  = 0x31;  // should read 0x03 (rev B)
constexpr uint8_t CC2500_REG_MARCSTATE = 0x44; // radio state machine state (read-only)
constexpr uint8_t CC2500_REG_TXBYTES  = 0x45;  // bytes currently in TX FIFO

// --- Command strobes (one-shot, do not persist) ---
constexpr uint8_t CC2500_CMD_SRES   = 0x30;  // reset entire chip
constexpr uint8_t CC2500_CMD_SCAL   = 0x33;  // start frequency synthesizer calibration
constexpr uint8_t CC2500_CMD_SFLT   = 0x34;  // start frequency offset calibration
constexpr uint8_t CC2500_CMD_STX    = 0x35;  // start TX (only from IDLE)
constexpr uint8_t CC2500_CMD_SIDLE  = 0x36;  // enter IDLE state

// --- SPI command modifiers ---
constexpr uint8_t CC2500_OFF_READ_BURST  = 0xC0;  // OR with address to read burst
constexpr uint8_t CC2500_OFF_WRITE_BURST = 0x40;  // OR with address to write burst

// --- SPI speed ---
// The CC2500 supports up to 8 MHz SPI clock.
// 8 MHz is the max the CC2500 datasheet specifies.
// If you get flaky reads/writes, drop to 4 MHz.
constexpr uint32_t SPI_SPEED = 8000000;

// --- Lamp addresses (9 bytes each: 6-byte IEEE address + 3 bytes overhead) ---
// The first 6 bytes are the lamp's unique 48-bit IEEE address.
// Bytes 7–9 are protocol-specific (channel, group, etc.)
const uint8_t LAMP_ADDRESSES[CC2500LivingColor::LAMP_COUNT][9] = {
    {0xD3, 0x06, 0x8E, 0x43, 0x6D, 0x92, 0xE5, 0x0C, 0x11},
    {0x94, 0x4B, 0x2D, 0x30, 0x6D, 0x92, 0xE5, 0x0C, 0x11},
    {0x04, 0x11, 0x68, 0x2E, 0x6D, 0x92, 0xE5, 0x0C, 0x11},
    {0x60, 0xA7, 0x3B, 0x0A, 0x6D, 0x92, 0xE5, 0x0C, 0x11},
    {0xB0, 0x56, 0x86, 0x01, 0x6D, 0x92, 0xE5, 0x0C, 0x11},
    {0x5B, 0xC8, 0x15, 0x63, 0x6D, 0x92, 0xE5, 0x0C, 0x11},
    {0x73, 0x34, 0x63, 0x73, 0x78, 0x53, 0x51, 0x2E, 0x11},
    {0xF8, 0x6F, 0x21, 0x31, 0x78, 0x53, 0x51, 0x2E, 0x11},
    {0x1C, 0x27, 0x2F, 0x1B, 0x78, 0x53, 0x51, 0x2E, 0x11},
    {0x8B, 0x73, 0xA3, 0x18, 0xAD, 0xFA, 0x3F, 0x50, 0x11},
    {0x2F, 0x06, 0xE6, 0x30, 0xAD, 0xFA, 0x3F, 0x50, 0x11},
    {0x9F, 0x5B, 0x9A, 0x02, 0xAD, 0xFA, 0x3F, 0x50, 0x11}
};

// Packet payload size (everything after the length byte)
constexpr uint8_t PAYLOAD_SIZE = 14;  // 9 addr + 1 cmd + 1 seq + 1 H + 1 S + 1 V
// Total bytes written to TX FIFO per packet (length byte + payload)
constexpr uint8_t FIFO_PACKET_SIZE = 1 + PAYLOAD_SIZE;  // = 15 bytes

} // anonymous namespace


// ============================================================================
// GDO0 INTERRUPT SERVICE ROUTINE
//
// GDO0 is configured (IOCFG0 = 0x06) to:
//   - go HIGH when the sync word is transmitted
//   - go LOW  when the packet is fully transmitted (TX complete)
//
// We attach a FALLING-edge interrupt so this fires at the exact moment
// the radio finishes sending. This is how we know the TX FIFO is free
// and the radio is back in IDLE, ready for the next packet.
//
// IMPORTANT: Keep this as short as possible. Just set a flag.
// The main loop (or sendPacket) checks the flag.
// ============================================================================

// We need a way to access the instance from the free-function ISR.
// Simplest approach for a single-instance library: a static pointer.
static CC2500LivingColor* _instance = nullptr;

void cc2500TxCompleteISR() {
    if (_instance) {
        _instance->markTxComplete();
    }
}   

// (declared in .h as friend, defined here)
void CC2500LivingColor::markTxComplete() {
    _txComplete = true;
}

// Helper to set the instance pointer (called from begin())
void CC2500LivingColor::setInstance(CC2500LivingColor* inst) {
    _instance = inst;
}


// ============================================================================
// begin()
//
// Initializes SPI, the CS pin, and the GDO0 interrupt.
// Call this ONCE at startup before anything else.
// ============================================================================
void CC2500LivingColor::begin(uint8_t csPin) {
    _csPin = csPin;
    setInstance(this);  // so the ISR can find us

    // CS is active-LOW: hold HIGH when not talking to the CC2500
    pinMode(_csPin, OUTPUT);
    digitalWrite(_csPin, HIGH);

    // Start the hardware SPI peripheral
    SPI.begin();

    // GDO0 pin: input with internal pull-up (GDO0 is open-drain,
    // so it pulls LOW when active and floats HIGH otherwise)
    pinMode(GDO0_PIN, INPUT_PULLUP);

    // Attach external interrupt 0 (pin 2) on FALLING edge.
    // This fires when GDO0 goes LOW = TX complete.
    attachInterrupt(digitalPinToInterrupt(GDO0_PIN), cc2500TxCompleteISR, FALLING);

    // Mark TX as "complete" initially so the first sendPacket doesn't wait
    _txComplete = true;
}


// ============================================================================
// writeRegister()
//
// Writes a single byte to a CC2500 configuration register.
//
// SPI protocol for a register write:
//   Bit 7:0 of first byte = 0 0 R A A A A A
//     R = 0 (write), A = 6-bit register address
//   Second byte = data to write
//
// Example: write 0x06 to IOCFG0 (address 0x02):
//   First byte:  0b00000010 = 0x02
//   Second byte: 0x06
// ============================================================================
void CC2500LivingColor::writeRegister(uint8_t address, uint8_t value) {
    SPI.beginTransaction(SPISettings(SPI_SPEED, MSBFIRST, SPI_MODE0));
    digitalWrite(_csPin, LOW);
    SPI.transfer(address);       // register address (write mode, single)
    SPI.transfer(value);         // data byte
    digitalWrite(_csPin, HIGH);
    SPI.endTransaction();
}


// ============================================================================
// readStatusRegister()
//
// Reads a single byte from a CC2500 register (config or status).
//
// SPI protocol for a register read:
//   Bit 7:0 of first byte = 0 0 R A A A A A
//     R = 0 (read), A = 6-bit register address
//   OR with 0xC0 to set the burst/read bits: 1 1 0 A A A A A
//   Second byte = dummy (clocks out the data)
//
// For status registers (0x40–0x4F), the address already has bit 6 set,
// so ORing with 0xC0 gives 0xC0 | 0x44 = 0xE4, which is correct.
// ============================================================================
uint8_t CC2500LivingColor::readStatusRegister(uint8_t address) {
    SPI.beginTransaction(SPISettings(SPI_SPEED, MSBFIRST, SPI_MODE0));
    digitalWrite(_csPin, LOW);
    SPI.transfer(address | CC2500_OFF_READ_BURST);  // read command
    const uint8_t value = SPI.transfer(0x00);       // dummy byte clocks out data
    digitalWrite(_csPin, HIGH);
    SPI.endTransaction();
    return value;
}


// ============================================================================
// sendStrobe()
//
// Sends a one-shot command to the CC2500 state machine.
// Strobe addresses have bit 5 set (0x30–0x3F range with bit 5 = 1).
//
// NO delay after the strobe. The CC2500 processes the command
// within a few SPI clock cycles. The only exception is SRES,
// which we handle separately in initializeRadio() with an explicit wait.
// ============================================================================
void CC2500LivingColor::sendStrobe(uint8_t strobe) {
    SPI.beginTransaction(SPISettings(SPI_SPEED, MSBFIRST, SPI_MODE0));
    digitalWrite(_csPin, LOW);
    SPI.transfer(strobe);
    digitalWrite(_csPin, HIGH);
    SPI.endTransaction();
    // No delay here! The radio acts on the strobe immediately.
    // For SRES, we add a delay at the call site because the chip
    // needs ~128 µs to finish its internal reset.
}


// ============================================================================
// waitTxComplete()
//
// Spins (busy-waits) until the GDO0 interrupt sets _txComplete.
//
// In practice this loop almost never executes more than 0–1 iterations
// because the air time (800 µs) is much longer than the time it takes
// to prepare the next packet (15 µs SPI write). By the time we call
// this for packet N+1, packet N has long since finished transmitting.
//
// Worst case (first packet after a long idle): we wait up to 800 µs.
// ============================================================================
void CC2500LivingColor::waitTxComplete() {
    while (!_txComplete) {
        // Just spin. Max wait is one air-time (800 µs at 250 kbps).
        // The ISR will set _txComplete = true when GDO0 goes LOW.
    }
    _txComplete = false;  // reset for next packet
}


// ============================================================================
// verifyRadio()
//
// Checks that a CC2500 is actually connected and responding.
// Reads PARTNUM (should be 0x80) and VERSION (should be 0x03).
// Retries 3 times in case of a slow power-up.
// ============================================================================
bool CC2500LivingColor::verifyRadio() {
    for (uint8_t attempt = 0; attempt < 3; ++attempt) {
        // Full reset to get the chip into a known state
        sendStrobe(CC2500_CMD_SRES);
        delay(1);  // give the chip 1 ms to finish resetting

        // Read the two identification registers
        if (readStatusRegister(CC2500_REG_PARTNUM) == 0x80 &&
            readStatusRegister(CC2500_REG_VERSION) == 0x03) {
            return true;  // CC2500 rev B detected
        }
        delay(10);  // wait 10 ms before retry
    }
    return false;
}


// ============================================================================
// initializeRadio()
//
// Configures the CC2500 for 2.4 GHz O-QPSK, 250 kbps, channel 3.
// These register values come from the SmartRF Studio / CC2500 config.
//
// Key settings explained:
//   - FREQ2/1/0: sets the base RF frequency (~2405 MHz + channel × 5 MHz)
//   - MDMCFG4/3: data rate = 250 kbps (O-QPSK)
//   - MDMCFG2:  O-QPSK modulation, 4-byte sync word
//   - MDMCFG1:  4-byte preamble, no FEC
//   - MCSM1:    TX complete → IDLE (important! radio auto-returns to idle)
//   - IOCFG0:   GDO0 = "assert on sync sent, de-assert on TX complete"
//   - PKTCTRL0: variable length, CRC enabled, no address check
//   - FIFOTHR:  TX threshold = 0 (we don't use GDO2), RX threshold = 13
// ============================================================================
void CC2500LivingColor::initializeRadio() {
    // --- Full reset ---
    sendStrobe(CC2500_CMD_SRES);
    delayMicroseconds(150);  // chip needs ~128 µs to finish reset

    // --- Frequency synthesizer ---
    // FSCTRL1/0: IF filter and frequency offset (FSCTRL0 is auto-calibrated by SFLT)
    writeRegister(CC2500_REG_FSCTRL1, 0x09);
    writeRegister(CC2500_REG_FSCTRL0, 0x00);

    // FREQ2/1/0: base frequency control word.
    // Combined with CHANNR, determines the actual RF frequency.
    // Formula: f_RF = 2^28 * f_XTAL * (FREQ + CHANNR * 2^18) / f_SYNTH
    writeRegister(CC2500_REG_FREQ2, 0x5D);
    writeRegister(CC2500_REG_FREQ1, 0x93);
    writeRegister(CC2500_REG_FREQ0, 0xB1);

    // --- Modem configuration ---
    // MDMCFG4: channel filter bandwidth (250 kHz) and data rate exponent
    writeRegister(CC2500_REG_MDMCFG4, 0x2D);
    // MDMCFG3: data rate mantissa → combined with MDMCFG4 gives 250 kbps
    writeRegister(CC2500_REG_MDMCFG3, 0x3B);
    // MDMCFG2: O-QPSK modulation, 4-byte sync word, sync word auto-detection
    writeRegister(CC2500_REG_MDMCFG2, 0x73);
    // MDMCFG1: 4-byte preamble, no FEC, channel spacing exponent
    writeRegister(CC2500_REG_MDMCFG1, 0x22);
    // MDMCFG0: channel spacing mantissa (5 MHz between channels)
    writeRegister(CC2500_REG_MDMCFG0, 0xF8);
    // CHANNR: RF channel number (0–255). Channel 3 = 2405 + 3×5 = 2420 MHz
    writeRegister(CC2500_REG_CHANNR, 0x03);
    // DEVIATN: frequency deviation for FSK/MSK (unused for O-QPSK)
    writeRegister(CC2500_REG_DEVIATN, 0x00);

    // --- Front-end (TX/RX analog) ---
    // FREND1: RX LNA gain, mixer gain, IF filter (optimized for 250 kbps O-QPSK)
    writeRegister(CC2500_REG_FREND1, 0xB6);
    // FREND0: TX PA gain, mixer settings
    writeRegister(CC2500_REG_FREND0, 0x10);

    // --- State machine (MCSM) ---
    // MCSM0: calibration behavior (auto-calibrate on every TX)
    writeRegister(CC2500_REG_MCSM0, 0x00);
    // MCSM1: post-TX state = IDLE (0x0C).
    // This is critical: after each packet, the radio automatically
    // returns to IDLE, ready for the next STX. No manual SFTX needed.
    writeRegister(CC2500_REG_MCSM1, 0x0C);

    // --- Frequency offset compensation ---
    writeRegister(CC2500_REG_FOCCFG, 0x1D);
    writeRegister(CC2500_REG_BSCFG, 0x1C);

    // --- AGC (automatic gain control) ---
    writeRegister(CC2500_REG_AGCCTRL2, 0xC7);
    writeRegister(CC2500_REG_AGCCTRL1, 0x00);
    writeRegister(CC2500_REG_AGCCTRL0, 0xB2);

    // --- Frequency synthesizer calibration ---
    writeRegister(CC2500_REG_FSCAL3, 0xEA);
    writeRegister(CC2500_REG_FSCAL2, 0x0A);
    writeRegister(CC2500_REG_FSCAL1, 0x00);
    writeRegister(CC2500_REG_FSCAL0, 0x1F);

    // --- Internal test registers (vendor-recommended values) ---
    writeRegister(CC2500_REG_TEST2, 0x88);
    writeRegister(CC2500_REG_TEST1, 0x31);
    writeRegister(CC2500_REG_TEST0, 0x0B);

    // --- GDO0 pin configuration ---
    // 0x06 = "Assert when sync word is sent (value 0), De-assert when TX complete (value 6)"
    // This means: GDO0 goes HIGH at start of TX, goes LOW when packet is fully sent.
    // We use the falling edge (HIGH→LOW) as our "TX done" signal.
    writeRegister(CC2500_REG_IOCFG0, 0x06);
    
    // --- GDO2 pin configuration ---
    // Per packet: LED on for ~693 µs, off for ~107 µs.
    // Per frame (12 packets): LED on ~8.3 ms, off ~1.3 ms.
    // Result: a steady glow at ~86% brightness while frames are being sent, dark when idle. 
    // Exactly an "active" indicator.
    writeRegister(CC2500_REG_IOCFG2, 0x04); // GDO2: HIGH when TX FIFO ≥ threshold, LOW when below
    writeRegister(CC2500_REG_FIFOTHR, 0xE0);  // TX threshold = 14, RX threshold = 0   

    // --- Packet configuration ---
    // PKTCTRL1: 0x04 = no address check, no status byte appended
    writeRegister(CC2500_REG_PKTCTRL1, 0x04);
    // PKTCTRL0: 0x45 = variable length (first byte = length), CRC-16 enabled,
    //           no whitening, no address check
    writeRegister(CC2500_REG_PKTCTRL0, 0x45);
    // ADDR: unused (address check disabled)
    writeRegister(CC2500_REG_ADDR, 0x00);
    // PKTLEN: unused in variable-length mode (the length byte in the packet controls it)
    writeRegister(CC2500_REG_PKTLEN, 0xFF);
    // FIFOTHR: upper nibble = TX FIFO threshold (0 = no GDO2 TX assertion),
    //          lower nibble = RX FIFO threshold (13 = unused, we don't receive)
    writeRegister(CC2500_REG_FIFOTHR, 0x0D);

    // --- PA power ---
    // PATABLE: 0xFF = maximum output power (0 dBm)
    writeRegister(CC2500_REG_PATABLE, 0xFF);

    // --- Run calibration ---
    // SCAL: calibrate the frequency synthesizer (PLL lock)
    sendStrobe(CC2500_CMD_SCAL);
    delayMicroseconds(200);  // wait for calibration to finish (~100 µs typical)

    // SFLT: calibrate the frequency offset (writes result to FSCTRL0)
    sendStrobe(CC2500_CMD_SFLT);
    delayMicroseconds(200);

    // Enter IDLE state — radio is now ready to transmit
    sendStrobe(CC2500_CMD_SIDLE);
}


// ============================================================================
// sendPacket()
//
// Transmits a single 15-byte packet to one lamp.
//
// Packet format (15 bytes total in the TX FIFO):
//   [0]    = length byte (0x0E = 14, the size of the payload that follows)
//   [1–9]  = 9-byte lamp address (6-byte IEEE + 3 protocol bytes)
//   [10]   = command (0x03 = set color, 0x05 = standby, etc.)
//   [11]   = sequence number (increments each packet, helps detect drops)
//   [12]   = hue (0–255)
//   [13]   = saturation (0–255)
//   [14]   = value/brightness (0–255)
//
// On the air, the CC2500 automatically adds:
//   4 bytes preamble + 4 bytes sync word + 2 bytes CRC-16
//   Total on air: 4 + 4 + 1 + 14 + 2 = 25 bytes = 200 bits
//   At 250 kbps: 200 / 250,000 = 800 µs per packet
//
// Flow:
//   1. Wait for previous packet to finish (GDO0 interrupt sets flag)
//   2. Write 15 bytes to TX FIFO via SPI burst
//   3. Issue STX strobe → radio starts transmitting
//   4. Return immediately (radio works autonomously)
// ============================================================================
void CC2500LivingColor::sendPacket(uint8_t lampIndex, uint8_t command,
                                    uint8_t hue, uint8_t saturation, uint8_t value) {
    // Wait for the previous packet to finish transmitting.
    // On the very first call, _txComplete is already true (set in begin()),
    // so this is a no-op.
    waitTxComplete();

    // Build the 15-byte packet in a local buffer
    uint8_t packet[FIFO_PACKET_SIZE];
    packet[0] = PAYLOAD_SIZE;  // length byte: 14 bytes follow
    memcpy(&packet[1], LAMP_ADDRESSES[lampIndex], 9);  // lamp address
    packet[10] = command;
    packet[11] = _sequenceNumber++;  // auto-incrementing sequence
    packet[12] = hue;
    packet[13] = saturation;
    packet[14] = value;

    // Write the entire packet to the TX FIFO in one SPI burst.
    // Command byte: 0x3F | 0x40 = 0x7F (TX FIFO burst write)
    // Then 15 data bytes follow on the same CS-low period.
    SPI.beginTransaction(SPISettings(SPI_SPEED, MSBFIRST, SPI_MODE0));
    digitalWrite(_csPin, LOW);
    SPI.transfer(CC2500_REG_TXFIFO | CC2500_OFF_WRITE_BURST);  // 0x7F

    for (uint8_t i = 0; i < FIFO_PACKET_SIZE; ++i) {
                delayMicroseconds(150);  // chip needs ~128 µs to finish reset

        SPI.transfer(packet[i]);
    }
    digitalWrite(_csPin, HIGH);
    SPI.endTransaction();

    // Fire the transmit. The radio grabs the data from the FIFO,
    // prepends preamble + sync, appends CRC, and starts sending.
    // It will automatically return to IDLE when done (MCSM1 = 0x0C),
    // and GDO0 will go LOW, triggering our interrupt.
    sendStrobe(CC2500_CMD_STX);
}


// ============================================================================
// sendFrame()
//
// Sends all 12 lamps back-to-back with no gaps.
// This is the main workhorse for the rainbow effect.
//
// Total time: 12 × 800 µs = 9.6 ms (radio is the bottleneck, not the Uno)
// The Uno is free to do other things between packets (drain UART, etc.)
//
// For even higher throughput, you could pre-load 4 packets into the FIFO
// (64-byte FIFO holds 4 × 15 = 60 bytes) and let the radio chain them
// automatically. Then the ISR refills as each one completes.
// That's the "full pipeline" optimization — see comments at the end of this file.
// ============================================================================
void CC2500LivingColor::sendFrame(const uint8_t hue[LAMP_COUNT],
                                   const uint8_t sat[LAMP_COUNT],
                                   const uint8_t val[LAMP_COUNT]) {
    for (uint8_t i = 0; i < LAMP_COUNT; ++i) {
        sendPacket(i, 0x03, hue[i], sat[i], val[i]);
        
    }
}


// ============================================================================
// runStartupColorSequence()
//
// One-time startup show:
//   1. Set all lamps to near-black (standby)
//   2. Set all lamps to random colors
//
// Uses delays between packets because this runs once at boot and
// the visual effect benefits from a sequential "wave" appearance.
// Not performance-critical.
// ============================================================================
void CC2500LivingColor::runStartupColorSequence() {
    constexpr unsigned long PAUSE_MS = 65;  // ~1.25× air time, gives a visible wave

    delay(500);  // let the system settle before starting

    // Phase 1: all lamps to near-black
    for (uint8_t i = 0; i < LAMP_COUNT; ++i) {
        sendPacket(i, 0x05, 0, 255, 1);  // standby / near-black
        delay(PAUSE_MS);
    }

    // Phase 2: all lamps to random color
    for (uint8_t i = 0; i < LAMP_COUNT; ++i) {
        sendPacket(i, 0x03, static_cast<uint8_t>(random(256)), 255, 100);
        delay(PAUSE_MS);
    }
}

// ============================================================================
// FUTURE OPTIMIZATION: Full FIFO Pipeline
//
// The current sendPacket() is simple and correct: wait → write → STX.
// Since air time (800 µs) >> SPI write time (15 µs), the wait is almost
// always instant. This is fine for 12 lamps at 80+ Hz.
//
// If you ever need to push the limits (e.g., 20+ lamps, or higher data
// rates), you can pre-load the FIFO with 4 packets at once:
//
//   1. Write 4 × 15 = 60 bytes to TX FIFO in one SPI burst
//   2. Issue STX once
//   3. Radio transmits pkt1, pkt2, pkt3, pkt4 automatically (no new STX needed)
//   4. Each time GDO0 goes LOW (one packet done), ISR writes the next
//      15 bytes into the freed FIFO space
//   5. Repeat until all packets are sent
//
// This eliminates even the tiny "wait" between packets and makes the
// stream perfectly gapless. For 12 lamps at 80 Hz you don't need it,
// but it's a good next step to learn.
// 
// Special thanks to 'Brave Search' for all the help and explaining about the CC2500 
// and it's special registers and packet format to make it work as fast as posible 
// with 'Philips Living Color' lamps from 2006 (the first generation of these lamps pre Hue)
// helping creating this gorgeous library with every section commented and 
// explained in detail for everyone to understand and use it.
// 
// ============================================================================   