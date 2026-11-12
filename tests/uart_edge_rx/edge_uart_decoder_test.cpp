// Tests for components/uart_edge_rx/edge_uart_decoder: decoding UART bytes
// from the times of the line's edges.

#include <cstdint>
#include <string>
#include <vector>

#include "../harness/harness.h"
#include "edge_builder.h"

namespace {

using namespace meter_test;
using namespace uart_edge_test;

constexpr double START_US = 1000;
// Rising-edge lateness the over-bright meter LED caused, measured with the
// positioning probe on 2026-11-11: before and after the optical filter.
constexpr double LATE_BEFORE_FILTER_US = 47.0;
constexpr double LATE_AFTER_FILTER_US = 17.0;
constexpr double JITTER_US = 15.0;
constexpr double IDLE_GAP_US = 300000.0;
constexpr double BREAK_US = 50000.0;
constexpr int TELEGRAMS = 3;
constexpr uint32_t BYTE_VALUES = 256;

// Every byte value, twice: all bit patterns, back to back.
Bytes every_byte_value() {
  Bytes bytes;
  for (int round = 0; round < 2; round++) {
    for (uint32_t value = 0; value < BYTE_VALUES; value++) bytes.push_back((uint8_t) value);
  }
  return bytes;
}

Bytes masked(const Bytes &bytes, uint32_t data_bits) {
  Bytes out;
  for (const uint8_t byte : bytes) out.push_back((uint8_t) (byte & ((1u << data_bits) - 1)));
  return out;
}

TEST(every_byte_value_decodes_8n1) {
  // Arrange: all byte values on a clean 8N1 line
  const Bytes sent = every_byte_value();
  Receiver receiver;

  // Act
  receiver.receive(sent, START_US);

  // Assert
  EXPECT_EQ(receiver.received, sent);
  EXPECT_EQ(receiver.decoder.framing_errors(), 0u);
}

TEST(late_rising_edges_and_jitter_still_decode) {
  // Arrange: one row per distorted line shape
  struct Row {
    const char *name;
    LineShape shape;
  };
  const std::vector<Row> rows = {
      {"rising edges late as after the optical filter", {LATE_AFTER_FILTER_US, 0}},
      {"rising edges late as before the optical filter", {LATE_BEFORE_FILTER_US, 0}},
      {"alternating jitter", {0, JITTER_US}},
      {"late rising edges plus jitter", {LATE_AFTER_FILTER_US, JITTER_US}},
  };
  const Bytes sent = every_byte_value();

  for (const Row &row : rows) {
    case_label = row.name;
    // Arrange: a fresh receiver for this row
    Receiver receiver;

    // Act
    receiver.receive(sent, START_US, {}, row.shape);

    // Assert
    EXPECT_EQ(receiver.received, sent);
    EXPECT_EQ(receiver.decoder.framing_errors(), 0u);
  }
}

TEST(every_frame_format_esphome_supports_gives_the_data_bits_only) {
  // Arrange: one row per frame format; parity is read and dropped, as
  // ESPHome's own receivers do
  struct Row {
    const char *name;
    Frame frame;
  };
  const std::vector<Row> rows = {
      {"8N1", {9600, 8, Parity::NONE, 1}}, {"8E1", {9600, 8, Parity::EVEN, 1}},
      {"8O1", {9600, 8, Parity::ODD, 1}},  {"8N2", {9600, 8, Parity::NONE, 2}},
      {"7E1", {9600, 7, Parity::EVEN, 1}}, {"7O1", {9600, 7, Parity::ODD, 1}},
      {"7N2", {9600, 7, Parity::NONE, 2}}, {"6E2", {9600, 6, Parity::EVEN, 2}},
      {"5N1", {9600, 5, Parity::NONE, 1}},
  };
  const Bytes sent = every_byte_value();

  for (const Row &row : rows) {
    case_label = row.name;
    // Arrange: a receiver configured as the bus
    Receiver receiver(row.frame);

    // Act
    receiver.receive(sent, START_US, row.frame, {LATE_AFTER_FILTER_US, 0});

    // Assert
    EXPECT_EQ(receiver.received, masked(sent, row.frame.data_bits));
    EXPECT_EQ(receiver.decoder.framing_errors(), 0u);
  }
}

TEST(baud_rates_across_the_supported_range_decode) {
  // Arrange: one row per baud rate
  const std::vector<uint32_t> rates = {300, 1200, 2400, 9600, 19200, 38400};
  const Bytes sent = every_byte_value();

  for (const uint32_t rate : rates) {
    const std::string name = std::to_string(rate) + " baud";
    case_label = name.c_str();
    // Arrange: a receiver at this rate
    const Frame frame{rate, 8, Parity::NONE, 1};
    Receiver receiver(frame);

    // Act
    receiver.receive(sent, START_US, frame);

    // Assert
    EXPECT_EQ(receiver.received, sent);
  }
}

TEST(the_microsecond_counter_wrapping_mid_stream_changes_nothing) {
  // Arrange: bytes that start 2 ms before the 32-bit counter wraps
  const Bytes sent = every_byte_value();
  const double start = (double) MICROSECOND_WRAP - 2000;
  Receiver receiver;

  // Act
  receiver.receive(sent, start);

  // Assert
  EXPECT_EQ(receiver.received, sent);
}

TEST(a_byte_ending_in_high_bits_completes_exactly_at_the_flush_threshold) {
  // Arrange: 0xff is a start bit, then a high line with no closing edge
  const Frame frame;
  Receiver receiver(frame);
  const std::vector<Edge> edges = edges_for({0xff}, START_US, frame);
  const uint32_t last_edge = edges.back().time_us;
  const uint32_t threshold_us = EdgeUartDecoder::FLUSH_BITS * 1000000u / frame.baud_rate;

  // Act
  receiver.feed(edges);
  const Bytes before_flush = receiver.received;
  receiver.decoder.flush(last_edge + threshold_us - 1);
  const Bytes just_before_threshold = receiver.received;
  receiver.decoder.flush(last_edge + threshold_us);
  const Bytes at_threshold = receiver.received;
  receiver.decoder.flush(last_edge + threshold_us + 1);

  // Assert
  EXPECT_EQ(before_flush, Bytes{});
  EXPECT_EQ(just_before_threshold, Bytes{});
  EXPECT_EQ(at_threshold, Bytes{0xff});
  EXPECT_EQ(receiver.received, Bytes{0xff});
}

TEST(transmissions_separated_by_idle_gaps_all_decode) {
  // Arrange: three transmissions with idle gaps between them
  const Bytes sent = every_byte_value();
  Receiver receiver;
  Bytes expected;

  // Act
  double start = START_US;
  for (int i = 0; i < TELEGRAMS; i++) {
    receiver.receive(sent, start, {}, {LATE_AFTER_FILTER_US, 0});
    expected.insert(expected.end(), sent.begin(), sent.end());
    start = end_of(sent, start, {}) + IDLE_GAP_US;
  }

  // Assert
  EXPECT_EQ(receiver.received, expected);
}

TEST(a_low_stop_bit_drops_its_frame_and_misframes_until_the_line_idles) {
  // Arrange: three bytes back to back, the middle one with a low stop bit,
  // then one more after an idle gap
  const Bytes sent = {0x41, 0x42, 0x43};
  const Frame frame;
  Receiver receiver(frame);
  const double later = end_of(sent, START_US, frame) + IDLE_GAP_US;

  // Act
  receiver.feed(edges_for(sent, START_US, frame, {}, 1));
  receiver.flush_after(end_of(sent, START_US, frame), frame);
  receiver.receive({0x44}, later, frame);

  // Assert: 0x42 is lost; with no idle bit before it, 0x43 resynchronises
  // on its own data bits and arrives misframed as 0xe8, as on any UART
  EXPECT_EQ(receiver.received, (Bytes{0x41, 0xe8, 0x44}));
  EXPECT_EQ(receiver.decoder.framing_errors(), 1u);
}

TEST(two_edges_within_one_interrupt_delay_drop_their_frame) {
  // Arrange: 0x55 where the rise after the start bit and the next fall
  // come so close that one interrupt sees both: it reads the line low
  // again, at the time of the second edge
  const Frame frame;
  Receiver receiver(frame);
  const std::vector<Edge> edges = edges_for({0x55}, START_US, frame);
  std::vector<Edge> merged = {edges[0], {edges[2].time_us, false}};
  merged.insert(merged.end(), edges.begin() + 3, edges.end());
  const double later = end_of({0x55}, START_US, frame) + IDLE_GAP_US;

  // Act
  receiver.feed(merged);
  receiver.flush_after(end_of({0x55}, START_US, frame), frame);
  receiver.receive({0x5a}, later, frame);

  // Assert: the frame counts as a framing error and 0x55 never arrives;
  // the decoder resynchronises on the next falling edge, which is a data
  // bit, so a misframed 0xf5 follows, as on any UART; the next byte after
  // an idle gap is intact
  EXPECT_EQ(receiver.received, (Bytes{0xf5, 0x5a}));
  EXPECT_EQ(receiver.decoder.framing_errors(), 1u);
}

TEST(a_line_break_then_bytes_recovers) {
  // Arrange: the line held low for 50 ms, then high, then bytes
  const Frame frame;
  Receiver receiver(frame);
  const double after_break = START_US + BREAK_US;

  // Act
  receiver.feed({{wrapped(START_US), false}, {wrapped(after_break), true}});
  receiver.receive({0x31, 0x32}, after_break + IDLE_GAP_US, frame);

  // Assert
  EXPECT_EQ(receiver.received, (Bytes{0x31, 0x32}));
  EXPECT_EQ(receiver.decoder.framing_errors(), 1u);
}

TEST(without_a_sink_bytes_are_discarded_safely) {
  // Arrange: a decoder with no sink set
  EdgeUartDecoder decoder;
  decoder.configure(9600, 8, false);

  // Act
  for (const Edge &edge : edges_for({0x55, 0xaa}, START_US)) decoder.edge(edge.time_us, edge.level);

  // Assert
  EXPECT_EQ(decoder.framing_errors(), 0u);
}

}  // namespace
