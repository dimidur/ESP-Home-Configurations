#include "obis_line.h"

#include <cinttypes>
#include <cstdio>

namespace esphome {
namespace electricity_meter_reader {

namespace {

// DLMS unit codes (IEC 62056-62) and their names in the text lines.
constexpr uint8_t UNIT_WATT_HOUR = 30;
struct UnitName {
  uint8_t code;
  const char *name;
};
constexpr UnitName UNIT_NAMES[] = {{8, "deg"}, {27, "W"}, {33, "A"}, {35, "V"}, {44, "Hz"}};
// Wh to kWh is a decimal shift by this many places.
constexpr int KWH_PER_WH_EXPONENT = 3;
// Entries whose decimal point moves further than this are not rendered;
// meters use a handful of places at most.
constexpr int MAX_DECIMAL_SHIFT = 9;

constexpr char HEX_DIGITS[] = "0123456789abcdef";
constexpr size_t HEX_CHARS_PER_BYTE = 2;
constexpr unsigned NIBBLE_BITS = 4;
constexpr uint8_t NIBBLE_MASK = 0x0f;
constexpr uint8_t PRINTABLE_FIRST = 0x20;
constexpr uint8_t PRINTABLE_END = 0x7f;
// Characters that delimit the value inside an OBIS text line.
constexpr char LINE_DELIMITERS[] = "()*";
// "255-255:255.255.255*255" plus terminator, with room to spare.
constexpr size_t OBIS_TEXT_CHARS = 32;

// Meter identification number, OBIS C.D.E = 96.1.0. In SML it is the
// 10-byte server ID: marker 0x0a, Sparte, three-letter manufacturer,
// fabrication block, then the fabrication number as 4 bytes big-endian.
// Text meters send it as "00", Sparte, manufacturer, block in 2 digits and
// number in 8 digits, e.g. "001LOG0012345678".
constexpr uint8_t OBIS_C_IDENTIFICATION = 96;
constexpr uint8_t OBIS_D_IDENTIFICATION = 1;
constexpr uint8_t OBIS_E_IDENTIFICATION = 0;
constexpr size_t OBIS_C = 2;
constexpr size_t OBIS_D = 3;
constexpr size_t OBIS_E = 4;
constexpr size_t SERVER_ID_LEN = 10;
constexpr uint8_t SERVER_ID_MARKER = 0x0a;
constexpr size_t SERVER_ID_SPARTE = 1;
constexpr size_t SERVER_ID_MANUFACTURER = 2;
constexpr size_t SERVER_ID_MANUFACTURER_LEN = 3;
constexpr size_t SERVER_ID_BLOCK = 5;
constexpr size_t SERVER_ID_NUMBER = 6;
constexpr size_t SERVER_ID_NUMBER_LEN = 4;
constexpr unsigned BITS_PER_BYTE = 8;
// "00", a 3-digit Sparte, 3 letters, a 3-digit block, a 10-digit number.
constexpr size_t SERVER_ID_TEXT_CHARS = 32;

bool is_line_delimiter(uint8_t byte) {
  for (const char *delimiter = LINE_DELIMITERS; *delimiter; delimiter++) {
    if (byte == (uint8_t) *delimiter) return true;
  }
  return false;
}

std::string octets_text(const std::string &bytes) {
  bool printable = !bytes.empty();
  for (const char character : bytes) {
    const uint8_t byte = (uint8_t) character;
    if (byte < PRINTABLE_FIRST || byte >= PRINTABLE_END || is_line_delimiter(byte)) printable = false;
  }
  if (printable) return bytes;
  std::string hex;
  hex.reserve(bytes.size() * HEX_CHARS_PER_BYTE);
  for (const char character : bytes) {
    const uint8_t byte = (uint8_t) character;
    hex += HEX_DIGITS[byte >> NIBBLE_BITS];
    hex += HEX_DIGITS[byte & NIBBLE_MASK];
  }
  return hex;
}

bool is_identification(const uint8_t (&code)[OBIS_CODE_LEN]) {
  return code[OBIS_C] == OBIS_C_IDENTIFICATION && code[OBIS_D] == OBIS_D_IDENTIFICATION &&
         code[OBIS_E] == OBIS_E_IDENTIFICATION;
}

// The text form of a server ID; false when the bytes are not one.
bool server_id_text(const std::string &bytes, std::string &text) {
  if (bytes.size() != SERVER_ID_LEN || (uint8_t) bytes[0] != SERVER_ID_MARKER) return false;
  for (size_t i = SERVER_ID_MANUFACTURER; i < SERVER_ID_MANUFACTURER + SERVER_ID_MANUFACTURER_LEN; i++) {
    if (bytes[i] < 'A' || bytes[i] > 'Z') return false;
  }
  uint32_t number = 0;
  for (size_t i = SERVER_ID_NUMBER; i < SERVER_ID_NUMBER + SERVER_ID_NUMBER_LEN; i++) {
    number = (number << BITS_PER_BYTE) | (uint8_t) bytes[i];
  }
  char buffer[SERVER_ID_TEXT_CHARS];
  snprintf(buffer, sizeof(buffer), "00%u%.3s%02u%08" PRIu32, (unsigned) (uint8_t) bytes[SERVER_ID_SPARTE],
           bytes.data() + SERVER_ID_MANUFACTURER, (unsigned) (uint8_t) bytes[SERVER_ID_BLOCK], number);
  text = buffer;
  return true;
}

std::string obis_text(const uint8_t (&code)[OBIS_CODE_LEN]) {
  char text[OBIS_TEXT_CHARS];
  snprintf(text, sizeof(text), "%u-%u:%u.%u.%u*%u", code[0], code[1], code[2], code[3], code[4], code[5]);
  return text;
}

std::string unit_name(uint8_t unit) {
  for (const auto &entry : UNIT_NAMES) {
    if (entry.code == unit) return entry.name;
  }
  return std::to_string(unit);
}

// The number times 10^exponent in decimal, exact for any 64-bit value: the
// integer's digits with the decimal point moved, never through floating
// point. False when the point would move further than allowed.
bool decimal_text(bool is_signed, uint64_t raw, int exponent, std::string &text) {
  if (exponent > MAX_DECIMAL_SHIFT || exponent < -MAX_DECIMAL_SHIFT) return false;
  const bool negative = is_signed && (int64_t) raw < 0;
  // Two's complement magnitude; also right for the most negative value.
  const uint64_t magnitude = negative ? uint64_t(0) - raw : raw;
  std::string digits = std::to_string(magnitude);
  if (exponent > 0) {
    digits.append((size_t) exponent, '0');
  } else if (exponent < 0) {
    const size_t decimals = (size_t) -exponent;
    if (digits.size() <= decimals) digits.insert(0, decimals + 1 - digits.size(), '0');
    digits.insert(digits.size() - decimals, 1, '.');
  }
  text = negative ? "-" + digits : digits;
  return true;
}

bool number_text(const SmlEntry &entry, std::string &text) {
  const bool energy = entry.has_unit && entry.unit == UNIT_WATT_HOUR;
  const int exponent = energy ? entry.scaler - KWH_PER_WH_EXPONENT : entry.scaler;
  std::string value;
  if (!decimal_text(entry.is_signed, entry.raw, exponent, value)) return false;
  std::string name;
  if (energy) {
    name = "kWh";
  } else if (entry.has_unit) {
    name = unit_name(entry.unit);
  }
  text = value + "*" + name;
  return true;
}

}  // namespace

bool obis_line(const SmlEntry &entry, std::string &line) {
  std::string value;
  if (entry.is_octets) {
    if (!is_identification(entry.obis) || !server_id_text(entry.octets, value)) value = octets_text(entry.octets);
  } else if (!number_text(entry, value)) {
    return false;
  }
  line = obis_text(entry.obis) + "(" + value + ")";
  return true;
}

}  // namespace electricity_meter_reader
}  // namespace esphome
