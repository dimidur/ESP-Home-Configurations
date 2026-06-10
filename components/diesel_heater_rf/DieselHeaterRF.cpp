/*
 * DieselHeaterRF.cpp
 * Copyright (c) 2020 Jarno Kyttälä
 * https://github.com/jakkik/DieselHeaterRF
 *
 * Modified by Dimitri Duro (2026) — see DieselHeaterRF.h for change log.
 *
 * ---------------------------------
 *
 * Simple class for Arduino to control an inexpensive Chinese diesel
 * heater through 433 MHz RF by using a TI CC1101 transceiver.
 * Replicates the protocol used by the four button "red LCD remote" with
 * an OLED screen, and should probably work if your heater supports this
 * type of remote controller.
 *
 * Protocol notes (reverse-engineered):
 *   TX packet: 10 bytes — [0x09 (len), cmd, addr[3:0], seq, crc_hi, crc_lo, 0x00]
 *   RX packet: 26 bytes — 23 data bytes + 2 CC1101 APPEND_STATUS bytes (RSSI, LQI/CRC_OK)
 *   HEATER_CMD_GET_STATUS (0x23): requests a state packet; also acts as a keep-alive ping
 *   HEATER_CMD_MODE (0x24), HEATER_CMD_POWER (0x2B): toggle commands — each unique-seq
 *     packet fires one toggle, so send exactly 1 packet per desired toggle
 *   HEATER_CMD_UP (0x3C), HEATER_CMD_DOWN (0x3E): accumulator commands — heater applies
 *     the most recent direction once per burst; 14-packet bursts improve hit probability
 *   The heater responds to any valid command regardless of its WOR (Wake-On-Radio) sleep
 *     state — no explicit wake sequence is required before issuing commands
 *
 * Happy hacking!
 *
 */

#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "DieselHeaterRF.h"

static const char *const RF_TAG = "diesel_heater_rf";

// Local helpers replacing Arduino equivalents
static inline uint32_t _millis() {
  return (uint32_t)(esp_timer_get_time() / 1000LL);
}
static inline void _delay_ms(uint32_t ms) {
  vTaskDelay(pdMS_TO_TICKS(ms));
}

// ---------------------------------------------------------------------------
// pre_cb/post_cb: manual CS management and CC1101 chip-ready wait.
//
// ESP-IDF calls pre_cb BEFORE asserting hardware CS, so we cannot use
// hardware-managed CS for chip-ready detection. Instead, spics_io_num=-1
// (no hardware CS) and these callbacks drive CS via GPIO:
//   pre_cb:  assert CS LOW → wait for MISO LOW (crystal oscillator stable)
//   post_cb: deassert CS HIGH
//
// t->user encodes both pins: (cs_pin << 8) | miso_pin  (uint16_t)
// Runs in task context (polling transmit) — regular GPIO access is safe.
// ---------------------------------------------------------------------------
static void cc1101_pre_cb(spi_transaction_t *t) {
  uint16_t pins     = (uint16_t)(uintptr_t)t->user;
  gpio_num_t cs     = (gpio_num_t)((pins >> 8) & 0xFF);
  gpio_num_t miso   = (gpio_num_t)(pins & 0xFF);
  gpio_set_level(cs, 0);                        // assert CS
  int64_t t0 = esp_timer_get_time();
  while (gpio_get_level(miso)) {                // wait for chip-ready (MISO LOW)
    if (esp_timer_get_time() - t0 > 5000) break;  // 5 ms timeout
  }
}

static void cc1101_post_cb(spi_transaction_t *t) {
  uint16_t pins = (uint16_t)(uintptr_t)t->user;
  gpio_num_t cs = (gpio_num_t)((pins >> 8) & 0xFF);
  gpio_set_level(cs, 1);                        // deassert CS
}

void DieselHeaterRF::begin() {
  begin(0);
}

