#pragma once

// Splits the byte stream an IR head receives into OBIS text lines: text
// lines of a push-mode IEC 62056-21 meter, and SML telegrams decoded and
// rendered in the same line format. Free of ESPHome so it can be
// host-tested.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "sml_decoder.h"

namespace esphome {
namespace electricity_meter_reader {

// What one received byte completed.
enum class MeterStreamEvent {
  NONE,               // nothing yet, or a text line (see lines)
  TELEGRAM,           // an SML telegram was decoded into lines
  TELEGRAM_RESTART,   // a new start sequence cut a telegram short
  TELEGRAM_CRC,       // a complete telegram failed its CRC
  TELEGRAM_DECODE,    // a complete telegram could not be decoded
  TELEGRAM_OVERFLOW,  // too long to be a telegram
};

struct MeterStreamResult {
  MeterStreamEvent event = MeterStreamEvent::NONE;
  // For telegram events: bytes the telegram had when it ended.
  size_t telegram_length = 0;
  // For dropped telegrams: lines it would have carried, as many as the
  // last decoded telegram that rendered any line had (1 before the first).
  size_t lines_lost = 0;
};

// The reason a telegram was dropped, or nullptr when the event drops none.
const char *dropped_telegram_reason(MeterStreamEvent event);

class MeterStreamReader {
 public:
  // Once a text line holds more than this many characters, the next byte
  // ends it and is itself dropped.
  static constexpr size_t MAX_LINE_LEN = 120;
  // A telegram longer than this is dropped as garbled.
  static constexpr size_t MAX_TELEGRAM_LEN = 2048;

  // Feeds one received byte; completed lines are appended to lines.
  // Text bytes are read with the top bit cleared, so a 7E1 meter is read
  // correctly on an 8N1 UART.
  MeterStreamResult feed(uint8_t byte, std::vector<std::string> &lines);

 protected:
  void feed_text_(char received, std::vector<std::string> &lines);
  void start_telegram_();
  MeterStreamResult feed_telegram_(uint8_t byte, std::vector<std::string> &lines);
  MeterStreamResult finish_telegram_(std::vector<std::string> &lines);
  MeterStreamResult dropped_(MeterStreamEvent event, size_t telegram_length);

  std::string line_;
  std::vector<uint8_t> telegram_;
  std::vector<SmlEntry> entries_;
  bool in_telegram_{false};
  // After a dropped telegram, bytes up to the next start are skipped.
  bool discarding_{false};
  // The last bytes outside a telegram, to spot a start sequence.
  uint8_t recent_[SML_START_LEN]{};
  // Lines the last decoded telegram rendering any line carried; a dropped
  // one lost as many. A telegram that renders none leaves it unchanged.
  size_t lines_per_telegram_{1};
};

}  // namespace electricity_meter_reader
}  // namespace esphome
