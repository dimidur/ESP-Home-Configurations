// End to end: the meter's line edges, decoded by uart_edge_rx's decoder,
// read by the electricity meter reader.

#include <string>

#include "../../components/electricity_meter_reader/meter_stream.h"
#include "../harness/harness.h"
#include "../uart_edge_rx/edge_builder.h"
#include "sml_builder.h"

namespace {

using esphome::electricity_meter_reader::MeterStreamReader;
using namespace meter_test;
using uart_edge_test::Frame;
using uart_edge_test::LineShape;
using uart_edge_test::Parity;
using uart_edge_test::Receiver;

constexpr double START_US = 1000;
// Rising-edge lateness after the optical filter and jitter, as measured
// with the positioning probe on 2026-11-11.
constexpr double LATE_AFTER_FILTER_US = 17.0;
constexpr double JITTER_US = 15.0;

Lines read_lines(const Bytes &bytes) {
  MeterStreamReader reader;
  Lines lines;
  for (const uint8_t byte : bytes) reader.feed(byte, lines);
  return lines;
}

TEST(the_captured_sml_telegram_gives_its_22_lines_at_8n1) {
  // Arrange: the telegram's edges on a distorted 8N1 line
  const Frame frame{9600, 8, Parity::NONE, 1};
  Receiver receiver(frame);

  // Act
  receiver.receive(fixture(), START_US, frame, {LATE_AFTER_FILTER_US, JITTER_US});
  const Lines lines = read_lines(receiver.received);

  // Assert
  EXPECT_EQ(lines, fixture_lines());
}

TEST(a_7e1_text_meter_gives_its_lines_on_a_7e1_bus) {
  // Arrange: an OBIS text line from a 7E1 meter, on a bus configured 7E1
  const std::string text = "1-0:1.8.0*255(031719.5078*kWh)\r\n";
  const Frame frame{9600, 7, Parity::EVEN, 1};
  Receiver receiver(frame);

  // Act
  receiver.receive(Bytes(text.begin(), text.end()), START_US, frame, {LATE_AFTER_FILTER_US, 0});
  const Lines lines = read_lines(receiver.received);

  // Assert
  EXPECT_EQ(lines, (Lines{"1-0:1.8.0*255(031719.5078*kWh)"}));
}

}  // namespace
