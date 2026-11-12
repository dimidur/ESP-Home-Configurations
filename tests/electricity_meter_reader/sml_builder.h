#pragma once

// Builds SML telegrams for the tests, written from the specification rather
// than from the decoder under test.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

#include "harness.h"
#include "logarex_telegram.h"

namespace meter_test {

using Bytes = std::vector<uint8_t>;
using Lines = std::vector<std::string>;

inline constexpr uint8_t ESCAPE_BYTE = 0x1b;
// Escapes, padding and the start and end sequences work in 4-byte blocks.
inline constexpr size_t BLOCK_LEN = 4;
inline constexpr uint8_t START_MARK = 0x01;
inline constexpr uint8_t END_MARK = 0x1a;
inline constexpr size_t START_LEN = 2 * BLOCK_LEN;
inline constexpr size_t END_LEN = 2 * BLOCK_LEN;
inline constexpr size_t CRC_LEN = 2;
inline constexpr uint8_t PADDING_BYTE = 0x00;
inline constexpr uint8_t MAX_PADDING = BLOCK_LEN - 1;
inline constexpr uint8_t END_OF_MESSAGE = 0x00;

inline constexpr uint8_t TL_OCTETS = 0x00;
inline constexpr uint8_t TL_BOOL = 0x40;
inline constexpr uint8_t TL_INT = 0x50;
inline constexpr uint8_t TL_UINT = 0x60;
inline constexpr uint8_t TL_LIST = 0x70;
inline constexpr uint8_t TL_MORE = 0x80;
inline constexpr uint8_t TL_LENGTH_MAX = 0x0f;
// An empty octet string marks an optional field as left out.
inline constexpr uint8_t ABSENT = TL_OCTETS | 1;

inline constexpr size_t MESSAGE_FIELDS = 6;
inline constexpr size_t GET_LIST_FIELDS = 7;
inline constexpr uint64_t TAG_PUBLIC_OPEN_RES = 0x0101;
inline constexpr uint64_t TAG_GET_LIST_RES = 0x0701;
inline constexpr size_t TAG_BYTES = 2;
inline constexpr size_t CRC16_BYTES = 2;
inline constexpr size_t BYTE_FIELD = 1;
inline constexpr uint64_t GROUP_NO = 0;
inline constexpr uint64_t ABORT_ON_ERROR = 0;

inline constexpr uint16_t CRC_X25_INIT = 0xffff;
inline constexpr uint16_t CRC_X25_POLY_REFLECTED = 0x8408;
inline constexpr uint16_t CRC_X25_XOROUT = 0xffff;
inline constexpr unsigned BITS_PER_BYTE = 8;
inline constexpr uint8_t BYTE_MASK = 0xff;

// DLMS unit codes.
inline constexpr uint8_t UNIT_DEGREE = 8;
inline constexpr uint8_t UNIT_WATT = 27;
inline constexpr uint8_t UNIT_WATT_HOUR = 30;
inline constexpr uint8_t UNIT_AMPERE = 33;
inline constexpr uint8_t UNIT_VOLT = 35;
inline constexpr uint8_t UNIT_HERTZ = 44;
inline constexpr uint8_t UNIT_UNKNOWN = 255;

inline void append(Bytes &to, const Bytes &bytes) { to.insert(to.end(), bytes.begin(), bytes.end()); }

inline Bytes join(std::initializer_list<Bytes> parts) {
  Bytes out;
  for (const Bytes &part : parts) append(out, part);
  return out;
}

inline Bytes repeat(uint8_t byte, size_t count) { return Bytes(count, byte); }
inline Bytes escape_block() { return repeat(ESCAPE_BYTE, BLOCK_LEN); }
inline Bytes start_sequence() { return join({escape_block(), repeat(START_MARK, BLOCK_LEN)}); }
inline Bytes ascii(const std::string &text) { return Bytes(text.begin(), text.end()); }
inline Bytes slice(const Bytes &bytes, size_t from, size_t to) {
  return Bytes(bytes.begin() + (std::ptrdiff_t) from, bytes.begin() + (std::ptrdiff_t) to);
}

inline uint8_t short_tl(uint8_t type, size_t length) {
  builder_check(length <= TL_LENGTH_MAX, "short type-length field too long");
  return (uint8_t) (type | length);
}

inline Bytes big_endian(uint64_t value, size_t width) {
  Bytes out(width);
  for (size_t i = 0; i < width; i++) out[width - 1 - i] = (uint8_t) (value >> (i * BITS_PER_BYTE));
  return out;
}

inline Bytes absent() { return {ABSENT}; }
inline Bytes octets(const Bytes &data) { return join({{short_tl(TL_OCTETS, data.size() + 1)}, data}); }
inline Bytes text_octets(const std::string &text) { return octets(ascii(text)); }
inline Bytes unsigned_int(uint64_t value, size_t width) {
  return join({{short_tl(TL_UINT, width + 1)}, big_endian(value, width)});
}
inline Bytes signed_int(int64_t value, size_t width) {
  return join({{short_tl(TL_INT, width + 1)}, big_endian((uint64_t) value, width)});
}
inline Bytes scaler(int64_t value) { return signed_int(value, BYTE_FIELD); }
inline Bytes unit(uint64_t code) { return unsigned_int(code, BYTE_FIELD); }
inline Bytes boolean(uint8_t raw) { return {short_tl(TL_BOOL, BYTE_FIELD + 1), raw}; }

inline Bytes list_of(const std::vector<Bytes> &items) {
  Bytes out{short_tl(TL_LIST, items.size())};
  for (const Bytes &item : items) append(out, item);
  return out;
}

inline uint16_t crc_x25(const uint8_t *data, size_t length) {
  uint16_t crc = CRC_X25_INIT;
  for (size_t i = 0; i < length; i++) {
    crc ^= data[i];
    for (unsigned bit = 0; bit < BITS_PER_BYTE; bit++) {
      crc = (crc & 1) ? (uint16_t) ((crc >> 1) ^ CRC_X25_POLY_REFLECTED) : (uint16_t) (crc >> 1);
    }
  }
  return crc ^ CRC_X25_XOROUT;
}

// CRC-16/X25 appended low byte first, as SML sends it.
inline void append_crc(Bytes &bytes) {
  const uint16_t crc = crc_x25(bytes.data(), bytes.size());
  bytes.push_back((uint8_t) (crc & BYTE_MASK));
  bytes.push_back((uint8_t) (crc >> BITS_PER_BYTE));
}

inline constexpr uint8_t TRANSACTION_ID_BYTE = 0x11;
inline const Bytes TRANSACTION_ID = {TRANSACTION_ID_BYTE};
inline const Bytes SERVER_ID = {0x0a, 0x01, 'T', 'S', 'T', 0x00, 0x00, 0x00, 0x00, 0x2a};

// SML_Message: transactionId, groupNo, abortOnError, body, crc16, end. The
// transactionId comes already encoded, so tests can vary its type-length.
inline Bytes message_with_id_field(const Bytes &transaction_id_field, const Bytes &body) {
  Bytes out{short_tl(TL_LIST, MESSAGE_FIELDS)};
  append(out, transaction_id_field);
  append(out, unsigned_int(GROUP_NO, BYTE_FIELD));
  append(out, unsigned_int(ABORT_ON_ERROR, BYTE_FIELD));
  append(out, body);
  out.push_back(short_tl(TL_UINT, CRC16_BYTES + 1));
  append_crc(out);
  out.push_back(END_OF_MESSAGE);
  return out;
}

inline Bytes message(const Bytes &transaction_id, const Bytes &body) {
  return message_with_id_field(octets(transaction_id), body);
}

inline Bytes body(uint64_t tag, const Bytes &content) { return list_of({unsigned_int(tag, TAG_BYTES), content}); }

inline Bytes entry(const Bytes &obis, const Bytes &unit_field, const Bytes &scaler_field, const Bytes &value) {
  return list_of({octets(obis), absent(), absent(), unit_field, scaler_field, value, absent()});
}

inline Bytes get_list_res(const std::vector<Bytes> &entries) {
  return list_of({absent(), octets(SERVER_ID), absent(), absent(), list_of(entries), absent(), absent()});
}

inline Bytes get_list_message(const std::vector<Bytes> &entries, const Bytes &transaction_id = TRANSACTION_ID) {
  return message(transaction_id, body(TAG_GET_LIST_RES, get_list_res(entries)));
}

// A complete telegram: start sequence, payload with every aligned
// 1b 1b 1b 1b block sent twice, padding to a block boundary, end sequence
// with the padding count (or padding_count when given) and the CRC.
inline Bytes frame(Bytes payload, std::optional<uint8_t> padding_count = std::nullopt) {
  uint8_t padding = 0;
  while (payload.size() % BLOCK_LEN != 0) {
    payload.push_back(PADDING_BYTE);
    padding++;
  }
  Bytes telegram = start_sequence();
  const Bytes escape = escape_block();
  for (size_t at = 0; at < payload.size(); at += BLOCK_LEN) {
    const Bytes block = slice(payload, at, at + BLOCK_LEN);
    append(telegram, block);
    if (block == escape) append(telegram, block);
  }
  append(telegram, escape);
  telegram.push_back(END_MARK);
  telegram.push_back(padding_count.value_or(padding));
  append_crc(telegram);
  return telegram;
}

inline size_t find(const Bytes &haystack, const Bytes &needle) {
  for (size_t at = 0; at + needle.size() <= haystack.size(); at++) {
    if (std::equal(needle.begin(), needle.end(), haystack.begin() + (std::ptrdiff_t) at)) return at;
  }
  return haystack.size();
}

inline bool has_aligned(const Bytes &haystack, const Bytes &needle) {
  for (size_t at = 0; at + needle.size() <= haystack.size(); at += BLOCK_LEN) {
    if (std::equal(needle.begin(), needle.end(), haystack.begin() + (std::ptrdiff_t) at)) return true;
  }
  return false;
}

// Entries used across the module tests.
inline const Bytes OBIS_UNDER_TEST = {1, 0, 2, 7, 0, 255};
inline const std::string OBIS_UNDER_TEST_TEXT = "1-0:2.7.0*255";
// Follows the entry under test so a left-out entry still shows the
// telegram decoded.
inline const Bytes OBIS_SENTINEL = {1, 0, 16, 7, 0, 255};
inline constexpr uint64_t SENTINEL_WATTS = 1134;
inline constexpr size_t SENTINEL_VALUE_BYTES = 2;
inline const std::string SENTINEL_LINE = "1-0:16.7.0*255(1134*W)";

inline Bytes sentinel_entry() {
  return entry(OBIS_SENTINEL, unit(UNIT_WATT), scaler(0), unsigned_int(SENTINEL_WATTS, SENTINEL_VALUE_BYTES));
}

// One GetList.Res whose octet-string value starts at the given offset
// within a 4-byte block of the telegram. The payload starts on a block
// boundary, so payload and telegram alignment agree.
inline Bytes payload_with_octet_value_at(const Bytes &value, size_t offset_in_block) {
  for (size_t id_len = 1; id_len <= BLOCK_LEN; id_len++) {
    const Bytes payload = get_list_message({entry(OBIS_UNDER_TEST, absent(), absent(), octets(value))},
                                           repeat(TRANSACTION_ID_BYTE, id_len));
    if (find(payload, value) % BLOCK_LEN == offset_in_block) return payload;
  }
  builder_check(false, "no transaction id length gives the requested alignment");
  return {};
}

inline constexpr size_t FIXTURE_LINE_COUNT = 22;
static_assert(std::size(meter_test::LOGAREX_LINES) == FIXTURE_LINE_COUNT, "fixture lines");

inline Bytes fixture() {
  return Bytes(std::begin(meter_test::LOGAREX_TELEGRAM),
               std::end(meter_test::LOGAREX_TELEGRAM));
}

inline Lines fixture_lines() {
  return Lines(std::begin(meter_test::LOGAREX_LINES),
               std::end(meter_test::LOGAREX_LINES));
}

// SmlEntry values as the decoder produces them. An absent unit is stored
// as code 0; an absent scaler as 0.
inline SmlEntry base_entry(const Bytes &obis, std::optional<uint8_t> unit_code, int8_t scaler_value) {
  builder_check(obis.size() == esphome::electricity_meter_reader::OBIS_CODE_LEN, "OBIS code must have six bytes");
  SmlEntry out{};
  std::copy(obis.begin(), obis.end(), out.obis);
  out.has_unit = unit_code.has_value();
  out.unit = unit_code.value_or(0);
  out.scaler = scaler_value;
  return out;
}

inline SmlEntry number_entry(const Bytes &obis, std::optional<uint8_t> unit_code, int8_t scaler_value,
                             bool is_signed, uint64_t raw) {
  SmlEntry out = base_entry(obis, unit_code, scaler_value);
  out.is_signed = is_signed;
  out.raw = raw;
  return out;
}

inline SmlEntry octets_entry(const Bytes &obis, std::optional<uint8_t> unit_code, int8_t scaler_value,
                             const std::string &value) {
  SmlEntry out = base_entry(obis, unit_code, scaler_value);
  out.is_octets = true;
  out.octets = value;
  return out;
}

inline SmlEntry sentinel_decoded() { return number_entry(OBIS_SENTINEL, UNIT_WATT, 0, false, SENTINEL_WATTS); }

// Entries compare through their full description, which names every field.
inline std::vector<std::string> describe_each(const std::vector<SmlEntry> &entries) {
  std::vector<std::string> out;
  for (const SmlEntry &sml_entry : entries) out.push_back(describe(sml_entry));
  return out;
}

}  // namespace meter_test
