// Tests for components/uart_edge_rx/byte_ring: handing bytes to the ring
// buffer of ESPHome's software UART, read as its read_byte() reads it.

#include <cstddef>
#include <cstdint>
#include <vector>

#include "../../components/uart_edge_rx/byte_ring.h"
#include "../harness/harness.h"

namespace {

using esphome::uart_edge_rx::ring_push;
using namespace meter_test;
using Bytes = std::vector<uint8_t>;

constexpr size_t RING_SIZE = 8;

// The ring as ESPHome's software UART holds it; read() mirrors its
// read_byte(): empty when in == out.
struct Ring {
  uint8_t buffer[RING_SIZE]{};
  volatile size_t in = 0;
  size_t out = 0;

  bool push(uint8_t byte) { return ring_push(this->buffer, RING_SIZE, this->in, this->out, byte); }
  Bytes read_all() {
    Bytes bytes;
    while (this->in != this->out) {
      bytes.push_back(this->buffer[this->out]);
      this->out = (this->out + 1) % RING_SIZE;
    }
    return bytes;
  }
};

TEST(pushed_bytes_read_back_in_order) {
  // Arrange: an empty ring
  Ring ring;

  // Act
  const bool pushed = ring.push(0x11) && ring.push(0x22) && ring.push(0x33);
  const Bytes read = ring.read_all();

  // Assert
  EXPECT_EQ(pushed, true);
  EXPECT_EQ(read, (Bytes{0x11, 0x22, 0x33}));
}

TEST(a_full_ring_keeps_one_slot_free_and_refuses_more) {
  // Arrange: a ring filled to its capacity of size - 1
  Ring ring;
  for (uint8_t value = 1; value < RING_SIZE; value++) REQUIRE(ring.push(value));

  // Act
  const bool pushed_when_full = ring.push(0xee);
  const Bytes read = ring.read_all();

  // Assert: the refused byte is not in the ring, the others are intact
  EXPECT_EQ(pushed_when_full, false);
  EXPECT_EQ(read, (Bytes{1, 2, 3, 4, 5, 6, 7}));
}

TEST(pushes_and_reads_across_the_wrap_keep_order) {
  // Arrange: write and read positions near the end of the buffer
  Ring ring;
  for (size_t i = 0; i < RING_SIZE - 2; i++) REQUIRE(ring.push(0));
  ring.read_all();

  // Act
  Bytes pushed;
  for (uint8_t value = 0x40; value < 0x45; value++) {
    if (ring.push(value)) pushed.push_back(value);
  }
  const Bytes read = ring.read_all();

  // Assert
  EXPECT_EQ(pushed, (Bytes{0x40, 0x41, 0x42, 0x43, 0x44}));
  EXPECT_EQ(read, pushed);
}

}  // namespace