void DieselHeaterRF::begin(uint32_t heaterAddr) {
  _heaterAddr = heaterAddr;

  // CS: configure as GPIO output, idle HIGH. spics_io_num=-1 means the SPI
  // driver does not touch CS — pre_cb/post_cb drive it manually so that the
  // chip-ready wait happens after CS is actually asserted.
  gpio_set_direction((gpio_num_t)_pinSs, GPIO_MODE_OUTPUT);
  gpio_set_level((gpio_num_t)_pinSs, 1);

  gpio_set_direction((gpio_num_t)_pinGdo2, GPIO_MODE_INPUT);

  spi_bus_config_t buscfg = {};
  buscfg.mosi_io_num   = _pinMosi;
  buscfg.miso_io_num   = _pinMiso;
  buscfg.sclk_io_num   = _pinSck;
  buscfg.quadwp_io_num = -1;
  buscfg.quadhd_io_num = -1;

  esp_err_t ret = spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_DISABLED);
  if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
    return;
  }

  // Pullup on MISO so it idles HIGH when CC1101 is not driving it.
  // pre_cb waits for MISO HIGH→LOW (chip-ready); without pullup MISO floats LOW.
  gpio_set_pull_mode((gpio_num_t)_pinMiso, GPIO_PULLUP_ONLY);

  // Encode CS and MISO pin numbers into a single word passed via t->user.
  _pinEnc = ((uint16_t)_pinSs << 8) | (uint16_t)_pinMiso;

  spi_device_interface_config_t devcfg = {};
  devcfg.mode           = 0;
  devcfg.clock_speed_hz = 1 * 1000 * 1000;  // 1 MHz — matches ESPHome CC1101 component; 4 MHz caused compilation-sensitive TX failures
  devcfg.spics_io_num   = -1;   // CS managed manually in pre_cb / post_cb
  devcfg.queue_size     = 1;
  devcfg.pre_cb         = cc1101_pre_cb;
  devcfg.post_cb        = cc1101_post_cb;

  spi_bus_add_device(SPI2_HOST, &devcfg, &_spi);

  initRadio();
}

void DieselHeaterRF::setAddress(uint32_t heaterAddr) {
  _heaterAddr = heaterAddr;
}

void DieselHeaterRF::sendCommand(uint8_t cmd, uint32_t addr, uint8_t numTransmits, uint8_t seq) {
  char buf[10];
  buf[0] = 9;
  buf[1] = cmd;
  buf[2] = (addr >> 24) & 0xFF;
  buf[3] = (addr >> 16) & 0xFF;
  buf[4] = (addr >> 8)  & 0xFF;
  buf[5] =  addr        & 0xFF;
  buf[6] = seq;
  buf[9] = 0;
  uint16_t crc = crc16_2(buf, 7);
  buf[7] = (crc >> 8) & 0xFF;
  buf[8] =  crc       & 0xFF;
  // Log TX packet hex for protocol debugging
  ESP_LOGD(RF_TAG, "TX pkt: %02X %02X %02X%02X%02X%02X seq=%02X crc=%02X%02X",
           (uint8_t)buf[0], (uint8_t)buf[1], (uint8_t)buf[2], (uint8_t)buf[3],
           (uint8_t)buf[4], (uint8_t)buf[5], (uint8_t)buf[6], (uint8_t)buf[7], (uint8_t)buf[8]);

  txBurstLoop(numTransmits, buf);
}

