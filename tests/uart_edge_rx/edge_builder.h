#pragma once

// The edges a UART line carries for given bytes and frame format, and a
// decoder harness that collects what it emits. Shared by the uart_edge_rx
// tests and the electricity meter reader's end-to-end test.

#include <cstddef>
#include <cstdint>
#include <vector>

#include "../../components/uart_edge_rx/edge_uart_decoder.h"

namespace uart_edge_test {

using esphome::uart_edge_rx::EdgeUartDecoder;
using Bytes = std::vector<uint8_t>;

struct Edge {
  uint32_t time_us;
  bool level;
};

enum class Parity { NONE, EVEN, ODD };

struct Frame {
  uint32_t baud_rate = 9600;
  uint32_t data_bits = 8;
  Parity parity = Parity::NONE;
  uint32_t stop_bits = 1;

  uint32_t bits() const { return 1 + this->data_bits + (this->parity == Parity::NONE ? 0 : 1) + this->stop_bits; }
  double bit_us() const { return 1e6 / this->baud_rate; }
};

// Distortions of a real IR head: rising edges late, every edge jittered.
struct LineShape {
  double rise_late_us = 0;
  double jitter_us = 0;  // added and subtracted alternately
};

constexpr size_t NO_BAD_STOP = SIZE_MAX;
constexpr uint64_t MICROSECOND_WRAP = 1ull << 32;

// The microsecond counter as the device reads it: modulo 2^32.
inline uint32_t wrapped(double time_us) { return (uint32_t) ((uint64_t) time_us % MICROSECOND_WRAP); }

inline double end_of(const Bytes &bytes, double start_us, const Frame &frame) {
  return start_us + (double) (bytes.size() * frame.bits()) * frame.bit_us();
}

// The edges of bytes sent back to back from start_us, line idle high before
// and after. The byte at bad_stop_index has a low first stop bit.
inline std::vector<Edge> edges_for(const Bytes &bytes, double start_us, const Frame &frame = {},
                                   const LineShape &shape = {}, size_t bad_stop_index = NO_BAD_STOP) {
  std::vector<Edge> edges;
  bool level = true;
  size_t edge_count = 0;
  const uint32_t parity_index = 1 + frame.data_bits;
  const uint32_t stop_index = parity_index + (frame.parity == Parity::NONE ? 0 : 1);
  for (size_t index = 0; index < bytes.size(); index++) {
    const uint32_t data = bytes[index] & ((1u << frame.data_bits) - 1);
    const double frame_start = start_us + (double) (index * frame.bits()) * frame.bit_us();
    for (uint32_t bit = 0; bit < frame.bits(); bit++) {
      bool bit_level;
      if (bit == 0) {
        bit_level = false;
      } else if (bit < parity_index) {
        bit_level = (data >> (bit - 1)) & 1;
      } else if (bit < stop_index) {
        const bool odd_ones = __builtin_popcount(data) & 1;
        bit_level = frame.parity == Parity::EVEN ? odd_ones : !odd_ones;
      } else {
        bit_level = !(bit == stop_index && index == bad_stop_index);
      }
      if (bit_level == level) continue;
      double time = frame_start + bit * frame.bit_us();
      if (bit_level) time += shape.rise_late_us;
      time += (edge_count++ % 2 == 0) ? shape.jitter_us : -shape.jitter_us;
      edges.push_back({wrapped(time), bit_level});
      level = bit_level;
    }
  }
  if (!level) edges.push_back({wrapped(end_of(bytes, start_us, frame) + shape.rise_late_us), true});
  return edges;
}

// A decoder whose sink collects the bytes it emits.
struct Receiver {
  EdgeUartDecoder decoder;
  Bytes received;

  explicit Receiver(const Frame &frame = {}) {
    this->decoder.configure(frame.baud_rate, frame.data_bits, frame.parity != Parity::NONE);
    this->decoder.set_sink(&Receiver::collect, this);
  }
  Receiver(const Receiver &) = delete;
  Receiver &operator=(const Receiver &) = delete;

  static void collect(void *context, uint8_t byte) { static_cast<Receiver *>(context)->received.push_back(byte); }

  void feed(const std::vector<Edge> &edges) {
    for (const Edge &edge : edges) this->decoder.edge(edge.time_us, edge.level);
  }
  // Flushes once the line has idled long enough after time_us.
  void flush_after(double time_us, const Frame &frame) {
    this->decoder.flush(wrapped(time_us + (EdgeUartDecoder::FLUSH_BITS + 1) * frame.bit_us()));
  }
  // Feeds the edges of bytes and flushes after the last frame.
  void receive(const Bytes &bytes, double start_us, const Frame &frame = {}, const LineShape &shape = {}) {
    this->feed(edges_for(bytes, start_us, frame, shape));
    this->flush_after(end_of(bytes, start_us, frame) + shape.rise_late_us, frame);
  }
};

}  // namespace uart_edge_test
