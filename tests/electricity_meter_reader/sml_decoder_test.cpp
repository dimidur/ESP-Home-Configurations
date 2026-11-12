// Tests for components/electricity_meter_reader/sml_decoder: framing, CRC
// and decoding a telegram into SmlEntry values.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <vector>

#include "../../components/electricity_meter_reader/sml_decoder.h"
#include "../harness/harness.h"
#include "sml_builder.h"

namespace {

using esphome::electricity_meter_reader::sml_classify_telegram;
using esphome::electricity_meter_reader::sml_crc_ok;
using esphome::electricity_meter_reader::sml_decode_entries;
using esphome::electricity_meter_reader::sml_ends_with_start;
using esphome::electricity_meter_reader::sml_is_complete;
using esphome::electricity_meter_reader::sml_is_start;
using namespace meter_test;

using Entries = std::vector<SmlEntry>;

constexpr uint8_t FILLER = 0x55;
constexpr size_t ROOMY_LIMIT = 4096;

// Decodes a copy of telegram; the decoder consumes its input.
bool decode(const Bytes &telegram, Entries &entries) {
  Bytes copy = telegram;
  return sml_decode_entries(copy, entries);
}

void expect_entries(const Bytes &telegram, const Entries &expected) {
  Entries entries;
  EXPECT(decode(telegram, entries));
  EXPECT_EQ(describe_each(entries), describe_each(expected));
}

// ===========================================================================
// Framing

TEST(sml_is_start_accepts_only_the_exact_start_sequence) {
  // Arrange: a window holding the exact start sequence
  uint8_t window[esphome::electricity_meter_reader::SML_START_LEN];
  const Bytes start = start_sequence();
  std::copy(start.begin(), start.end(), window);

  // Act / Assert
  EXPECT(sml_is_start(window));
  for (size_t i = 0; i < START_LEN; i++) {
    case_label = "byte " + std::to_string(i) + " altered";
    std::copy(start.begin(), start.end(), window);
    window[i] ^= 1;
    EXPECT(!sml_is_start(window));
  }
}

TEST(sml_ends_with_start_finds_framing_starts_but_not_escaped_payload) {
  // Arrange: one row per telegram tail and whether it ends with a new start
  constexpr uint8_t NOT_START_MARK = START_MARK + 1;
  const Bytes start = start_sequence();
  const Bytes escape = escape_block();
  const Bytes block = repeat(FILLER, BLOCK_LEN);
  const Bytes almost_start = {START_MARK, START_MARK, START_MARK, NOT_START_MARK};
  struct Case {
    const char *name;
    Bytes telegram;
    bool expected;
  };
  const Case cases[] = {
      {"the telegram's own start sequence alone", start, false},
      {"a start after an aligned block", join({start, block, start}), true},
      {"a start one byte past a block boundary", join({start, {FILLER}, start}), true},
      {"a start whose escape completes an escaped escape", join({start, escape, start}), false},
      {"a start after a whole escaped escape", join({start, escape, escape, start}), true},
      {"an escape followed by 01 01 01 02", join({start, block, escape, almost_start}), false},
      {"an end sequence", join({start, block, escape, {END_MARK, MAX_PADDING, 0, 0}}), false},
  };

  for (const Case &row : cases) {
    case_label = row.name;
    // Act
    const bool restart = sml_ends_with_start(row.telegram);
    // Assert
    EXPECT_EQ(restart, row.expected);
  }
}

TEST(sml_is_complete_needs_an_aligned_framing_escape_and_end_mark) {
  // Arrange: one row per telegram tail and whether it completes the telegram
  constexpr uint8_t ANY_CRC_BYTE = 0x00;
  constexpr uint8_t NOT_END_MARK = END_MARK + 1;
  const Bytes start = start_sequence();
  const Bytes escape = escape_block();
  const Bytes block = repeat(FILLER, BLOCK_LEN);
  const Bytes tail = {END_MARK, 0, ANY_CRC_BYTE, ANY_CRC_BYTE};
  struct Case {
    const char *name;
    Bytes telegram;
    bool expected;
  };
  const Case cases[] = {
      {"end sequence after an aligned block", join({start, block, escape, tail}), true},
      {"end sequence with an empty payload", join({start, escape, tail}), true},
      {"end escape is the second half of an escaped escape", join({start, escape, escape, tail}), false},
      {"end sequence right after an escaped escape", join({start, escape, escape, escape, tail}), true},
      {"end escape one byte past a block boundary", join({start, {FILLER}, escape, tail}), false},
      {"byte after the escape is not 0x1a",
       join({start, block, escape, {NOT_END_MARK, 0, ANY_CRC_BYTE, ANY_CRC_BYTE}}), false},
      {"shorter than a start and an end sequence", join({start, escape}), false},
  };

  for (const Case &row : cases) {
    case_label = row.name;
    // Act
    const bool complete = sml_is_complete(row.telegram);
    // Assert
    EXPECT_EQ(complete, row.expected);
  }
}

TEST(sml_classify_telegram_prefers_restart_then_complete_then_overflow) {
  // Arrange: one row per telegram, buffer limit and the state it classifies as
  const Bytes start = start_sequence();
  const Bytes collecting = join({start, repeat(FILLER, BLOCK_LEN)});
  const Bytes restarting = join({start, repeat(FILLER, BLOCK_LEN), start});
  const Bytes complete = frame({});
  struct Case {
    const char *name;
    Bytes telegram;
    size_t max_length;
    SmlTelegramState expected;
  };
  const Case cases[] = {
      {"partial telegram below the limit", collecting, ROOMY_LIMIT, SmlTelegramState::COLLECTING},
      {"partial telegram one byte below the limit", collecting, collecting.size() + 1, SmlTelegramState::COLLECTING},
      {"partial telegram at the limit", collecting, collecting.size(), SmlTelegramState::OVERFLOW},
      {"complete telegram", complete, ROOMY_LIMIT, SmlTelegramState::COMPLETE},
      {"complete telegram exactly at the limit", complete, complete.size(), SmlTelegramState::COMPLETE},
      {"new start exactly at the limit", restarting, restarting.size(), SmlTelegramState::RESTART},
  };

  for (const Case &row : cases) {
    case_label = row.name;
    // Act
    const SmlTelegramState state = sml_classify_telegram(row.telegram, row.max_length);
    // Assert
    EXPECT_EQ(state, row.expected);
  }
}

// ===========================================================================
// CRC

TEST(sml_crc_ok_accepts_the_crc16_x25_check_value_low_byte_first) {
  // Arrange: CRC-16/X25 of "123456789" is 0x906e.
  constexpr uint8_t CHECK_LOW = 0x6e;
  constexpr uint8_t CHECK_HIGH = 0x90;
  const Bytes low_first = join({ascii("123456789"), {CHECK_LOW, CHECK_HIGH}});
  const Bytes high_first = join({ascii("123456789"), {CHECK_HIGH, CHECK_LOW}});

  // Act / Assert
  EXPECT(sml_crc_ok(low_first));
  EXPECT(!sml_crc_ok(high_first));
}

TEST(sml_crc_ok_handles_inputs_no_longer_than_the_crc) {
  // Arrange: the CRC of no data is init ^ xorout = 0x0000.
  const Bytes empty = {};
  const Bytes one_byte = {0x00};
  const Bytes crc_of_nothing = {0x00, 0x00};

  // Act / Assert
  EXPECT(!sml_crc_ok(empty));
  EXPECT(!sml_crc_ok(one_byte));
  EXPECT(sml_crc_ok(crc_of_nothing));
}

TEST(sml_crc_ok_accepts_the_fixture_and_rejects_every_single_bit_flip) {
  // Arrange: the captured telegram, whose own CRC holds
  const Bytes telegram = fixture();
  REQUIRE(sml_crc_ok(telegram));
  size_t accepted_flips = 0;

  // Act
  for (size_t at = 0; at < telegram.size(); at++) {
    for (unsigned bit = 0; bit < BITS_PER_BYTE; bit++) {
      Bytes flipped = telegram;
      flipped[at] ^= (uint8_t) (1u << bit);
      accepted_flips += sml_crc_ok(flipped);
    }
  }

  // Assert
  EXPECT_EQ(accepted_flips, size_t{0});
}

// ===========================================================================
// Decoding

TEST(real_telegram_decodes_to_22_entries_with_their_fields) {
  // Arrange: the captured telegram and the indices of the entries checked field by field
  constexpr size_t LOG_INDEX = 0;
  constexpr size_t SERVER_ID_INDEX = 1;
  constexpr size_t IMPORT_INDEX = 2;
  constexpr size_t POWER_INDEX = 4;
  constexpr size_t CURRENT_L1_INDEX = 11;
  constexpr size_t ANGLE_INDEX = 14;
  constexpr size_t FIRMWARE_INDEX = 20;
  constexpr uint64_t IMPORT_TENTHS_WH = 93440;
  constexpr uint64_t POWER_W = 1134;
  constexpr uint64_t CURRENT_HUNDREDTHS_A = 71;
  constexpr uint64_t ANGLE_TENTHS_DEG = 1206;
  constexpr int8_t TENTHS = -1;
  constexpr int8_t HUNDREDTHS = -2;
  const std::string meter_id = {0x0a, 0x01, 'L', 'O', 'G', 0x00, 0x00, (char) 0xbc, 0x61, 0x4e};
  const Bytes telegram = fixture();
  Entries entries;

  // Act
  const bool decoded = decode(telegram, entries);

  // Assert
  EXPECT(decoded);
  EXPECT_EQ(entries.size(), FIXTURE_LINE_COUNT);
  if (entries.size() != FIXTURE_LINE_COUNT) return;
  EXPECT_EQ(describe(entries[LOG_INDEX]), describe(octets_entry({1, 0, 96, 50, 1, 1}, std::nullopt, 0, "LOG")));
  EXPECT_EQ(describe(entries[SERVER_ID_INDEX]),
            describe(octets_entry({1, 0, 96, 1, 0, 255}, std::nullopt, 0, meter_id)));
  EXPECT_EQ(describe(entries[IMPORT_INDEX]),
            describe(number_entry({1, 0, 1, 8, 0, 255}, UNIT_WATT_HOUR, TENTHS, false, IMPORT_TENTHS_WH)));
  EXPECT_EQ(describe(entries[POWER_INDEX]),
            describe(number_entry({1, 0, 16, 7, 0, 255}, UNIT_WATT, 0, true, POWER_W)));
  EXPECT_EQ(describe(entries[CURRENT_L1_INDEX]),
            describe(number_entry({1, 0, 31, 7, 0, 255}, UNIT_AMPERE, HUNDREDTHS, false, CURRENT_HUNDREDTHS_A)));
  EXPECT_EQ(describe(entries[ANGLE_INDEX]),
            describe(number_entry({1, 0, 81, 7, 1, 255}, UNIT_DEGREE, TENTHS, true, ANGLE_TENTHS_DEG)));
  EXPECT_EQ(describe(entries[FIRMWARE_INDEX]), describe(octets_entry({1, 0, 0, 2, 0, 0}, std::nullopt, 0, "06")));
}

TEST(telegram_with_an_empty_payload_decodes_to_no_entries) {
  // Arrange: a complete telegram with no messages in its payload
  const Bytes telegram = frame({});

  // Act / Assert
  expect_entries(telegram, {});
}

TEST(sml_decode_entries_rejects_input_shorter_than_start_and_end_sequences) {
  // Arrange: a start sequence and end escape cut to one byte less than a start and an end sequence
  Bytes too_short = join({start_sequence(), escape_block()});
  too_short.resize(START_LEN + END_LEN - 1, END_MARK);
  Entries entries;

  // Act
  const bool decoded = sml_decode_entries(too_short, entries);

  // Assert
  EXPECT(!decoded);
}

TEST(messages_other_than_get_list_res_are_skipped) {
  // Arrange: a PublicOpen.Res followed by a GetList.Res holding the sentinel entry
  const Bytes open = message(TRANSACTION_ID, body(TAG_PUBLIC_OPEN_RES, list_of({absent(), absent()})));
  const Bytes payload = join({open, get_list_message({sentinel_entry()})});

  // Act / Assert
  expect_entries(frame(payload), {sentinel_decoded()});
}

TEST(every_get_list_res_in_a_telegram_is_decoded_in_order) {
  // Arrange: two GetList.Res messages of one entry each
  const Bytes first = get_list_message({entry(OBIS_UNDER_TEST, unit(UNIT_WATT), scaler(0), unsigned_int(1, 1))});
  const Bytes second = get_list_message({sentinel_entry()});

  // Act / Assert
  expect_entries(frame(join({first, second})),
                 {number_entry(OBIS_UNDER_TEST, UNIT_WATT, 0, false, 1), sentinel_decoded()});
}

struct EntryCase {
  const char *name;
  Bytes obis;
  Bytes unit_field;
  Bytes scaler_field;
  Bytes value;
  std::optional<SmlEntry> expected;  // nullopt: entry left out
};

void run_entry_cases(const std::vector<EntryCase> &cases) {
  for (const EntryCase &row : cases) {
    case_label = row.name;
    // Arrange: the row's entry then the sentinel entry, in one GetList.Res of a complete telegram
    const Bytes under_test = entry(row.obis, row.unit_field, row.scaler_field, row.value);
    const Bytes telegram = frame(get_list_message({under_test, sentinel_entry()}));
    Entries expected;
    if (row.expected) expected.push_back(*row.expected);
    expected.push_back(sentinel_decoded());
    // Act / Assert
    expect_entries(telegram, expected);
  }
}

TEST(value_encodings_decode_to_entry_fields) {
  // Arrange: one row per value, unit and scaler encoding and the entry it decodes to
  constexpr uint64_t VALUE = 5;
  constexpr uint64_t TOP_BIT_ONLY = 0x80;
  constexpr uint64_t THREE_BYTE_VALUE = 0x10000;
  constexpr size_t TWO_BYTES = 2;
  constexpr size_t THREE_BYTES = 3;
  constexpr size_t FOUR_BYTES = 4;
  constexpr size_t EIGHT_BYTES = 8;
  constexpr int64_t NEGATIVE_SMALL = -5;
  constexpr int64_t NEGATIVE_WIDE = -1234;
  constexpr uint8_t BOOL_ODD_TRUE = 0x05;
  constexpr uint8_t SIGNED_UNIT = 5;
  constexpr int8_t TENTHS = -1;
  constexpr uint64_t U64_MAX = std::numeric_limits<uint64_t>::max();
  constexpr int64_t I64_MIN = std::numeric_limits<int64_t>::min();
  const Bytes watt_unit = unit(UNIT_WATT);
  const Bytes obis = OBIS_UNDER_TEST;
  const auto unsigned_watts = [&](uint64_t raw) { return number_entry(obis, UNIT_WATT, 0, false, raw); };
  const auto signed_watts = [&](int64_t raw) { return number_entry(obis, UNIT_WATT, 0, true, (uint64_t) raw); };
  const std::vector<EntryCase> cases = {
      {"Unsigned64 maximum", obis, watt_unit, scaler(0), unsigned_int(U64_MAX, EIGHT_BYTES), unsigned_watts(U64_MAX)},
      {"Unsigned8 with the top bit set stays unsigned", obis, watt_unit, scaler(0),
       unsigned_int(TOP_BIT_ONLY, BYTE_FIELD), unsigned_watts(TOP_BIT_ONLY)},
      {"three-byte Unsigned", obis, watt_unit, scaler(0), unsigned_int(THREE_BYTE_VALUE, THREE_BYTES),
       unsigned_watts(THREE_BYTE_VALUE)},
      {"Integer8 is sign-extended", obis, watt_unit, scaler(0), signed_int(NEGATIVE_SMALL, BYTE_FIELD),
       signed_watts(NEGATIVE_SMALL)},
      {"Integer16 is sign-extended", obis, watt_unit, scaler(0), signed_int(NEGATIVE_WIDE, TWO_BYTES),
       signed_watts(NEGATIVE_WIDE)},
      {"positive Integer8", obis, watt_unit, scaler(0), signed_int((int64_t) VALUE, BYTE_FIELD),
       signed_watts((int64_t) VALUE)},
      {"Integer64 -1 uses all eight bytes", obis, watt_unit, scaler(0), signed_int(-1, EIGHT_BYTES), signed_watts(-1)},
      {"Integer64 lowest value", obis, watt_unit, scaler(0), signed_int(I64_MIN, EIGHT_BYTES), signed_watts(I64_MIN)},
      {"Boolean true is 1", obis, absent(), absent(), boolean(1), number_entry(obis, std::nullopt, 0, false, 1)},
      {"Boolean with any non-zero byte is 1", obis, absent(), absent(), boolean(BOOL_ODD_TRUE),
       number_entry(obis, std::nullopt, 0, false, 1)},
      {"Boolean false is 0", obis, absent(), absent(), boolean(0), number_entry(obis, std::nullopt, 0, false, 0)},
      {"absent unit and scaler", obis, absent(), absent(), unsigned_int(VALUE, BYTE_FIELD),
       number_entry(obis, std::nullopt, 0, false, VALUE)},
      {"in-range scaler sent as Integer32", obis, watt_unit, signed_int(TENTHS, FOUR_BYTES),
       unsigned_int(VALUE, BYTE_FIELD), number_entry(obis, UNIT_WATT, TENTHS, false, VALUE)},
      {"in-range unit sent as Integer8", obis, signed_int(SIGNED_UNIT, BYTE_FIELD), scaler(0),
       unsigned_int(VALUE, BYTE_FIELD), number_entry(obis, SIGNED_UNIT, 0, false, VALUE)},
      {"highest unit code", obis, unit(UNIT_UNKNOWN), scaler(0), unsigned_int(VALUE, BYTE_FIELD),
       number_entry(obis, UNIT_UNKNOWN, 0, false, VALUE)},
      {"octet value keeps its bytes, unit and scaler", obis, watt_unit, scaler(TENTHS), text_octets("A(B"),
       octets_entry(obis, UNIT_WATT, TENTHS, "A(B")},
      {"octet value with UTF-8 bytes", obis, absent(), absent(), text_octets("\xc3\xa4"),
       octets_entry(obis, std::nullopt, 0, "\xc3\xa4")},
  };

  // Act / Assert: per row, in run_entry_cases
  run_entry_cases(cases);
}

TEST(entries_outside_the_field_types_are_left_out_and_the_telegram_still_decodes) {
  // Arrange: one row per entry whose name, unit, scaler or value is outside its field type
  constexpr int64_t INT8_HIGHEST_PLUS_ONE = 128;
  constexpr int64_t INT8_LOWEST_MINUS_ONE = -129;
  constexpr uint64_t UINT8_HIGHEST_PLUS_ONE = 256;
  constexpr size_t TWO_BYTES = 2;
  constexpr size_t FOUR_BYTES = 4;
  constexpr size_t EIGHT_BYTES = 8;
  constexpr size_t FIVE_BYTE_NAME = 5;
  constexpr int64_t I32_MIN = std::numeric_limits<int32_t>::min();
  constexpr int64_t I64_MIN = std::numeric_limits<int64_t>::min();
  constexpr int64_t I64_MAX = std::numeric_limits<int64_t>::max();
  const Bytes watt_unit = unit(UNIT_WATT);
  const Bytes obis = OBIS_UNDER_TEST;
  const Bytes one = unsigned_int(1, BYTE_FIELD);
  const Bytes seven_byte_name = join({OBIS_UNDER_TEST, {0}});
  const std::vector<EntryCase> cases = {
      {"Integer16 scaler just above Integer8", obis, watt_unit, signed_int(INT8_HIGHEST_PLUS_ONE, TWO_BYTES), one,
       std::nullopt},
      {"Integer16 scaler just below Integer8", obis, watt_unit, signed_int(INT8_LOWEST_MINUS_ONE, TWO_BYTES), one,
       std::nullopt},
      {"Unsigned8 scaler above Integer8", obis, watt_unit, unsigned_int(INT8_HIGHEST_PLUS_ONE, BYTE_FIELD), one,
       std::nullopt},
      {"Integer32 lowest scaler", obis, watt_unit, signed_int(I32_MIN, FOUR_BYTES), one, std::nullopt},
      {"Integer64 lowest scaler", obis, watt_unit, signed_int(I64_MIN, EIGHT_BYTES), one, std::nullopt},
      {"Integer64 highest scaler", obis, watt_unit, signed_int(I64_MAX, EIGHT_BYTES), one, std::nullopt},
      {"Unsigned16 unit above Unsigned8", obis, unsigned_int(UINT8_HIGHEST_PLUS_ONE, TWO_BYTES), scaler(0), one,
       std::nullopt},
      {"negative unit", obis, signed_int(-1, BYTE_FIELD), scaler(0), one, std::nullopt},
      {"list-typed value", obis, watt_unit, scaler(0), list_of({one, one}), std::nullopt},
      {"absent value", obis, watt_unit, scaler(0), absent(), std::nullopt},
      {"empty OBIS name", {}, watt_unit, scaler(0), one, std::nullopt},
      {"five-byte OBIS name", slice(OBIS_UNDER_TEST, 0, FIVE_BYTE_NAME), watt_unit, scaler(0), one, std::nullopt},
      {"seven-byte OBIS name", seven_byte_name, watt_unit, scaler(0), one, std::nullopt},
  };

  // Act / Assert: per row, in run_entry_cases
  run_entry_cases(cases);
}

TEST(integer8_scalers_are_kept_whatever_shift_they_imply) {
  // Arrange: one row per Integer8 scaler beyond what obis_line renders
  constexpr int8_t LOWEST = std::numeric_limits<int8_t>::min();
  constexpr int8_t HIGHEST = std::numeric_limits<int8_t>::max();
  constexpr int8_t BEYOND_RENDERING = 10;
  const Bytes obis = OBIS_UNDER_TEST;
  const Bytes one = unsigned_int(1, BYTE_FIELD);
  const Bytes watt_hour_unit = unit(UNIT_WATT_HOUR);
  const auto kept = [&](int8_t scaler_value) { return number_entry(obis, UNIT_WATT_HOUR, scaler_value, false, 1); };
  const std::vector<EntryCase> cases = {
      {"lowest Integer8 scaler", obis, watt_hour_unit, scaler(LOWEST), one, kept(LOWEST)},
      {"scaler -10", obis, watt_hour_unit, scaler(-BEYOND_RENDERING), one, kept((int8_t) -BEYOND_RENDERING)},
      {"scaler +10", obis, watt_hour_unit, scaler(BEYOND_RENDERING), one, kept(BEYOND_RENDERING)},
      {"highest Integer8 scaler", obis, watt_hour_unit, scaler(HIGHEST), one, kept(HIGHEST)},
  };

  // Act / Assert: per row, in run_entry_cases
  run_entry_cases(cases);
}

TEST(padding_count_above_three_is_rejected) {
  // Arrange: a one-entry GetList.Res and padding counts above three
  constexpr uint8_t ABOVE_MAX = MAX_PADDING + 1;
  constexpr uint8_t BYTE_MAX = 0xff;
  const Bytes payload = get_list_message({sentinel_entry()});

  for (const uint8_t padding : {ABOVE_MAX, BYTE_MAX}) {
    case_label = "padding count " + std::to_string(padding);
    // Arrange: the payload framed with this padding count and a matching CRC
    const Bytes telegram = frame(payload, padding);
    Entries entries;

    // Act
    const bool decoded = decode(telegram, entries);

    // Assert
    EXPECT(sml_crc_ok(telegram));
    EXPECT(!decoded);
  }
}

TEST(hostile_structures_fail_decoding_without_hanging_or_overreading) {
  // Arrange: one row per payload that breaks one structural rule of SML
  constexpr size_t WRAPPING_CONTINUATIONS = 14;
  constexpr size_t FOUR_BYTE_TL_CONTINUATIONS = 3;
  constexpr size_t FIVE_BYTE_TL_CONTINUATIONS = 4;
  constexpr size_t CONTINUATION_TL_BYTES = 2;
  constexpr size_t ID_DATA_BYTES = 3;
  constexpr size_t DEEP_NESTING = 16;
  constexpr size_t INT_OVER_EIGHT_BYTES = 9;
  constexpr size_t EIGHT_BYTES = 8;
  constexpr uint8_t TL_CONTINUE = TL_MORE | TL_LENGTH_MAX;
  constexpr uint64_t U64_MAX = std::numeric_limits<uint64_t>::max();
  const uint8_t six_fields = short_tl(TL_LIST, MESSAGE_FIELDS);
  const Bytes watt_unit = unit(UNIT_WATT);
  const Bytes one = unsigned_int(1, BYTE_FIELD);
  const Bytes full_message = get_list_message({sentinel_entry()});
  Bytes nested = repeat(short_tl(TL_LIST, 1), DEEP_NESTING);
  nested.push_back(ABSENT);
  const Bytes deep_status =
      list_of({octets(OBIS_UNDER_TEST), nested, absent(), watt_unit, scaler(0), one, absent()});
  const Bytes six_field_entry = list_of({octets(OBIS_UNDER_TEST), absent(), absent(), watt_unit, scaler(0), one});
  Bytes wrapping = {six_fields, TL_MORE | TL_LIST | TL_LENGTH_MAX};
  append(wrapping, repeat(TL_CONTINUE, WRAPPING_CONTINUATIONS));
  wrapping.push_back(TL_LENGTH_MAX);
  wrapping.push_back(START_MARK);
  wrapping.push_back(TL_MORE | TL_OCTETS | TL_LENGTH_MAX);
  append(wrapping, repeat(TL_CONTINUE, WRAPPING_CONTINUATIONS));
  wrapping.push_back(TL_LENGTH_MAX);
  Bytes huge_list = {six_fields, TL_MORE | TL_LIST | TL_LENGTH_MAX};
  append(huge_list, repeat(TL_CONTINUE, FOUR_BYTE_TL_CONTINUATIONS - 1));
  huge_list.push_back(TL_LENGTH_MAX);
  // Otherwise valid messages whose transactionId type-length breaks one rule.
  const Bytes id_data = repeat(TRANSACTION_ID_BYTE, ID_DATA_BYTES);
  Bytes five_byte_tl = {TL_MORE | TL_OCTETS};
  append(five_byte_tl, repeat(TL_MORE, FIVE_BYTE_TL_CONTINUATIONS - 1));
  five_byte_tl.push_back((uint8_t) (FIVE_BYTE_TL_CONTINUATIONS + 1 + ID_DATA_BYTES));
  append(five_byte_tl, id_data);
  const Bytes typed_continuation =
      join({{TL_MORE | TL_OCTETS, (uint8_t) (TL_INT | (CONTINUATION_TL_BYTES + ID_DATA_BYTES))}, id_data});
  const Bytes sentinel_body = body(TAG_GET_LIST_RES, get_list_res({sentinel_entry()}));
  const Bytes wide_int = join({{short_tl(TL_INT, INT_OVER_EIGHT_BYTES + 1)}, repeat(0, INT_OVER_EIGHT_BYTES)});
  const Bytes tag = unsigned_int(TAG_GET_LIST_RES, TAG_BYTES);
  const std::vector<Bytes> six_absent(GET_LIST_FIELDS - 1, absent());
  const std::vector<Bytes> no_val_list = {absent(), octets(SERVER_ID), absent(), absent(), absent(), absent(),
                                          absent()};

  struct Case {
    const char *name;
    Bytes payload;
  };
  const std::vector<Case> cases = {
      {"type-length chain long enough to wrap a 64-bit length", wrapping},
      {"type-length of five bytes", message_with_id_field(five_byte_tl, sentinel_body)},
      {"continuation byte with type bits set", message_with_id_field(typed_continuation, sentinel_body)},
      {"list claiming 65535 elements", huge_list},
      {"octet string longer than the payload", {six_fields, TL_MORE | TL_OCTETS | TL_LENGTH_MAX, TL_LENGTH_MAX}},
      {"type-length shorter than its own bytes", {six_fields, TL_MORE | TL_OCTETS, 1}},
      {"type-length cut off by the end of the payload", {six_fields, ABSENT, ABSENT, TL_MORE | TL_OCTETS}},
      {"entry status nested deeper than the limit", get_list_message({deep_status})},
      {"message that is not a six-element list", list_of(std::vector<Bytes>(MESSAGE_FIELDS - 1, absent()))},
      {"message cut short", slice(full_message, 0, full_message.size() / 2)},
      {"body without a tag", message(TRANSACTION_ID, list_of({absent(), absent()}))},
      {"body that is not a two-element list", message(TRANSACTION_ID, list_of({tag}))},
      {"GetList.Res with six fields", message(TRANSACTION_ID, body(TAG_GET_LIST_RES, list_of(six_absent)))},
      {"valList that is not a list", message(TRANSACTION_ID, body(TAG_GET_LIST_RES, list_of(no_val_list)))},
      {"entry with six fields", get_list_message({six_field_entry})},
      {"decodable entry followed by a malformed one", get_list_message({sentinel_entry(), six_field_entry})},
      {"OBIS name that is not an octet string",
       get_list_message({list_of({one, absent(), absent(), watt_unit, scaler(0), one, absent()})})},
      {"unit sent as an octet string", get_list_message({entry(OBIS_UNDER_TEST, text_octets("W"), scaler(0), one)})},
      {"unit too large for a 64-bit signed integer",
       get_list_message({entry(OBIS_UNDER_TEST, unsigned_int(U64_MAX, EIGHT_BYTES), scaler(0), one)})},
      {"scaler too large for a 64-bit signed integer",
       get_list_message({entry(OBIS_UNDER_TEST, watt_unit, unsigned_int(U64_MAX, EIGHT_BYTES), one)})},
      {"integer value wider than eight bytes",
       get_list_message({entry(OBIS_UNDER_TEST, watt_unit, scaler(0), wide_int)})},
      {"integer value with no bytes",
       get_list_message({entry(OBIS_UNDER_TEST, watt_unit, scaler(0), {short_tl(TL_INT, 1)})})},
  };

  for (const Case &row : cases) {
    case_label = row.name;
    // Arrange: the row's payload in a complete telegram with a matching CRC
    const Bytes telegram = frame(row.payload);
    Entries entries;

    // Act
    const bool decoded = decode(telegram, entries);

    // Assert
    EXPECT(sml_is_complete(telegram));
    EXPECT(sml_crc_ok(telegram));
    EXPECT(!decoded);
  }
}

TEST(extended_type_lengths_of_up_to_four_bytes_decode) {
  // Arrange: a three-byte transactionId value and the GetList.Res body that follows it
  constexpr size_t ID_DATA_BYTES = 3;
  constexpr size_t MAX_TL_BYTES = 4;
  const Bytes id_data = repeat(TRANSACTION_ID_BYTE, ID_DATA_BYTES);
  const Bytes sentinel_body = body(TAG_GET_LIST_RES, get_list_res({sentinel_entry()}));

  for (size_t tl_bytes = 1; tl_bytes <= MAX_TL_BYTES; tl_bytes++) {
    case_label = std::to_string(tl_bytes) + "-byte type-length";
    // Arrange: the transactionId field with a type-length of tl_bytes bytes
    Bytes id_field = {tl_bytes > 1 ? (uint8_t) (TL_MORE | TL_OCTETS) : TL_OCTETS};
    for (size_t i = 2; i < tl_bytes; i++) id_field.push_back(TL_MORE);
    const uint8_t total = (uint8_t) (tl_bytes + ID_DATA_BYTES);
    if (tl_bytes > 1) {
      id_field.push_back(total);
    } else {
      id_field[0] |= total;
    }
    append(id_field, id_data);

    // Act / Assert
    expect_entries(frame(message_with_id_field(id_field, sentinel_body)), {sentinel_decoded()});
  }
}

// ===========================================================================
// Escaping

TEST(escaped_escape_in_the_payload_decodes_and_frames_as_one_telegram) {
  // Arrange: one row per octet value holding escape bytes, at an offset that does or does not align them
  const Bytes escape = escape_block();
  const Bytes doubled = join({escape, escape});
  const Bytes unaligned_tail = {0x02, 0x02, 0x02, 0x02};
  struct Case {
    const char *name;
    Bytes value;
    size_t offset_in_block;
    bool escaped_on_the_wire;
  };
  const Case cases[] = {
      {"aligned escape block alone", escape, 0, true},
      {"aligned escape block then 01 01 01 01", join({escape, repeat(START_MARK, BLOCK_LEN)}), 0, true},
      {"aligned escape block then the end mark", join({escape, {END_MARK, 0x00, 0x11, 0x22}}), 0, true},
      {"two aligned escape blocks back to back", doubled, 0, true},
      {"unaligned escape bytes are sent as they are", join({escape, unaligned_tail}), 1, false},
  };

  for (const Case &row : cases) {
    case_label = row.name;
    // Arrange: the row's value in a complete telegram, escaped on the wire exactly when aligned
    const Bytes telegram = frame(payload_with_octet_value_at(row.value, row.offset_in_block));
    REQUIRE(has_aligned(telegram, doubled) == row.escaped_on_the_wire);
    std::vector<SmlTelegramState> states;

    // Act
    for (size_t length = START_LEN; length <= telegram.size(); length++) {
      const SmlTelegramState state = sml_classify_telegram(slice(telegram, 0, length), ROOMY_LIMIT);
      if (state != SmlTelegramState::COLLECTING) states.push_back(state);
    }

    // Assert: complete only at its last byte, never restarted.
    EXPECT_EQ(states, std::vector<SmlTelegramState>{SmlTelegramState::COMPLETE});
    expect_entries(telegram, {octets_entry(OBIS_UNDER_TEST, std::nullopt, 0,
                                           std::string(row.value.begin(), row.value.end()))});
  }
}

}  // namespace