void DieselHeaterRF::txBurstLoop(uint8_t numTransmits, const char *buf) {
  writeReg(0x17, (_ccaMode << 4) | 0x01); // MCSM1: CCA_MODE=_ccaMode, TXOFF_MODE=FSTXON
  txFlush();

  uint8_t completed = 0;
  for (int i = 0; i < numTransmits; i++) {
    writeBurst(0x7F, 10, (char*)buf);
    writeStrobe(0x35); // STX

    // Phase 1: spin until CC1101 leaves IDLE/FSTXON (confirms STX accepted).
    // Include 0x00 — first MARCSTATE read after writeStrobe returns corrupted
    // data (SPI_USR_MISO was disabled during the strobe), so treat 0x00 as
    // "still waiting" to avoid a race where Phase 2 sees stale FSTXON.
    uint32_t t = _millis();
    uint8_t ms;
    uint8_t p1_first = 0xFF;
    do {
      ms = writeReg(0xF5, 0xFF);
      if (p1_first == 0xFF) p1_first = ms;
      if (_millis() - t > 50) {
        ESP_LOGW(RF_TAG, "P1 timeout i=%d ms=0x%02X first=0x%02X done=%d/%d", i, ms, p1_first, completed, numTransmits);
        _lastP1First = p1_first; _lastP1Last = ms;
        _lastBurstCompleted = completed; _lastBurstRequested = numTransmits;
        writeReg(0x17, (_ccaMode << 4));
        writeStrobe(0x36);
        return;
      }
    } while (ms == 0x00 || ms == 0x01 || ms == 0x12);

    _lastP1First = p1_first; _lastP1Last = ms;

    // Phase 2: wait for packet done (FSTXON = 0x12)
    t = _millis();
    uint8_t p2_first = 0xFF;
    do {
      ms = writeReg(0xF5, 0xFF);
      if (p2_first == 0xFF) p2_first = ms;
      if (ms == 0x16) { // TXFIFO_UNDERFLOW — flush and abort
        ESP_LOGW(RF_TAG, "TXFIFO underflow i=%d done=%d/%d", i, completed, numTransmits);
        _lastBurstCompleted = completed; _lastBurstRequested = numTransmits;
        txFlush();
        writeReg(0x17, (_ccaMode << 4));
        return;
      }
      _delay_ms(1);
      if (_millis() - t > 100) {
        ESP_LOGW(RF_TAG, "P2 timeout i=%d ms=0x%02X first=0x%02X done=%d/%d", i, ms, p2_first, completed, numTransmits);
        _lastBurstCompleted = completed; _lastBurstRequested = numTransmits;
        writeReg(0x17, (_ccaMode << 4));
        writeStrobe(0x36);
        return;
      }
    } while (ms != 0x12);
    completed++;
  }

  ESP_LOGD(RF_TAG, "Burst OK: %d/%d packets", completed, numTransmits);
  _lastBurstCompleted = completed;
  _lastBurstRequested = numTransmits;
  // Leave chip in FSTXON with synthesizer locked.  DO NOT write config registers
  // here — CC1101 only allows config writes in IDLE state; writing MCSM1 in FSTXON
  // corrupts the state machine (observed: MARCSTATE=0x00 after SRX, degraded TX).
  // Caller uses startRxFromFstxon() for cal-free RX entry.
  // Next sendCommand() sets MCSM1 after reinitRadio() puts chip back in IDLE.
}

void DieselHeaterRF::endTxBurst() {
  writeReg(0x17, (_ccaMode << 4)); // MCSM1: TXOFF_MODE=IDLE
  writeStrobe(0x36);               // SIDLE
}

void DieselHeaterRF::startRx() {
  rxFlush();
  rxEnable();
  _lastRxEntryState = writeReg(0xF5, 0xFF); // MARCSTATE after SRX
}

void DieselHeaterRF::startRxFromFstxon() {
  // From FSTXON the synthesizer is already calibrated and locked.
  // SRX goes directly to RX without STARTCAL — avoids the ~720μs
  // calibration window where VCC droop can reset the CC1101.
  writeStrobe(0x34); // SRX
  _lastRxEntryState = writeReg(0xF5, 0xFF); // MARCSTATE after SRX
}

bool DieselHeaterRF::isRxAvailable() {
  return gpio_get_level((gpio_num_t)_pinGdo2);
}

