#pragma once

// Test harness for the electricity meter reader host tests: TEST registers
// a test, EXPECT and EXPECT_EQ record failed checks, REQUIRE checks a
// precondition in Arrange, and harness_main.cpp runs every registered test.
// Table-driven tests set case_label so a failure names its row.

#include <cstddef>
#include <cstdint>
#include <string>
#include <type_traits>
#include <vector>

#include "../../components/electricity_meter_reader/meter_stream.h"
#include "../../components/electricity_meter_reader/sml_decoder.h"

namespace meter_test {

using esphome::electricity_meter_reader::MeterStreamEvent;
using esphome::electricity_meter_reader::SmlEntry;
using esphome::electricity_meter_reader::SmlTelegramState;

struct TestCase {
  const char *file;
  const char *name;
  void (*body)();
};

inline std::vector<TestCase> &registry() {
  static std::vector<TestCase> tests;
  return tests;
}

struct Registrar {
  Registrar(const char *file, const char *name, void (*body)()) { registry().push_back({file, name, body}); }
};

inline std::vector<std::string> failures;
inline std::string case_label;

inline void report_failure(const char *file, int line, const std::string &message) {
  std::string where = std::string(file) + ":" + std::to_string(line) + ": ";
  if (!case_label.empty()) where += "[" + case_label + "] ";
  failures.push_back(where + message);
}

inline constexpr uint8_t PRINTABLE_FIRST = 0x20;
inline constexpr uint8_t PRINTABLE_END = 0x7f;
inline constexpr unsigned NIBBLE_BITS = 4;
inline constexpr uint8_t NIBBLE_MASK = 0x0f;
inline constexpr char HEX_DIGITS[] = "0123456789abcdef";

inline std::string hex(const std::string &bytes) {
  std::string out;
  for (const unsigned char byte : bytes) {
    out += HEX_DIGITS[byte >> NIBBLE_BITS];
    out += HEX_DIGITS[byte & NIBBLE_MASK];
  }
  return out;
}

inline std::string describe(const std::string &text) {
  std::string out = "\"";
  for (const unsigned char character : text) {
    if (character >= PRINTABLE_FIRST && character < PRINTABLE_END) {
      out += (char) character;
    } else {
      out += "\\x" + hex(std::string(1, (char) character));
    }
  }
  return out + "\"";
}

inline std::string describe(const char *text) { return text == nullptr ? "nullptr" : describe(std::string(text)); }
inline std::string describe(bool value) { return value ? "true" : "false"; }

template <typename T, std::enable_if_t<std::is_integral_v<T> && !std::is_same_v<T, bool>, int> = 0>
std::string describe(T value) {
  return std::to_string(value);
}

inline std::string describe(MeterStreamEvent event) {
  switch (event) {
    case MeterStreamEvent::NONE:
      return "NONE";
    case MeterStreamEvent::TELEGRAM:
      return "TELEGRAM";
    case MeterStreamEvent::TELEGRAM_RESTART:
      return "TELEGRAM_RESTART";
    case MeterStreamEvent::TELEGRAM_CRC:
      return "TELEGRAM_CRC";
    case MeterStreamEvent::TELEGRAM_DECODE:
      return "TELEGRAM_DECODE";
    case MeterStreamEvent::TELEGRAM_OVERFLOW:
      return "TELEGRAM_OVERFLOW";
  }
  return "unknown event";
}

inline std::string describe(SmlTelegramState state) {
  switch (state) {
    case SmlTelegramState::COLLECTING:
      return "COLLECTING";
    case SmlTelegramState::RESTART:
      return "RESTART";
    case SmlTelegramState::COMPLETE:
      return "COMPLETE";
    case SmlTelegramState::OVERFLOW:
      return "OVERFLOW";
  }
  return "unknown state";
}

// Every field, so two entries describe alike exactly when they are equal.
inline std::string describe(const SmlEntry &entry) {
  std::string out = "{obis";
  for (const uint8_t byte : entry.obis) out += " " + std::to_string(byte);
  out += entry.has_unit ? ", unit " + std::to_string(entry.unit) : ", no unit";
  out += ", scaler " + std::to_string(entry.scaler);
  if (entry.is_octets) {
    out += ", octets " + hex(entry.octets);
  } else {
    out += entry.is_signed ? ", signed " : ", unsigned ";
    out += std::to_string(entry.raw);
  }
  return out + "}";
}

template <typename T>
std::string describe(const std::vector<T> &items) {
  std::string out = "[";
  for (size_t i = 0; i < items.size(); i++) {
    if (i > 0) out += ", ";
    out += describe(items[i]);
  }
  return out + "]";
}

inline void expect_at(bool ok, const char *expression, const char *file, int line) {
  if (!ok) report_failure(file, line, std::string("expected ") + expression);
}

template <typename T>
void expect_eq_at(const T &actual, const T &expected, const char *expression, const char *file, int line) {
  if (actual == expected) return;
  report_failure(file, line,
                 std::string(expression) + "\n      actual:   " + describe(actual) +
                     "\n      expected: " + describe(expected));
}

// Thrown by REQUIRE once the failure is recorded; the runner catches it
// and moves on to the next test.
struct PreconditionFailed {};

// A precondition that does not hold leaves Act and Assert nothing to test,
// so the test fails and stops there.
inline void require_at(bool ok, const char *expression, const char *file, int line) {
  if (ok) return;
  report_failure(file, line, std::string("precondition failed, test stopped: ") + expression);
  throw PreconditionFailed{};
}

// A test that cannot build its own input is broken, not failing: the whole
// run aborts. Unlike REQUIRE, which stops one test.
void builder_check(bool ok, const char *what);

}  // namespace meter_test

#define TEST(name) \
  void name(); \
  const meter_test::Registrar name##_registrar(__FILE__, #name, name); \
  void name()

#define REQUIRE(condition) meter_test::require_at((condition), #condition, __FILE__, __LINE__)
#define EXPECT(condition) meter_test::expect_at((condition), #condition, __FILE__, __LINE__)
#define EXPECT_EQ(actual, expected) \
  meter_test::expect_eq_at<std::decay_t<decltype(actual)>>((actual), (expected), #actual " == " #expected, __FILE__, \
                                                           __LINE__)
