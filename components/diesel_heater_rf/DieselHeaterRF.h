/*
 * DieselHeaterRF.h
 * Copyright (c) 2020 Jarno Kyttälä
 * https://github.com/jakkik/DieselHeaterRF
 *
 * Modified by Dimitri Duro (2026):
 *   - Added configurable SPI frequency via setFrequency() / _freq2/_freq1/_freq0 members
 *   - Added reinitRadio() public method for runtime CC1101 recovery without SPI reinit
 *   - Added getMarcstate(), readConfigReg(), getFreqRegisters() for health-check and diagnostics
 *   - Added getLastBurstCompleted() / getLastBurstRequested() for TX verification
 *   - Added receiveRaw() for raw packet capture (debug mode)
 *   - Rewrote sendCommand(): CCA_MODE=3 (RSSI+no packet), TXOFF_MODE=FSTXON for locked
 *     synthesizer between packets in a burst (single calibration, no frequency drift);
 *     explicit IDLE+FSTXON wait loop replaces timing-based delays
 *   - CRC check in receivePacket() switched to hardware CRC (APPEND_STATUS bit7);
 *     software crc16_2 positions don't match this heater variant
 *   - FOCCFG 0x16→0x17 (FOC_LIMIT ±BW/4 → ±BW/2) for better frequency-offset tolerance
 *   - MDMCFG1 NUM_PREAMBLE set to match original remote (4 bytes)
 *   - MCSM1 CCA_MODE configurable via setCcaMode() (default 0 = always TX)
 *   - readPacket() now parses buf[11] as errorCode into heater_state_t
 *
 * Feel free to use this library as you please, but do it at your own risk!
 */

#ifndef DieselHeaterRF_h
#define DieselHeaterRF_h

#include <stdint.h>
#include <string.h>
#include "driver/spi_master.h"

#define HEATER_SCK_PIN   18
#define HEATER_MISO_PIN  19
#define HEATER_MOSI_PIN  23
#define HEATER_SS_PIN    5
#define HEATER_GDO2_PIN  4

#define HEATER_CMD_GET_STATUS 0x23
#define HEATER_CMD_MODE   0x24
#define HEATER_CMD_POWER  0x2b
#define HEATER_CMD_UP     0x3c
#define HEATER_CMD_DOWN   0x3e

#define HEATER_STATE_OFF            0x00
#define HEATER_STATE_STARTUP        0x01
#define HEATER_STATE_WARMING        0x02
#define HEATER_STATE_WARMING_WAIT   0x03
#define HEATER_STATE_PRE_RUN        0x04
#define HEATER_STATE_RUNNING        0x05
#define HEATER_STATE_SHUTDOWN       0x06
#define HEATER_STATE_SHUTTING_DOWN  0x07
#define HEATER_STATE_COOLING        0x08

#define HEATER_TX_REPEAT    10 // Number of times to re-transmit command packets
#define HEATER_RX_TIMEOUT   5000

typedef struct {
  uint8_t state       = 0;
  uint8_t power       = 0;
  uint8_t errorCode   = 0;
  float voltage       = 0;
  int8_t ambientTemp  = 0;
  uint8_t caseTemp    = 0;
  int8_t setpoint     = 0;
  uint8_t autoMode    = 0;
  float pumpFreq      = 0;
  int16_t rssi        = 0;
} heater_state_t;

class DieselHeaterRF {
  public:
    DieselHeaterRF() {
      _pinSck = HEATER_SCK_PIN;
      _pinMiso = HEATER_MISO_PIN;
      _pinMosi = HEATER_MOSI_PIN;
      _pinSs = HEATER_SS_PIN;
      _pinGdo2 = HEATER_GDO2_PIN;
    }
    DieselHeaterRF(uint8_t sck, uint8_t miso, uint8_t mosi, uint8_t ss, uint8_t gdo2) {
      _pinSck = sck;
      _pinMiso = miso;
      _pinMosi = mosi;
      _pinSs = ss;
      _pinGdo2 = gdo2;
    }
    ~DieselHeaterRF() {}

