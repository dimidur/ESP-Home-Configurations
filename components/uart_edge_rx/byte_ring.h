#pragma once

#include <cstddef>
#include <cstdint>

namespace esphome {
namespace uart_edge_rx {

// Appends byte to a ring buffer that another context reads: size bytes,
// write position in, read position out. As in ESPHome's software UART, one
// slot stays free, so in == out means empty. Returns false, leaving the
// ring unchanged, when it is full. Always inlined, so an interrupt that
// calls it runs no code outside its own section.
template<typename Position>
__attribute__((always_inline)) inline bool ring_push(uint8_t *buffer, size_t size, Position &in, size_t out,
                                                     uint8_t byte) {
  const size_t position = in;
  const size_t next = position + 1 == size ? 0 : position + 1;
  if (next == out) return false;
  buffer[position] = byte;
  in = next;
  return true;
}

}  // namespace uart_edge_rx
}  // namespace esphome
