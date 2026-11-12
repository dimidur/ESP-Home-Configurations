#pragma once

// A UART receiver that decodes bytes from the times of the line's edges.
// An interrupt calls edge() with the time and the new level; the interval
// since the previous edge, rounded to whole bit times, gives the bits the
// line held. Each call is a few operations, so interrupts stay masked for
// microseconds, unlike a receiver that samples every bit inside the
// interrupt. Decoded bytes go to a sink function. Free of ESPHome so it
// can be host-tested.

#include <cstddef>
#include <cstdint>

#if defined(ARDUINO)
#include <Arduino.h>
#elif defined(ESP_PLATFORM)
#include <esp_attr.h>
#endif
#ifndef IRAM_ATTR
#define IRAM_ATTR
#endif

namespace esphome {
namespace uart_edge_rx {

class EdgeUartDecoder {
 public:
  // Receives each decoded byte; called from edge() and flush(), so from an
  // interrupt: it must be in IRAM and must not block.
  using Sink = void (*)(void *context, uint8_t byte);

  static constexpr uint32_t MIN_DATA_BITS = 5;
  static constexpr uint32_t MAX_DATA_BITS = 8;
  // Idle bit times after which flush() completes a frame.
  static constexpr uint32_t FLUSH_BITS = 12;

  // The frame as the UART bus defines it. Parity is read and ignored, as
  // ESPHome's own receivers do; only the first stop bit is checked.
  void configure(uint32_t baud_rate, uint32_t data_bits, bool has_parity);
  void set_sink(Sink sink, void *context) {
    this->sink_ = sink;
    this->sink_context_ = context;
  }

  // The line changed to level at time_us (microseconds, wrapping). Runs in
  // an interrupt: it and everything it calls are defined IRAM_ATTR.
  void edge(uint32_t time_us, bool level);
  // Completes a frame whose last bits are high, once the line has been
  // idle for FLUSH_BITS bit times. Call from the main loop with
  // interrupts masked, as it shares state with edge().
  void flush(uint32_t time_us);

  // Frames dropped: a low stop bit, or an edge to the level the line
  // already had (a missed edge pair) inside a frame.
  uint32_t framing_errors() const { return this->framing_errors_; }

 protected:
  uint32_t bit_count_(uint32_t interval_us) const;
  void bits_(bool level, uint32_t count);
  void framing_error_(bool level);

  Sink sink_{nullptr};
  void *sink_context_{nullptr};
  // Bits per microsecond in 16.16 fixed point.
  uint32_t bits_per_us_q16_{0};
  uint32_t flush_us_{0};
  uint32_t data_bits_{MAX_DATA_BITS};
  // Frame positions after the start bit up to the first stop bit.
  uint32_t stop_index_{MAX_DATA_BITS};
  bool started_{false};
  bool level_{true};
  uint32_t last_edge_us_{0};
  // Frame state: IDLE waits for a start bit, WAIT_HIGH for the line to go
  // high after a framing error, 0..stop_index_-1 the data and parity bits,
  // stop_index_ the stop bit.
  int32_t bit_index_{-1};
  uint8_t shift_{0};
  uint32_t framing_errors_{0};
};

}  // namespace uart_edge_rx
}  // namespace esphome