    void begin();
    void begin(uint32_t heaterAddr);
    void setAddress(uint32_t heaterAddr);
    void setFrequency(uint8_t freq2, uint8_t freq1, uint8_t freq0);
    void setCcaMode(uint8_t mode) { _ccaMode = mode & 0x03; }
    void setTxPower(uint8_t index) { _txPower = index & 0x07; }
    void reinitRadio() { initRadio(); }

    // Blocking TX — sends numTransmits packets with Phase 1/Phase 2 MARCSTATE polling.
    // Caller provides seq# explicitly; use nextSeq() for a new seq, or reuse for retransmit.
    void sendCommand(uint8_t cmd, uint32_t addr, uint8_t numTransmits, uint8_t seq);
    uint8_t nextSeq() { return _packetSeq++; }

    void endTxBurst();

    // Non-blocking RX —————————————————————————————————————
    // rxFlush + rxEnable — puts CC1101 into RX mode (requires calibration).
    void startRx();
    // SRX from FSTXON — enters RX directly without calibration (synth already locked).
    void startRxFromFstxon();
    // True if GDO2 is high (packet in RX FIFO).
    bool isRxAvailable();
    // Read RX FIFO, validate CRC and address, parse into state. Non-blocking.
    // Returns false if FIFO size wrong, CRC fail, or address mismatch; calls rxFlush() on failure.
    bool readPacket(heater_state_t *state);

    // Diagnostics —————————————————————————————————————————
    uint8_t getPartNum();
    uint8_t getVersion();
    void getFreqRegisters(uint8_t *freq2, uint8_t *freq1, uint8_t *freq0);
    uint8_t getMarcstate();
    uint8_t getRxBytes() { return writeReg(0xFB, 0xFF); }  // RXBYTES status register
    uint8_t readConfigReg(uint8_t addr) { return writeReg(addr | 0x80, 0xFF); }
    uint8_t getLastBurstCompleted() const { return _lastBurstCompleted; }
    uint8_t getLastBurstRequested() const { return _lastBurstRequested; }
    uint8_t getLastRxEntryState() const { return _lastRxEntryState; }
    uint8_t getLastP1First() const { return _lastP1First; }
    uint8_t getLastP1Last() const { return _lastP1Last; }
    void calibrate() { writeStrobe(0x33); }  // SCAL — manual frequency calibration
    void sidle()    { writeStrobe(0x36); }  // SIDLE — force chip to IDLE state
    // Blocking raw RX — for debug/find_address only.
    bool receiveRaw(char *bytes, uint8_t *len, uint16_t timeout);
    uint32_t findAddress(uint16_t timeout);

  private:
    uint8_t _pinSck, _pinMiso, _pinMosi, _pinSs, _pinGdo2;
    uint16_t _pinEnc{0};  // (cs_pin << 8) | miso_pin — packed into spi_transaction_t::user
    spi_device_handle_t _spi{nullptr};
    uint32_t _heaterAddr = 0;
    uint8_t _packetSeq = 0;
    uint8_t _lastBurstCompleted{0};
    uint8_t _lastBurstRequested{0};
    uint8_t _lastRxEntryState{0};
    uint8_t _lastP1First{0};    // first MARCSTATE seen in Phase 1 of last burst packet
    uint8_t _lastP1Last{0};     // last MARCSTATE seen in Phase 1 (at exit or timeout)
    uint8_t _freq2{0x10}, _freq1{0xB0}, _freq0{0x9E};
    uint8_t _ccaMode{0};
    uint8_t _txPower{7};  // PATABLE index 0-7; 7=+10dBm, 5=+7dBm, 4=0dBm, 3=-10dBm

    void initRadio();
    void txBurstLoop(uint8_t numTransmits, const char *buf);
    void txBurst(uint8_t len, char *bytes);
    void txFlush();
    void rx(uint8_t len, char *bytes);
    void rxFlush();
    void rxEnable();
    uint8_t writeReg(uint8_t addr, uint8_t val);
    void writeBurst(uint8_t addr, uint8_t len, char *bytes);
    void writeStrobe(uint8_t addr);
    bool receivePacket(char *bytes, uint16_t timeout);
    uint32_t parseAddress(char *buf);
    uint16_t crc16_2(char *buf, int len);
};

#endif
