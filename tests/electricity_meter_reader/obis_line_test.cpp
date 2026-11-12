// Tests for components/electricity_meter_reader/obis_line: rendering an
// SmlEntry as the OBIS text line a push-mode text meter sends.

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <vector>

#include "../../components/electricity_meter_reader/obis_line.h"
#include "../../components/electricity_meter_reader/sml_decoder.h"
#include "../harness/harness.h"
#include "sml_builder.h"

namespace {

using esphome::electricity_meter_reader::obis_line;
using esphome::electricity_meter_reader::sml_decode_entries;
using namespace meter_test;

struct RenderCase {
  const char *name;
  SmlEntry entry;
  std::optional<std::string> expected_value;  // nullopt: not rendered
};

void run_render_cases(const std::vector<RenderCase> &cases) {
  for (const RenderCase &row : cases) {
    case_label = row.name;

    // Act
    std::string line;
    const bool rendered = obis_line(row.entry, line);

    // Assert
    EXPECT_EQ(rendered, row.expected_value.has_value());
    if (rendered && row.expected_value) EXPECT_EQ(line, OBIS_UNDER_TEST_TEXT + "(" + *row.expected_value + ")");
  }
}

TEST(real_telegram_entries_render_to_the_22_expected_lines) {
  // Arrange: the entries decoded from the captured telegram
  Bytes telegram = fixture();
  std::vector<SmlEntry> entries;
  REQUIRE(sml_decode_entries(telegram, entries));
  Lines lines;
  size_t unrendered = 0;

  // Act
  for (const SmlEntry &decoded_entry : entries) {
    std::string line;
    if (obis_line(decoded_entry, line)) {
      lines.push_back(line);
    } else {
      unrendered++;
    }
  }

  // Assert
  EXPECT_EQ(unrendered, size_t{0});
  EXPECT_EQ(lines, fixture_lines());
}

TEST(obis_codes_render_as_six_decimal_groups) {
  // Arrange: one row per OBIS code and the text it renders to
  constexpr uint8_t GROUP_MAX = std::numeric_limits<uint8_t>::max();
  struct Case {
    const char *name;
    Bytes obis;
    std::string expected;
  };
  const Case cases[] = {
      {"all zero", {0, 0, 0, 0, 0, 0}, "0-0:0.0.0*0(1*W)"},
      {"all 255", {GROUP_MAX, GROUP_MAX, GROUP_MAX, GROUP_MAX, GROUP_MAX, GROUP_MAX}, "255-255:255.255.255*255(1*W)"},
      {"mixed", {1, 0, 96, 50, 1, 1}, "1-0:96.50.1*1(1*W)"},
  };

  for (const Case &row : cases) {
    case_label = row.name;
    // Act
    std::string line;
    const bool rendered = obis_line(number_entry(row.obis, UNIT_WATT, 0, false, 1), line);
    // Assert
    EXPECT(rendered);
    EXPECT_EQ(line, row.expected);
  }
}

TEST(numbers_render_as_exact_decimals_with_their_unit) {
  // Arrange: one row per number entry and the value text it renders to
  constexpr uint64_t VALUE = 5;
  constexpr uint64_t WATT_HOURS = 1234;
  constexpr uint64_t TOP_BIT_ONLY = 0x80;
  constexpr uint64_t TENTHS_VALUE = 15;
  constexpr int64_t NEGATIVE_SMALL = -5;
  constexpr int64_t NEGATIVE_WIDE = -1234;
  constexpr int8_t SHIFT_LIMIT = 9;
  constexpr int8_t KWH_SHIFT = 3;
  constexpr uint64_t U64_MAX = std::numeric_limits<uint64_t>::max();
  constexpr int64_t I64_MIN = std::numeric_limits<int64_t>::min();
  const Bytes obis = OBIS_UNDER_TEST;
  const auto watts = [&](int8_t scaler_value, bool is_signed, uint64_t raw) {
    return number_entry(obis, UNIT_WATT, scaler_value, is_signed, raw);
  };
  const auto watt_hours = [&](int8_t scaler_value, uint64_t raw) {
    return number_entry(obis, UNIT_WATT_HOUR, scaler_value, false, raw);
  };
  const auto with_unit = [&](uint8_t code, int8_t scaler_value, uint64_t raw) {
    return number_entry(obis, code, scaler_value, false, raw);
  };
  SmlEntry wh_code_without_unit = number_entry(obis, std::nullopt, -1, false, VALUE);
  wh_code_without_unit.unit = UNIT_WATT_HOUR;
  const std::vector<RenderCase> cases = {
      {"Unsigned64 maximum prints every digit", watts(0, false, U64_MAX), "18446744073709551615*W"},
      {"Unsigned64 maximum with scaler -1", watts(-1, false, U64_MAX), "1844674407370955161.5*W"},
      {"unsigned value with the top bit set is positive", watts(0, false, TOP_BIT_ONLY), "128*W"},
      {"unsigned 2^63 is positive", watts(0, false, (uint64_t) I64_MIN), "9223372036854775808*W"},
      {"negative value", watts(0, true, (uint64_t) NEGATIVE_SMALL), "-5*W"},
      {"negative value with scaler -1", watts(-1, true, (uint64_t) NEGATIVE_WIDE), "-123.4*W"},
      {"negative value below one keeps a leading zero", watts(-2, true, (uint64_t) NEGATIVE_SMALL), "-0.05*W"},
      {"lowest signed 64-bit value", watts(0, true, (uint64_t) I64_MIN), "-9223372036854775808*W"},
      {"lowest signed 64-bit value with scaler -1", watts(-1, true, (uint64_t) I64_MIN),
       "-922337203685477580.8*W"},
      {"zero keeps its decimals", watts(-2, false, 0), "0.00*W"},
      {"positive scaler appends zeros", watts(KWH_SHIFT, false, VALUE), "5000*W"},
      {"scaler +9 is the largest shift", watts(SHIFT_LIMIT, false, 1), "1000000000*W"},
      {"scaler -9 is the smallest shift", watts(-SHIFT_LIMIT, false, 1), "0.000000001*W"},
      {"one decimal", watts(-1, false, TENTHS_VALUE), "1.5*W"},
      {"Wh becomes kWh", watt_hours(0, WATT_HOURS), "1.234*kWh"},
      {"Wh with scaler +12 shifts nine places", watt_hours(SHIFT_LIMIT + KWH_SHIFT, 1), "1000000000*kWh"},
      {"Wh with scaler -6 shifts nine places", watt_hours(KWH_SHIFT - SHIFT_LIMIT, 1), "0.000000001*kWh"},
      {"degree", with_unit(UNIT_DEGREE, -1, TENTHS_VALUE), "1.5*deg"},
      {"ampere", with_unit(UNIT_AMPERE, -1, TENTHS_VALUE), "1.5*A"},
      {"volt", with_unit(UNIT_VOLT, -1, TENTHS_VALUE), "1.5*V"},
      {"hertz", with_unit(UNIT_HERTZ, -1, TENTHS_VALUE), "1.5*Hz"},
      {"unit outside the name table prints its code", with_unit(UNIT_UNKNOWN, 0, VALUE), "5*255"},
      {"no unit leaves the unit empty", number_entry(obis, std::nullopt, 0, false, VALUE), "5*"},
      {"Wh code without has_unit is not energy", wh_code_without_unit, "0.5*"},
  };

  // Act / Assert: per row, in run_render_cases
  run_render_cases(cases);
}

TEST(numbers_whose_point_moves_more_than_nine_places_are_not_rendered) {
  // Arrange: one row per scaler that moves the decimal point more than nine places
  constexpr int8_t BEYOND_SHIFT = 10;
  constexpr int8_t KWH_SHIFT = 3;
  constexpr int8_t LOWEST = std::numeric_limits<int8_t>::min();
  constexpr int8_t HIGHEST = std::numeric_limits<int8_t>::max();
  const Bytes obis = OBIS_UNDER_TEST;
  const auto watts = [&](int8_t scaler_value) { return number_entry(obis, UNIT_WATT, scaler_value, false, 1); };
  const auto watt_hours = [&](int8_t scaler_value) {
    return number_entry(obis, UNIT_WATT_HOUR, scaler_value, false, 1);
  };
  const std::vector<RenderCase> cases = {
      {"scaler -10", watts(-BEYOND_SHIFT), std::nullopt},
      {"scaler +10", watts(BEYOND_SHIFT), std::nullopt},
      {"lowest Integer8 scaler", watts(LOWEST), std::nullopt},
      {"highest Integer8 scaler", watts(HIGHEST), std::nullopt},
      {"Wh with scaler -7", watt_hours(KWH_SHIFT - BEYOND_SHIFT), std::nullopt},
      {"Wh with scaler +13", watt_hours(KWH_SHIFT + BEYOND_SHIFT), std::nullopt},
      {"Wh with the lowest Integer8 scaler", watt_hours(LOWEST), std::nullopt},
  };

  // Act / Assert: per row, in run_render_cases
  run_render_cases(cases);
}

TEST(octets_render_as_text_when_printable_else_as_hex) {
  // Arrange: one row per octet-string entry and the value text it renders to
  constexpr char DEL = 0x7f;
  constexpr char LINE_FEED = 0x0a;
  constexpr char NUL = 0x00;
  constexpr int8_t TENTHS = -1;
  const Bytes obis = OBIS_UNDER_TEST;
  const auto text = [&](const std::string &value) { return octets_entry(obis, std::nullopt, 0, value); };
  const std::vector<RenderCase> cases = {
      {"printable text with a space", text("A B"), "A B"},
      {"unit and scaler do not apply to octets", octets_entry(obis, UNIT_WATT_HOUR, TENTHS, "OK"), "OK"},
      {"text holding ( prints as hex", text("a(b"), "612862"},
      {"text holding ) prints as hex", text("a)b"), "612962"},
      {"text holding * prints as hex", text("a*b"), "612a62"},
      {"DEL prints as hex", text(std::string{'A', DEL}), "417f"},
      {"line feed prints as hex", text(std::string{'A', LINE_FEED}), "410a"},
      {"NUL prints as hex", text(std::string{'A', NUL}), "4100"},
      {"UTF-8 prints as lower-case hex", text("\xc3\xa4"), "c3a4"},
      {"tilde is the last printable character", text("~"), "~"},
  };

  // Act / Assert: per row, in run_render_cases
  run_render_cases(cases);
}

TEST(a_server_id_under_96_1_0_renders_as_the_text_meter_identification) {
  // Arrange: one row per identification value and the line it renders to
  const Bytes identification = {1, 0, 96, 1, 0, 255};
  const Bytes other_code = {1, 0, 96, 50, 1, 1};
  const std::string server_id("\x0a\x01LOG\x00\x00\xbc\x61\x4e", 10);
  const std::string block_and_number("\x0a\x01LOG\x07\xff\xff\xff\xff", 10);
  const std::string wrong_marker("\x0b\x01LOG\x00\x00\xbc\x61\x4e", 10);
  const std::string lower_case_maker("\x0a\x01log\x00\x00\xbc\x61\x4e", 10);
  const std::string too_short("\x0a\x01LOG\x00\x00\xbc\x61", 9);
  struct Row {
    const char *name;
    Bytes obis;
    std::string value;
    std::string expected_line;
  };
  const std::vector<Row> rows = {
      {"server id", identification, server_id, "1-0:96.1.0*255(001LOG0012345678)"},
      {"block in 2 digits, number in full", identification, block_and_number, "1-0:96.1.0*255(001LOG074294967295)"},
      {"another marker stays hex", identification, wrong_marker, "1-0:96.1.0*255(0b014c4f470000bc614e)"},
      {"a manufacturer not in capitals stays hex", identification, lower_case_maker,
       "1-0:96.1.0*255(0a016c6f670000bc614e)"},
      {"another length stays hex", identification, too_short, "1-0:96.1.0*255(0a014c4f470000bc61)"},
      {"another OBIS code stays hex", other_code, server_id, "1-0:96.50.1*1(0a014c4f470000bc614e)"},
  };

  for (const Row &row : rows) {
    case_label = row.name;
    // Arrange: this row's entry
    const SmlEntry entry = octets_entry(row.obis, std::nullopt, 0, row.value);

    // Act
    std::string line;
    const bool rendered = obis_line(entry, line);

    // Assert
    EXPECT_EQ(rendered, true);
    EXPECT_EQ(line, row.expected_line);
  }
}

}  // namespace