bool DieselHeaterRF::readPacket(heater_state_t *state) {
  uint8_t rxLen = writeReg(0xFB, 0xFF); // RXBYTES
  if (rxLen != 26) { rxFlush(); return false; }
  char buf[26];
  rx(26, buf);
  rxFlush();
  if (!((uint8_t)buf[25] & 0x80)) return false; // CRC_OK bit
  uint32_t address = parseAddress(buf);
  if (address != _heaterAddr) return false;
  state->state      = buf[6];
  state->power      = buf[7];
  state->errorCode  = buf[11];  // was buf[8] — neither byte confirmed by protocol docs
  state->voltage    = buf[9] / 10.0f;
  state->ambientTemp = buf[10];
  state->caseTemp   = buf[12];
  state->setpoint   = buf[13];
  state->autoMode   = buf[14] == 0x32;
  state->pumpFreq   = buf[15] / 10.0f;
  state->rssi       = ((uint8_t)buf[24] >= 128 ? (int)(buf[24]) - 256 : (int)(uint8_t)buf[24]) / 2 - 74;
  return true;
}

uint8_t DieselHeaterRF::getPartNum()   { return writeReg(0xF0, 0xFF); }
uint8_t DieselHeaterRF::getVersion()   { return writeReg(0xF1, 0xFF); }
uint8_t DieselHeaterRF::getMarcstate() { return writeReg(0xF5, 0xFF); }
void DieselHeaterRF::getFreqRegisters(uint8_t *freq2, uint8_t *freq1, uint8_t *freq0) {
  *freq2 = writeReg(0x8D, 0xFF);  // read FREQ2
  *freq1 = writeReg(0x8E, 0xFF);  // read FREQ1
  *freq0 = writeReg(0x8F, 0xFF);  // read FREQ0
}

void DieselHeaterRF::setFrequency(uint8_t freq2, uint8_t freq1, uint8_t freq0) {
  _freq2 = freq2;
  _freq1 = freq1;
  _freq0 = freq0;
}

bool DieselHeaterRF::receiveRaw(char *bytes, uint8_t *len, uint16_t timeout) {
  uint32_t t = _millis();
  *len = 0;

  writeReg(0x00, 0x01); // IOCFG2: assert when RX FIFO >= threshold or end of packet
  rxFlush();
  rxEnable();

  while (!gpio_get_level((gpio_num_t)_pinGdo2)) {
    if (_millis() - t > timeout) {
      writeReg(0x00, 0x07); // restore IOCFG2
      return false;
    }
    taskYIELD();
  }

  uint8_t rxLen = writeReg(0xFB, 0xFF);  // RXBYTES
  if (rxLen == 0 || rxLen > 64) {
    writeReg(0x00, 0x07); // restore IOCFG2
    rxFlush();
    return false;
  }

  rx(rxLen, bytes);
  rxFlush();
  writeReg(0x00, 0x07); // restore IOCFG2
  *len = rxLen;
  return true;
}

uint32_t DieselHeaterRF::findAddress(uint16_t timeout) {
  char buf[26];
  if (receivePacket(buf, timeout)) {
    return parseAddress(buf);
  }
  return 0;
}

uint32_t DieselHeaterRF::parseAddress(char *buf) {
  uint32_t address = 0;
  address |= (buf[2] << 24);
  address |= (buf[3] << 16);
  address |= (buf[4] << 8);
  address |= buf[5];
  return address;
}

bool DieselHeaterRF::receivePacket(char *bytes, uint16_t timeout) {
  uint32_t t = _millis();
  uint8_t rxLen;

  rxFlush();
  rxEnable();

  while (1) {
    if (_millis() - t > timeout) { rxFlush(); return false; }

    while (!gpio_get_level((gpio_num_t)_pinGdo2)) {
      if (_millis() - t > timeout) { rxFlush(); return false; }
      taskYIELD();
    }

    rxLen = writeReg(0xFB, 0xFF);
    if (rxLen == 26) break;  // length byte 0x17=23 + 2 APPEND_STATUS = 26 total

    rxFlush();
    rxEnable();
  }

  rx(rxLen, bytes);
  rxFlush();

  // Hardware CRC validated by CC1101 (APPEND_STATUS: buf[25] bit7 = CRC_OK)
  return (uint8_t)bytes[25] & 0x80;
}

