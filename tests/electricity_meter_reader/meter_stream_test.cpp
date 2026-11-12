// Tests for components/electricity_meter_reader/meter_stream: splitting a
// received byte stream into text lines and decoded SML telegrams.

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "../../components/electricity_meter_reader/meter_stream.h"
#include "../harness/harness.h"
#include "sml_builder.h"

namespace {

using esphome::electricity_meter_reader::dropped_telegram_reason;
using esphome::electricity_meter_reader::MeterStreamReader;
using esphome::electricity_meter_reader::MeterStreamResult;
using namespace meter_test;

using Events = std::vector<MeterStreamEvent>;

constexpr uint8_t FILLER = 0x55;
constexpr uint8_t FLIP = 0x01;
constexpr size_t MAX_TELEGRAM = MeterStreamReader::MAX_TELEGRAM_LEN;
constexpr size_t ALIGNED_CUT = 300;
constexpr size_t UNALIGNED_CUT = ALIGNED_CUT + 1;
static_assert(ALIGNED_CUT % BLOCK_LEN == 0, "aligned cut");
const std::string EARLIER_LINE = "earlier line";

struct StreamRun {
  Lines lines;
  std::vector<MeterStreamResult> results;  // every result but NONE, in order

  Events events() const {
    Events out;
    for (const MeterStreamResult &result : this->results) out.push_back(result.event);
    return out;
  }

