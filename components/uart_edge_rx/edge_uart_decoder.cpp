#include "edge_uart_decoder.h"

namespace esphome {
namespace uart_edge_rx {

namespace {

constexpr int32_t IDLE = -1;
constexpr int32_t WAIT_HIGH = -2;
constexpr uint32_t Q16_SHIFT = 16;
constexpr uint32_t Q16_ONE = 1u << Q16_SHIFT;
constexpr uint32_t Q16_HALF = Q16_ONE / 2;
constexpr uint32_t MICROSECONDS_PER_SECOND = 1000000;
// Longer intervals are clamped: they hold a frame's worth of one level
// many times over at any supported baud rate, and the clamp keeps the
// multiply within 32 bits.
constexpr uint32_t MAX_INTERVAL_US = 100000;
// More bits of one level than this change nothing: a frame is at most
// 12 bits.
constexpr uint32_t MAX_BITS_PER_INTERVAL = 20;

}  // namespace

void EdgeUartDecoder::configure(uint32_t baud_rate, uint32_t data_bits, bool has_parity) {
  this->bits_per_us_q16_ =
      (uint32_t) (((uint64_t) baud_rate * Q16_ONE + MICROSECONDS_PER_SECOND / 2) / MICROSECONDS_PER_SECOND);
  this->flush_us_ = baud_rate == 0 ? 0 : FLUSH_BITS * MICROSECONDS_PER_SECOND / baud_rate;
  this->data_bits_ = data_bits;
  this->stop_index_ = data_bits + (has_parity ? 1 : 0);
}

uint32_t IRAM_ATTR EdgeUartDecoder::bit_count_(uint32_t interval_us) const {
  if (interval_us > MAX_INTERVAL_US) interval_us = MAX_INTERVAL_US;
  const uint32_t bits = (interval_us * this->bits_per_us_q16_ + Q16_HALF) >> Q16_SHIFT;
  return bits > MAX_BITS_PER_INTERVAL ? MAX_BITS_PER_INTERVAL : bits;
}

void IRAM_ATTR EdgeUartDecoder::edge(uint32_t time_us, bool level) {
  if (this->started_ && level == this->level_) {
    // The edge in between was missed: the bits since the last edge are
    // unknown, so the frame they belong to is lost.
    this->framing_error_(level);
  } else if (this->started_) {
    this->bits_(this->level_, this->bit_count_(time_us - this->last_edge_us_));
  }
  this->started_ = true;
  this->level_ = level;
  this->last_edge_us_ = time_us;
}

void EdgeUartDecoder::flush(uint32_t time_us) {
  if (!this->started_ || !this->level_) return;
  const uint32_t interval_us = time_us - this->last_edge_us_;
  if (interval_us < this->flush_us_) return;
  this->bits_(true, this->bit_count_(interval_us));
  this->last_edge_us_ = time_us;
}

void IRAM_ATTR EdgeUartDecoder::framing_error_(bool level) {
  // A frame was under way: bits collected, or the line low (a start bit
  // whose bits are only counted at the next edge).
  const bool in_frame = this->bit_index_ >= 0 || (this->bit_index_ == IDLE && !this->level_);
  if (in_frame) this->framing_errors_++;
  this->bit_index_ = level ? IDLE : WAIT_HIGH;
}

void IRAM_ATTR EdgeUartDecoder::bits_(bool level, uint32_t count) {
  for (uint32_t i = 0; i < count; i++) {
    if (this->bit_index_ == WAIT_HIGH) {
      if (level) this->bit_index_ = IDLE;
    } else if (this->bit_index_ == IDLE) {
      if (!level) {
        this->bit_index_ = 0;
        this->shift_ = 0;
      }
    } else if (this->bit_index_ < (int32_t) this->stop_index_) {
      // Data bits, least significant first; a parity bit past them is
      // skipped.
      if (level && this->bit_index_ < (int32_t) this->data_bits_) {
        this->shift_ |= (uint8_t) (1u << this->bit_index_);
      }
      this->bit_index_++;
    } else if (level) {
      if (this->sink_ != nullptr) this->sink_(this->sink_context_, this->shift_);
      this->bit_index_ = IDLE;
    } else {
      this->framing_errors_++;
      this->bit_index_ = WAIT_HIGH;
    }
  }
}

}  // namespace uart_edge_rx
}  // namespace esphome