void DieselHeaterRF::initRadio() {
  writeStrobe(0x30); // SRES

  _delay_ms(100);

  writeReg(0x00, 0x07); // IOCFG2: assert when packet received with CRC OK
  writeReg(0x02, 0x06); // IOCFG0
  writeReg(0x03, 0x47); // FIFOTHR
  writeReg(0x07, 0x04); // PKTCTRL1
  writeReg(0x08, 0x05); // PKTCTRL0
  writeReg(0x0A, 0x00); // CHANNR
  writeReg(0x0B, 0x06); // FSCTRL1
  writeReg(0x0C, 0x00); // FSCTRL0
  writeReg(0x0D, _freq2); // FREQ2
  writeReg(0x0E, _freq1); // FREQ1
  writeReg(0x0F, _freq0); // FREQ0
  writeReg(0x10, 0xF8); // MDMCFG4
  writeReg(0x11, 0x93); // MDMCFG3
  writeReg(0x12, 0x13); // MDMCFG2
  writeReg(0x13, 0x22); // MDMCFG1: NUM_PREAMBLE=4 bytes (matches original remote)
  writeReg(0x14, 0xF8); // MDMCFG0
  writeReg(0x15, 0x26); // DEVIATN
  writeReg(0x16, 0x07); // MCSM2: RX_TIME=7 (no RX timeout — stay in RX until packet received)
  writeReg(0x17, (_ccaMode << 4)); // MCSM1: CCA_MODE=_ccaMode, TXOFF_MODE=IDLE
  writeReg(0x18, 0x18); // MCSM0
  writeReg(0x19, 0x17); // FOCCFG
  writeReg(0x1A, 0x6C); // BSCFG
  writeReg(0x1B, 0x03); // AGCTRL2
  writeReg(0x1C, 0x40); // AGCTRL1
  writeReg(0x1D, 0x91); // AGCTRL0
  writeReg(0x20, 0xFB); // WORCTRL
  writeReg(0x21, 0x56); // FREND1
  writeReg(0x22, 0x10 | _txPower); // FREND0: PA_POWER selects PATABLE index
  writeReg(0x23, 0xE9); // FSCAL3
  writeReg(0x24, 0x2A); // FSCAL2
  writeReg(0x25, 0x00); // FSCAL1
  writeReg(0x26, 0x1F); // FSCAL0
  writeReg(0x2C, 0x81); // TEST2
  writeReg(0x2D, 0x35); // TEST1
  writeReg(0x2E, 0x09); // TEST0
  writeReg(0x09, 0x00); // ADDR
  writeReg(0x04, 0x7E); // SYNC1
  writeReg(0x05, 0x3C); // SYNC0

  char patable[8] = {0x00, 0x12, 0x0E, 0x34, 0x60, (char)0xC5, (char)0xC1, (char)0xC0};
  writeBurst(0x7E, 8, patable); // PATABLE

  writeStrobe(0x31); // SFSTXON
  writeStrobe(0x36); // SIDLE
  writeStrobe(0x3B); // SFTX
  writeStrobe(0x36); // SIDLE
  writeStrobe(0x3A); // SFRX

  _delay_ms(136);
}

void DieselHeaterRF::txBurst(uint8_t len, char *bytes) {
  txFlush();
  writeBurst(0x7F, len, bytes);
  writeStrobe(0x35); // STX
}

void DieselHeaterRF::txFlush() {
  writeStrobe(0x36); // SIDLE
  writeStrobe(0x3B); // SFTX
}