  // The first result with this event; a NONE result when there is none.
  MeterStreamResult first(MeterStreamEvent event) const {
    for (const MeterStreamResult &result : this->results) {
      if (result.event == event) return result;
    }
    return {};
  }
};

StreamRun feed_all(MeterStreamReader &reader, const Bytes &bytes, Lines lines = {}) {
  StreamRun run{std::move(lines), {}};
  for (const uint8_t byte : bytes) {
    const MeterStreamResult result = reader.feed(byte, run.lines);
    if (result.event != MeterStreamEvent::NONE) run.results.push_back(result);
  }
  return run;
}

StreamRun feed_all(const Bytes &bytes) {
  MeterStreamReader reader;
  return feed_all(reader, bytes);
}

size_t count(const Events &events, MeterStreamEvent wanted) {
  size_t matches = 0;
  for (const MeterStreamEvent event : events) matches += event == wanted;
  return matches;
}

Lines repeated(const Lines &lines, size_t times) {
  Lines out;
  for (size_t i = 0; i < times; i++) out.insert(out.end(), lines.begin(), lines.end());
  return out;
}

Lines concat(std::initializer_list<Lines> parts) {
  Lines out;
  for (const Lines &part : parts) out.insert(out.end(), part.begin(), part.end());
  return out;
}

// Text as a 7E1 meter sends it, read by an 8N1 UART: the even-parity bit
// arrives as the top bit of each byte.
constexpr uint8_t SEVEN_BIT_MASK = 0x7f;
constexpr uint8_t PARITY_BIT = 0x80;

Bytes with_even_parity(const std::string &text) {
  Bytes out;
  for (const char character : text) {
    const uint8_t byte = (uint8_t) character & SEVEN_BIT_MASK;
    unsigned ones = 0;
    for (unsigned bit = 0; bit < BITS_PER_BYTE; bit++) ones += (byte >> bit) & 1;
    out.push_back(ones % 2 == 1 ? (uint8_t) (byte | PARITY_BIT) : byte);
  }
  return out;
}

// Telegrams that the reader drops for each reason.
Bytes crc_broken() {
  Bytes telegram = fixture();
  telegram[START_LEN] ^= FLIP;
  return telegram;
}
Bytes undecodable() { return frame(get_list_message({sentinel_entry(), list_of({absent()})})); }
Bytes overflowing() { return join({start_sequence(), repeat(FILLER, MAX_TELEGRAM - START_LEN)}); }
Bytes cut_short() { return slice(fixture(), 0, UNALIGNED_CUT); }

// ===========================================================================
// Telegrams

TEST(reader_decodes_the_real_telegram_into_the_22_expected_lines) {
  // Arrange: the captured telegram
  const Bytes telegram = fixture();

  // Act
  const StreamRun run = feed_all(telegram);

  // Assert
  EXPECT_EQ(run.events(), Events{MeterStreamEvent::TELEGRAM});
  EXPECT_EQ(run.first(MeterStreamEvent::TELEGRAM).telegram_length, telegram.size());
  EXPECT_EQ(run.lines, fixture_lines());
}

TEST(telegram_fed_byte_by_byte_yields_lines_only_on_its_last_byte) {
  // Arrange: a fresh reader, the captured telegram, and tallies for what arrives before its last byte
  MeterStreamReader reader;
  const Bytes telegram = fixture();
  size_t early_events = 0;
  size_t early_lines = 0;
  MeterStreamEvent last_event = MeterStreamEvent::NONE;
  Lines last_lines;

  // Act
  for (size_t i = 0; i < telegram.size(); i++) {
    Lines lines;
    const MeterStreamResult result = reader.feed(telegram[i], lines);
    if (i + 1 < telegram.size()) {
      early_events += result.event != MeterStreamEvent::NONE;
      early_lines += lines.size();
    } else {
      last_event = result.event;
      last_lines = lines;
    }
  }

  // Assert
  EXPECT_EQ(early_events, size_t{0});
  EXPECT_EQ(early_lines, size_t{0});
  EXPECT_EQ(last_event, MeterStreamEvent::TELEGRAM);
  EXPECT_EQ(last_lines, fixture_lines());
}

TEST(corrupted_byte_reports_telegram_crc_and_no_lines) {
  // Arrange: one row per position in the captured telegram to corrupt
  const Bytes telegram = fixture();
  struct Case {
    const char *name;
    size_t offset;
  };
  const Case cases[] = {
      {"first payload byte", START_LEN},
      {"middle of the payload", telegram.size() / 2},
      {"last padding byte", telegram.size() - END_LEN - 1},
      {"padding count", telegram.size() - CRC_LEN - 1},
      {"CRC low byte", telegram.size() - CRC_LEN},
      {"CRC high byte", telegram.size() - 1},
  };

  for (const Case &row : cases) {
    case_label = row.name;
    // Arrange: the captured telegram with one bit flipped at the row's position
    Bytes corrupted = telegram;
    corrupted[row.offset] ^= FLIP;

    // Act
    const StreamRun run = feed_all(corrupted);

    // Assert
    EXPECT_EQ(run.events(), Events{MeterStreamEvent::TELEGRAM_CRC});
    EXPECT_EQ(run.first(MeterStreamEvent::TELEGRAM_CRC).telegram_length, corrupted.size());
    EXPECT_EQ(run.lines, Lines{});
  }
}

TEST(undecodable_telegram_appends_no_lines_and_keeps_earlier_ones) {
  // Arrange: one row per telegram that passes its CRC but does not decode
  constexpr uint8_t PADDING_ABOVE_MAX = MAX_PADDING + 1;
  struct Case {
    const char *name;
    Bytes telegram;
  };
  const Case cases[] = {
      {"decodable entry followed by a malformed one", undecodable()},
      {"padding count above three", frame(get_list_message({sentinel_entry()}), PADDING_ABOVE_MAX)},
  };

  for (const Case &row : cases) {
    case_label = row.name;
    // Arrange: a fresh reader and a line list that already holds an earlier line
    MeterStreamReader reader;
    const Lines earlier_lines = {EARLIER_LINE};

    // Act
    const StreamRun run = feed_all(reader, row.telegram, earlier_lines);

    // Assert
    EXPECT_EQ(run.events(), Events{MeterStreamEvent::TELEGRAM_DECODE});
    EXPECT_EQ(run.lines, Lines{EARLIER_LINE});
  }
}

TEST(entries_that_cannot_be_rendered_are_skipped_and_the_rest_published) {
  // Arrange: a telegram whose first entry has a scaler too large to render, then the sentinel entry
  constexpr int64_t BEYOND_SHIFT = 10;
  const Bytes unrenderable = entry(OBIS_UNDER_TEST, unit(UNIT_WATT), scaler(BEYOND_SHIFT), unsigned_int(1, 1));
  const Bytes telegram = frame(get_list_message({unrenderable, sentinel_entry()}));

  // Act
  const StreamRun run = feed_all(telegram);

  // Assert
  EXPECT_EQ(run.events(), Events{MeterStreamEvent::TELEGRAM});
  EXPECT_EQ(run.lines, Lines{SENTINEL_LINE});
}

TEST(escaped_escape_in_the_payload_neither_restarts_nor_ends_the_telegram) {
  // Arrange: one row per octet value that starts with an escape block and the hex it renders to
  const Bytes escape = escape_block();
  struct Case {
    const char *name;
    Bytes value;
    std::string expected_value;
  };
  const Case cases[] = {
      {"escape block then 01 01 01 01", join({escape, repeat(START_MARK, BLOCK_LEN)}), "1b1b1b1b01010101"},
      {"escape block then the end mark", join({escape, {END_MARK, 0x00, 0x11, 0x22}}), "1b1b1b1b1a001122"},
  };

  for (const Case &row : cases) {
    case_label = row.name;
    // Arrange: the row's value block-aligned in a complete telegram
    const Bytes telegram = frame(payload_with_octet_value_at(row.value, 0));

    // Act
    const StreamRun run = feed_all(telegram);

    // Assert
    EXPECT_EQ(run.events(), Events{MeterStreamEvent::TELEGRAM});
    EXPECT_EQ(run.lines, Lines{OBIS_UNDER_TEST_TEXT + "(" + row.expected_value + ")"});
  }
}

// ===========================================================================
// Text

TEST(text_lines_end_at_line_feed_and_drop_carriage_returns) {
  // Arrange: one row per text input and the lines it yields
  struct Case {
    const char *name;
    std::string text;
    Lines expected;
  };
  const Case cases[] = {
      {"CR LF terminated", "abc\r\n", {"abc"}},
      {"LF terminated", "abc\n", {"abc"}},
      {"CR inside a line", "a\rb\n", {"ab"}},
      {"two lines", "a\r\nb\r\n", {"a", "b"}},
      {"empty lines are not emitted", "\r\n\n\r\n", {}},
      {"unterminated text stays pending", "abc", {}},
  };

  for (const Case &row : cases) {
    case_label = row.name;
    // Act
    const StreamRun run = feed_all(ascii(row.text));
    // Assert
    EXPECT_EQ(run.lines, row.expected);
    EXPECT_EQ(run.events(), Events{});
  }
}

TEST(text_line_over_the_limit_is_split_and_the_splitting_byte_dropped) {
  // Arrange: one row per text line around MAX_LINE_LEN and the lines it yields
  constexpr size_t MAX = MeterStreamReader::MAX_LINE_LEN;
  constexpr size_t TAIL = 5;
  const std::string full(MAX + 1, 'a');
  struct Case {
    const char *name;
    std::string text;
    Lines expected;
  };
  const Case cases[] = {
      {"exactly the limit", std::string(MAX, 'a') + "\n", {std::string(MAX, 'a')}},
      {"one over the limit, ended by the line feed", full + "\n", {full}},
      {"one over the limit, next byte dropped", full + "X" + "tail\n", {full, "tail"}},
      {"long run split repeatedly", std::string(2 * (MAX + 2) + TAIL, 'a') + "\n",
       {full, full, std::string(TAIL, 'a')}},
  };

  for (const Case &row : cases) {
    case_label = row.name;
    // Act
    const StreamRun run = feed_all(ascii(row.text));
    // Assert
    EXPECT_EQ(run.lines, row.expected);
  }
}

const std::string TEXT_BEFORE = "/XYZ5EXAMPLE\r\n1-0:1.8.0*255(001234.5678*kWh)\r\n";
const Lines TEXT_BEFORE_LINES = {"/XYZ5EXAMPLE", "1-0:1.8.0*255(001234.5678*kWh)"};
const std::string TEXT_AFTER = "1-0:2.8.0*255(000000.0001*kWh)\r\n!\r\n";
const Lines TEXT_AFTER_LINES = {"1-0:2.8.0*255(000000.0001*kWh)", "!"};

TEST(text_7e1_read_at_8n1_has_its_parity_bit_cleared) {
  // Arrange: text lines as 7E1 sends them, with the parity bit set on some bytes
  const Bytes received = with_even_parity(TEXT_BEFORE);
  size_t with_parity_bit = 0;
  for (const uint8_t byte : received) with_parity_bit += (byte & PARITY_BIT) != 0;
  REQUIRE(with_parity_bit > 0);

  // Act
  const StreamRun run = feed_all(received);

  // Assert
  EXPECT_EQ(run.lines, TEXT_BEFORE_LINES);
}

TEST(text_7e1_lines_before_and_after_a_telegram_are_all_read) {
  // Arrange: 7E1 text lines, the captured telegram, then more 7E1 text lines
  const Bytes stream = join({with_even_parity(TEXT_BEFORE), fixture(), with_even_parity(TEXT_AFTER)});

  // Act
  const StreamRun run = feed_all(stream);

  // Assert
  EXPECT_EQ(run.lines, concat({TEXT_BEFORE_LINES, fixture_lines(), TEXT_AFTER_LINES}));
  EXPECT_EQ(run.events(), Events{MeterStreamEvent::TELEGRAM});
}

TEST(unterminated_text_before_a_start_sequence_is_dropped) {
  // Arrange: text without a line feed, the captured telegram, then a terminated line
  const Bytes stream = join({ascii("partial line"), fixture(), ascii("after\n")});

  // Act
  const StreamRun run = feed_all(stream);

  // Assert
  EXPECT_EQ(run.lines, concat({fixture_lines(), {"after"}}));
}

TEST(start_sequence_after_stray_escape_bytes_is_found) {
  // Arrange: a text line, three stray escape bytes, then the captured telegram
  constexpr size_t STRAY = 3;
  const Bytes stream = join({ascii("x\n"), repeat(ESCAPE_BYTE, STRAY), fixture()});

  // Act
  const StreamRun run = feed_all(stream);

  // Assert
  EXPECT_EQ(run.lines, concat({{"x"}, fixture_lines()}));
  EXPECT_EQ(run.events(), Events{MeterStreamEvent::TELEGRAM});
}

// ===========================================================================
// Resynchronisation

TEST(cut_or_garbled_telegrams_resync_and_following_telegrams_decode) {
  // Arrange: one row per damaged stream and the restarts, CRC failures and telegrams it yields
  const Bytes telegram = fixture();
  const Bytes aligned_head = slice(telegram, 0, ALIGNED_CUT);
  const Bytes unaligned_head = slice(telegram, 0, UNALIGNED_CUT);
  struct Case {
    const char *name;
    Bytes stream;
    size_t restarts;
    size_t crc_failures;
    size_t telegrams;
  };
  const Case cases[] = {
      {"unaligned cut then two full telegrams", join({unaligned_head, telegram, telegram}), 1, 0, 2},
      {"aligned cut then two full telegrams", join({aligned_head, telegram, telegram}), 1, 0, 2},
      {"four bytes lost mid-telegram then a full one",
       join({aligned_head, slice(telegram, ALIGNED_CUT + BLOCK_LEN, telegram.size()), telegram}), 0, 1, 1},
      {"one byte lost mid-telegram then a full one",
       join({aligned_head, slice(telegram, ALIGNED_CUT + 1, telegram.size()), telegram}), 1, 0, 1},
      {"one extra byte mid-telegram then a full one",
       join({aligned_head, {FILLER}, slice(telegram, ALIGNED_CUT, telegram.size()), telegram}), 1, 0, 1},
  };

  for (const Case &row : cases) {
    case_label = row.name;
    // Act
    const StreamRun run = feed_all(row.stream);
    const Events events = run.events();
    // Assert
    EXPECT_EQ(count(events, MeterStreamEvent::TELEGRAM_RESTART), row.restarts);
    EXPECT_EQ(count(events, MeterStreamEvent::TELEGRAM_CRC), row.crc_failures);
    EXPECT_EQ(count(events, MeterStreamEvent::TELEGRAM), row.telegrams);
    EXPECT_EQ(count(events, MeterStreamEvent::TELEGRAM_DECODE), size_t{0});
    EXPECT_EQ(run.lines, repeated(fixture_lines(), row.telegrams));
  }
}

TEST(any_cut_position_resyncs_within_two_telegrams) {
  // Arrange: the captured telegram, its lines, and a list for the cut positions that fail to resync
  const Bytes telegram = fixture();
  const Lines expected = fixture_lines();
  std::vector<size_t> failing_cuts;

  // Act: for every cut position, the cut telegram then two full ones
  for (size_t cut = 1; cut < telegram.size(); cut++) {
    const StreamRun run = feed_all(join({slice(telegram, 0, cut), telegram, telegram}));
    const Events events = run.events();
    const bool last_decoded = run.lines.size() >= expected.size() &&
                              Lines(run.lines.end() - (std::ptrdiff_t) expected.size(), run.lines.end()) == expected;
    const bool resynced = last_decoded && !events.empty() && events.back() == MeterStreamEvent::TELEGRAM &&
                          count(events, MeterStreamEvent::TELEGRAM_DECODE) == 0;
    if (!resynced) failing_cuts.push_back(cut);
  }

  // Assert
  EXPECT_EQ(failing_cuts, std::vector<size_t>{});
}

// ===========================================================================
// Dropped telegrams

TEST(telegram_one_byte_below_the_limit_is_still_collected) {
  // Arrange: a start sequence and filler one byte short of MAX_TELEGRAM_LEN
  const Bytes stream = join({start_sequence(), repeat(FILLER, MAX_TELEGRAM - START_LEN - 1)});

  // Act
  const StreamRun run = feed_all(stream);

  // Assert
  EXPECT_EQ(run.events(), Events{});
}

TEST(overflow_drops_the_telegram_and_discards_until_the_next_start) {
  // Arrange: a fresh reader, and a text line, the captured telegram and another line to send after an overflow
  MeterStreamReader reader;
  const Bytes rest = join({ascii("1-0:1.8.0*255(1.0*kWh)\n"), fixture(), ascii("after\n")});

  // Act
  const StreamRun overflow = feed_all(reader, overflowing());
  const StreamRun after = feed_all(reader, rest);

  // Assert
  EXPECT_EQ(overflow.events(), Events{MeterStreamEvent::TELEGRAM_OVERFLOW});
  EXPECT_EQ(after.events(), Events{MeterStreamEvent::TELEGRAM});
  EXPECT_EQ(after.lines, concat({fixture_lines(), {"after"}}));
}

TEST(text_after_an_overflow_stays_discarded_without_a_new_start) {
  // Arrange: a telegram that overflows, followed by 8N1 and 7E1 text lines and no new start
  const Bytes text = join({ascii("line one\nline two\r\n"), with_even_parity("seven\r\n")});
  const Bytes stream = join({start_sequence(), repeat(FILLER, MAX_TELEGRAM), text});

  // Act
  const StreamRun run = feed_all(stream);

  // Assert
  EXPECT_EQ(run.events(), Events{MeterStreamEvent::TELEGRAM_OVERFLOW});
  EXPECT_EQ(run.lines, Lines{});
}

// Whatever the reason for a drop, the binary bytes after it are never read
// as text.
TEST(bytes_after_any_dropped_telegram_are_skipped_until_the_next_start) {
  // Arrange: one row per dropped telegram, followed by bytes that must not be read as text, then the captured one
  const Bytes telegram = fixture();
  const Bytes text = ascii("1-0:1.8.0*255(1.0*kWh)\n");
  struct Case {
    const char *name;
    Bytes stream;
    MeterStreamEvent drop;
  };
  const Case cases[] = {
      {"after an overflow", join({start_sequence(), repeat(FILLER, MAX_TELEGRAM), text, telegram}),
       MeterStreamEvent::TELEGRAM_OVERFLOW},
      {"after a CRC failure", join({crc_broken(), text, telegram}), MeterStreamEvent::TELEGRAM_CRC},
      {"after a decode failure", join({undecodable(), text, telegram}), MeterStreamEvent::TELEGRAM_DECODE},
      // Losing the CRC completes the telegram with the next start's escape
      // bytes; the rest of that start must not turn the telegram into text.
      {"after a telegram missing its last byte", join({slice(telegram, 0, telegram.size() - 1), telegram, telegram}),
       MeterStreamEvent::TELEGRAM_CRC},
      {"after a telegram missing its CRC", join({slice(telegram, 0, telegram.size() - CRC_LEN), telegram, telegram}),
       MeterStreamEvent::TELEGRAM_CRC},
      {"after a telegram missing its padding count and CRC",
       join({slice(telegram, 0, telegram.size() - CRC_LEN - 1), telegram, telegram}), MeterStreamEvent::TELEGRAM_CRC},
  };

  for (const Case &row : cases) {
    case_label = row.name;
    // Act
    const StreamRun run = feed_all(row.stream);
    // Assert
    EXPECT_EQ(count(run.events(), row.drop), size_t{1});
    EXPECT_EQ(run.lines, fixture_lines());
  }
}

TEST(dropped_telegrams_report_their_length) {
  // Arrange: one row per drop reason and the telegram length it reports
  struct Case {
    const char *name;
    Bytes stream;
    MeterStreamEvent drop;
    size_t expected_length;
  };
  const Case cases[] = {
      {"CRC failure", crc_broken(), MeterStreamEvent::TELEGRAM_CRC, fixture().size()},
      {"decode failure", undecodable(), MeterStreamEvent::TELEGRAM_DECODE, undecodable().size()},
      {"overflow", overflowing(), MeterStreamEvent::TELEGRAM_OVERFLOW, MAX_TELEGRAM},
      {"restart", join({cut_short(), fixture()}), MeterStreamEvent::TELEGRAM_RESTART, UNALIGNED_CUT},
  };

  for (const Case &row : cases) {
    case_label = row.name;
    // Act
    const StreamRun run = feed_all(row.stream);
    // Assert
    EXPECT_EQ(count(run.events(), row.drop), size_t{1});
    EXPECT_EQ(run.first(row.drop).telegram_length, row.expected_length);
  }
}

// ===========================================================================
// Lines lost and drop reasons

struct DropCase {
  const char *name;
  Bytes drop_stream;
  MeterStreamEvent drop;
};

std::vector<DropCase> drop_cases() {
  return {
      {"CRC failure", crc_broken(), MeterStreamEvent::TELEGRAM_CRC},
      {"decode failure", undecodable(), MeterStreamEvent::TELEGRAM_DECODE},
      {"overflow", overflowing(), MeterStreamEvent::TELEGRAM_OVERFLOW},
      {"restart", join({cut_short(), start_sequence()}), MeterStreamEvent::TELEGRAM_RESTART},
  };
}

TEST(lines_lost_is_one_before_any_telegram_is_decoded) {
  for (const DropCase &row : drop_cases()) {
    case_label = row.name;
    // Arrange: a fresh reader that has decoded no telegram
    MeterStreamReader reader;
    // Act
    const StreamRun run = feed_all(reader, row.drop_stream);
    // Assert
    EXPECT_EQ(run.events(), Events{row.drop});
    EXPECT_EQ(run.first(row.drop).lines_lost, size_t{1});
  }
}

TEST(lines_lost_is_the_line_count_of_the_last_decoded_telegram) {
  for (const DropCase &row : drop_cases()) {
    case_label = std::string(row.name) + " after the real telegram";
    // Arrange: a reader that has decoded the captured telegram
    MeterStreamReader reader;
    feed_all(reader, fixture());
    // Act
    const StreamRun run = feed_all(reader, row.drop_stream);
    // Assert
    EXPECT_EQ(run.first(row.drop).lines_lost, FIXTURE_LINE_COUNT);
  }
}

TEST(lines_lost_is_one_after_a_later_one_line_telegram) {
  for (const DropCase &row : drop_cases()) {
    case_label = std::string(row.name) + " after a one-line telegram that followed the real one";
    // Arrange: a reader that saw the captured telegram, this drop, the captured telegram, then a one-line telegram
    MeterStreamReader reader;
    feed_all(reader, fixture());
    feed_all(reader, row.drop_stream);
    feed_all(reader, join({fixture(), frame(get_list_message({sentinel_entry()}))}));
    // Act
    const StreamRun later = feed_all(reader, row.drop_stream);
    // Assert
    EXPECT_EQ(later.first(row.drop).lines_lost, size_t{1});
  }
}

TEST(telegram_without_rendered_lines_leaves_lines_lost_unchanged) {
  // Arrange: a reader that decoded the captured telegram, then a telegram with no entries
  MeterStreamReader reader;
  feed_all(reader, fixture());
  const StreamRun empty = feed_all(reader, frame({}));

  // Act
  const StreamRun run = feed_all(reader, crc_broken());

  // Assert
  EXPECT_EQ(empty.events(), Events{MeterStreamEvent::TELEGRAM});
  EXPECT_EQ(empty.lines, Lines{});
  EXPECT_EQ(run.first(MeterStreamEvent::TELEGRAM_CRC).lines_lost, FIXTURE_LINE_COUNT);
}

TEST(dropped_telegram_reason_names_each_drop_and_nothing_else) {
  // Arrange: the drop events, the other events, and a set for the distinct reasons
  const Events drops = {MeterStreamEvent::TELEGRAM_RESTART, MeterStreamEvent::TELEGRAM_CRC,
                        MeterStreamEvent::TELEGRAM_DECODE, MeterStreamEvent::TELEGRAM_OVERFLOW};
  const Events not_drops = {MeterStreamEvent::NONE, MeterStreamEvent::TELEGRAM};
  std::set<std::string> reasons;

  // Act / Assert
  for (const MeterStreamEvent event : drops) {
    case_label = describe(event);
    const char *reason = dropped_telegram_reason(event);
    EXPECT(reason != nullptr && strlen(reason) > 0);
    if (reason != nullptr) reasons.insert(reason);
  }
  case_label.clear();
  EXPECT_EQ(reasons.size(), drops.size());
  for (const MeterStreamEvent event : not_drops) {
    case_label = describe(event);
    EXPECT_EQ(dropped_telegram_reason(event), (const char *) nullptr);
  }
}

TEST(every_drop_the_reader_reports_has_a_reason) {
  for (const DropCase &row : drop_cases()) {
    case_label = row.name;
    // Arrange: a fresh reader
    MeterStreamReader reader;
    // Act
    const StreamRun run = feed_all(reader, row.drop_stream);
    // Assert
    for (const MeterStreamResult &result : run.results) {
      EXPECT((dropped_telegram_reason(result.event) != nullptr) == (result.event != MeterStreamEvent::TELEGRAM));
    }
  }
}

}  // namespace