// Burst-read len bytes from the RX FIFO in a single SPI transaction.
// Header byte 0xFF = R=1, Burst=1, addr=0x3F (RXFIFO).
// The first received byte is the CC1101 status byte; data follows from index 1.
void DieselHeaterRF::rx(uint8_t len, char *bytes) {
  uint8_t tx[27] = {};
  uint8_t rxbuf[27] = {};
  tx[0] = 0xFF;  // RXFIFO burst read
  // remaining tx bytes are 0x00 — don't-care, just clock out the FIFO

  spi_transaction_t t = {};
  t.length    = (size_t)(len + 1) * 8;
  t.tx_buffer = tx;
  t.rx_buffer = rxbuf;
  t.user      = (void *)(uintptr_t)_pinEnc;
  spi_device_polling_transmit(_spi, &t);

  memcpy(bytes, rxbuf + 1, len);  // skip status byte at index 0
}

void DieselHeaterRF::rxFlush() {
  writeStrobe(0x36); // SIDLE
  // De-assert GDO2 by reading one FIFO byte — but only if FIFO has data.
  // Reading from an empty FIFO triggers RXFIFO underflow (CC1101 auto-recovers
  // to IDLE, but the brief error state can leave the chip fragile).
  if (gpio_get_level((gpio_num_t)_pinGdo2)) {
    writeReg(0xBF, 0xFF); // read one byte → GDO2 de-asserts
  }
  writeStrobe(0x3A); // SFRX — flush RX FIFO (valid in IDLE or RXFIFO_OVERFLOW)
}

void DieselHeaterRF::rxEnable() {
  writeStrobe(0x34); // SRX
}

// 2-byte transaction: send addr, send val, return received byte from second transfer.
uint8_t DieselHeaterRF::writeReg(uint8_t addr, uint8_t val) {
  spi_transaction_t t = {};
  t.length = 16;
  t.flags  = SPI_TRANS_USE_TXDATA | SPI_TRANS_USE_RXDATA;
  t.tx_data[0] = addr;
  t.tx_data[1] = val;
  t.user = (void *)(uintptr_t)_pinEnc;
  spi_device_polling_transmit(_spi, &t);
  return t.rx_data[1];
}

// Burst write: send addr byte then len data bytes in one CS assertion.
// Non-NULL rx_buffer keeps SPI_USR_MISO enabled so the next SPI read
// (e.g. getMarcstate) returns valid data on the first call.
void DieselHeaterRF::writeBurst(uint8_t addr, uint8_t len, char *bytes) {
  uint8_t tx[32];  // max burst: 1 (addr) + 10 (TX packet) or 8 (PATABLE) = 11 max
  uint8_t rx[32];  // discarded — only present to keep MISO enabled
  tx[0] = addr;
  memcpy(tx + 1, bytes, len);

  spi_transaction_t t = {};
  t.length    = (size_t)(len + 1) * 8;
  t.tx_buffer = tx;
  t.rx_buffer = rx;
  t.user      = (void *)(uintptr_t)_pinEnc;
  spi_device_polling_transmit(_spi, &t);
}

// Single-byte strobe command — no data byte needed.
// SPI_TRANS_USE_RXDATA keeps SPI_USR_MISO enabled so the next SPI read
// returns valid data on the first call (without needing a warmup read).
void DieselHeaterRF::writeStrobe(uint8_t addr) {
  spi_transaction_t t = {};
  t.length = 8;
  t.flags  = SPI_TRANS_USE_TXDATA | SPI_TRANS_USE_RXDATA;
  t.tx_data[0] = addr;
  t.user = (void *)(uintptr_t)_pinEnc;
  spi_device_polling_transmit(_spi, &t);
}

/*
 * CRC-16/MODBUS
 */
uint16_t DieselHeaterRF::crc16_2(char *buf, int len) {
  uint16_t crc = 0xFFFF;
  for (int pos = 0; pos < len; pos++) {
    crc ^= (uint8_t)buf[pos];
    for (int i = 8; i != 0; i--) {
      if ((crc & 0x0001) != 0) {
        crc >>= 1;
        crc ^= 0xA001;
      } else {
        crc >>= 1;
      }
    }
  }
  return crc;
}
