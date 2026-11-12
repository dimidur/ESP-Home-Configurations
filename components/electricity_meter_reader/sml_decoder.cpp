#include "sml_decoder.h"

#include <cstring>

namespace esphome {
namespace electricity_meter_reader {

namespace {

// Transport framing: escape, then 4 x 0x01 to start; escape, 0x1a,
// padding count and CRC to end.
constexpr uint8_t SML_ESCAPE_BYTE = 0x1b;
constexpr size_t SML_ESCAPE_LEN = 4;
constexpr uint8_t SML_START_MARK = 0x01;
constexpr uint8_t SML_END_MARK = 0x1a;
constexpr size_t SML_CRC_LEN = 2;
constexpr size_t SML_END_LEN = 8;

constexpr uint16_t CRC_POLY_REFLECTED = 0x8408;
constexpr uint16_t CRC_X25_INIT = 0xffff;
constexpr uint16_t CRC_X25_XOROUT = 0xffff;
constexpr unsigned BITS_PER_BYTE = 8;

// Type-length field: continuation bit, type in bits 6-4, length nibble.
// For lists the length counts elements; for everything else it counts
// bytes including the type-length bytes themselves.
constexpr uint8_t TL_MORE = 0x80;
constexpr uint8_t TL_TYPE_MASK = 0x70;
constexpr unsigned TL_TYPE_SHIFT = 4;
constexpr uint8_t TL_LENGTH_MASK = 0x0f;
constexpr unsigned TL_LENGTH_SHIFT = 4;
// Four type-length bytes give a 16-bit length, far beyond any telegram.
constexpr size_t MAX_TL_BYTES = 4;
constexpr uint8_t TYPE_OCTETS = 0;
constexpr uint8_t TYPE_BOOL = 4;
constexpr uint8_t TYPE_INT = 5;
constexpr uint8_t TYPE_UINT = 6;
constexpr uint8_t TYPE_LIST = 7;
// EndOfSmlMsg, also the padding before the end sequence.
constexpr uint8_t END_OF_MESSAGE = 0x00;
constexpr size_t MAX_DEPTH = 8;
constexpr size_t MAX_INT_BYTES = 8;
constexpr uint8_t SIGN_BIT = 0x80;
// The end sequence pads the telegram to a multiple of 4 bytes.
constexpr uint8_t MAX_PADDING = SML_ESCAPE_LEN - 1;

// SML_Message: transactionId, groupNo, abortOnError, messageBody, crc16,
// endOfSmlMsg. messageBody: tag, content.
constexpr size_t MESSAGE_FIELDS = 6;
constexpr size_t MESSAGE_BODY_INDEX = 3;
constexpr size_t BODY_FIELDS = 2;
constexpr int64_t TAG_GET_LIST_RES = 0x0701;
// SML_GetList.Res: clientId, serverId, listName, actSensorTime, valList,
// listSignature, actGatewayTime.
constexpr size_t GET_LIST_FIELDS = 7;
constexpr size_t VAL_LIST_INDEX = 4;
// SML_ListEntry: objName, status, valTime, unit (Unsigned8), scaler
// (Integer8), value, valueSignature.
constexpr size_t ENTRY_FIELDS = 7;
constexpr int64_t UNIT_MIN = 0;
constexpr int64_t UNIT_MAX = UINT8_MAX;
constexpr int64_t SCALER_MIN = INT8_MIN;
constexpr int64_t SCALER_MAX = INT8_MAX;
// Entries one telegram usually yields; avoids regrowing the list.
constexpr size_t EXPECTED_ENTRIES = 32;

struct Reader {
  const uint8_t *data;
  size_t size;
  size_t pos;
  size_t remaining() const { return this->size - this->pos; }
};

struct TypeLength {
  uint8_t type;
  size_t length;  // lists: element count; others: payload bytes
};

bool read_type_length(Reader &reader, TypeLength &type_length) {
  if (reader.pos >= reader.size) return false;
  const uint8_t first = reader.data[reader.pos];
  if (first == END_OF_MESSAGE) {
    type_length = {TYPE_OCTETS, 0};
    reader.pos++;
    return true;
  }
  type_length.type = (first & TL_TYPE_MASK) >> TL_TYPE_SHIFT;
  size_t length = first & TL_LENGTH_MASK;
  size_t tl_bytes = 1;
  uint8_t current = first;
  while (current & TL_MORE) {
    if (tl_bytes >= MAX_TL_BYTES || tl_bytes >= reader.remaining()) return false;
    current = reader.data[reader.pos + tl_bytes];
    // Continuation bytes carry length bits only; their type bits are 000.
    if (current & TL_TYPE_MASK) return false;
    length = (length << TL_LENGTH_SHIFT) | (current & TL_LENGTH_MASK);
    tl_bytes++;
  }
  reader.pos += tl_bytes;
  if (type_length.type == TYPE_LIST) {
    // Every element takes at least one byte.
    if (length > reader.remaining()) return false;
    type_length.length = length;
    return true;
  }
  if (length < tl_bytes) return false;
  type_length.length = length - tl_bytes;
  return type_length.length <= reader.remaining();
}

bool skip(Reader &reader, size_t depth = 0) {
  if (depth > MAX_DEPTH) return false;
  TypeLength type_length;
  if (!read_type_length(reader, type_length)) return false;
  if (type_length.type == TYPE_LIST) {
    for (size_t i = 0; i < type_length.length; i++) {
      if (!skip(reader, depth + 1)) return false;
    }
    return true;
  }
  reader.pos += type_length.length;
  return true;
}

bool read_list(Reader &reader, size_t expected_fields) {
  TypeLength type_length;
  return read_type_length(reader, type_length) && type_length.type == TYPE_LIST &&
         type_length.length == expected_fields;
}

struct Number {
  bool present = false;
  bool is_signed = false;
  uint64_t raw = 0;  // two's complement when signed
};

// An absent optional field is an empty octet string.
bool read_number(Reader &reader, Number &number) {
  TypeLength type_length;
  if (!read_type_length(reader, type_length)) return false;
  number = Number{};
  if (type_length.type == TYPE_OCTETS && type_length.length == 0) return true;
  if (type_length.type != TYPE_INT && type_length.type != TYPE_UINT && type_length.type != TYPE_BOOL) return false;
  if (type_length.length == 0 || type_length.length > MAX_INT_BYTES) return false;
  for (size_t i = 0; i < type_length.length; i++) {
    number.raw = (number.raw << BITS_PER_BYTE) | reader.data[reader.pos + i];
  }
  number.is_signed = type_length.type == TYPE_INT;
  if (number.is_signed && (reader.data[reader.pos] & SIGN_BIT) && type_length.length < MAX_INT_BYTES) {
    number.raw |= ~uint64_t(0) << (type_length.length * BITS_PER_BYTE);
  }
  if (type_length.type == TYPE_BOOL) number.raw = number.raw != 0;
  number.present = true;
  reader.pos += type_length.length;
  return true;
}

// Small integer fields (tags, units, scalers); fails on values that do not
// fit an int64_t.
bool read_integer(Reader &reader, int64_t &value, bool &present) {
  Number number;
  if (!read_number(reader, number)) return false;
  present = number.present;
  if (!number.is_signed && number.raw > (uint64_t) INT64_MAX) return false;
  value = (int64_t) number.raw;
  return true;
}

// Reads an entry's value field into entry. Returns false on a malformed
// telegram; clears usable when the value cannot be published.
bool read_entry_value(Reader &reader, SmlEntry &entry, bool &usable) {
  if (reader.pos >= reader.size) return false;
  const uint8_t value_type = (reader.data[reader.pos] & TL_TYPE_MASK) >> TL_TYPE_SHIFT;
  switch (value_type) {
    case TYPE_LIST: {
      usable = false;
      return skip(reader);
    }
    case TYPE_OCTETS: {
      TypeLength octets;
      if (!read_type_length(reader, octets)) return false;
      usable = usable && octets.length > 0;
      entry.is_octets = true;
      entry.octets.assign(reinterpret_cast<const char *>(reader.data + reader.pos), octets.length);
      reader.pos += octets.length;
      return true;
    }
    default: {
      Number value;
      if (!read_number(reader, value)) return false;
      usable = usable && value.present;
      entry.is_signed = value.is_signed;
      entry.raw = value.raw;
      return true;
    }
  }
}

bool decode_entry(Reader &reader, std::vector<SmlEntry> &entries) {
  if (!read_list(reader, ENTRY_FIELDS)) return false;
  SmlEntry entry{};
  TypeLength name;
  if (!read_type_length(reader, name) || name.type != TYPE_OCTETS) return false;
  bool usable = name.length == OBIS_CODE_LEN;
  if (usable) memcpy(entry.obis, reader.data + reader.pos, OBIS_CODE_LEN);
  reader.pos += name.length;
  // status, valTime
  if (!skip(reader) || !skip(reader)) return false;
  int64_t unit = 0;
  int64_t scaler = 0;
  bool has_scaler = false;
  if (!read_integer(reader, unit, entry.has_unit) || !read_integer(reader, scaler, has_scaler)) return false;
  // An entry outside the field types is left out; the rest of the
  // telegram still counts.
  if (entry.has_unit && (unit < UNIT_MIN || unit > UNIT_MAX)) usable = false;
  if (scaler < SCALER_MIN || scaler > SCALER_MAX) usable = false;
  entry.unit = (uint8_t) unit;
  entry.scaler = (int8_t) scaler;

  if (!read_entry_value(reader, entry, usable)) return false;
  // valueSignature
  if (!skip(reader)) return false;
  if (usable) entries.push_back(std::move(entry));
  return true;
}

bool decode_get_list_res(Reader &reader, std::vector<SmlEntry> &entries) {
  if (!read_list(reader, GET_LIST_FIELDS)) return false;
  for (size_t field = 0; field < GET_LIST_FIELDS; field++) {
    if (field != VAL_LIST_INDEX) {
      if (!skip(reader)) return false;
      continue;
    }
    TypeLength values;
    if (!read_type_length(reader, values) || values.type != TYPE_LIST) return false;
    for (size_t entry = 0; entry < values.length; entry++) {
      if (!decode_entry(reader, entries)) return false;
    }
  }
  return true;
}

bool decode_message(Reader &reader, std::vector<SmlEntry> &entries) {
  if (!read_list(reader, MESSAGE_FIELDS)) return false;
  for (size_t field = 0; field < MESSAGE_FIELDS; field++) {
    if (field != MESSAGE_BODY_INDEX) {
      if (!skip(reader)) return false;
      continue;
    }
    if (!read_list(reader, BODY_FIELDS)) return false;
    int64_t tag = 0;
    bool has_tag = false;
    if (!read_integer(reader, tag, has_tag) || !has_tag) return false;
    const bool body_ok = tag == TAG_GET_LIST_RES ? decode_get_list_res(reader, entries) : skip(reader);
    if (!body_ok) return false;
  }
  return true;
}

bool is_escape_at(const std::vector<uint8_t> &bytes, size_t pos) {
  if (pos + SML_ESCAPE_LEN > bytes.size()) return false;
  for (size_t i = 0; i < SML_ESCAPE_LEN; i++) {
    if (bytes[pos + i] != SML_ESCAPE_BYTE) return false;
  }
  return true;
}

// Escapes on a 4-byte boundary directly before pos, back to the start
// sequence (whose own escape does not count).
size_t preceding_escapes(const std::vector<uint8_t> &telegram, size_t pos) {
  size_t count = 0;
  for (size_t at = pos; at >= SML_START_LEN + SML_ESCAPE_LEN && is_escape_at(telegram, at - SML_ESCAPE_LEN);
       at -= SML_ESCAPE_LEN) {
    count++;
  }
  return count;
}

bool is_aligned_escape_at(const std::vector<uint8_t> &telegram, size_t pos) {
  return pos % SML_ESCAPE_LEN == 0 && is_escape_at(telegram, pos);
}

// Framing: an aligned escape after an even run of escapes. After an odd
// run it is the second half of an escaped 1b 1b 1b 1b, i.e. payload.
bool is_framing_escape_at(const std::vector<uint8_t> &telegram, size_t pos) {
  return is_aligned_escape_at(telegram, pos) && preceding_escapes(telegram, pos) % 2 == 0;
}

bool is_escaped_payload_at(const std::vector<uint8_t> &telegram, size_t pos) {
  return is_aligned_escape_at(telegram, pos) && preceding_escapes(telegram, pos) % 2 == 1;
}

// Moves the payload, with escaped 1b 1b 1b 1b (sent twice on a 4-byte
// boundary) restored, to the front of telegram; returns its length.
size_t unescape_payload(std::vector<uint8_t> &telegram) {
  const size_t end = telegram.size() - SML_END_LEN;
  size_t read = SML_START_LEN;
  size_t write = 0;
  while (read < end) {
    if (read % SML_ESCAPE_LEN == 0 && is_escape_at(telegram, read) && is_escape_at(telegram, read + SML_ESCAPE_LEN)) {
      for (size_t i = 0; i < SML_ESCAPE_LEN; i++) telegram[write++] = SML_ESCAPE_BYTE;
      read += 2 * SML_ESCAPE_LEN;
      continue;
    }
    telegram[write++] = telegram[read++];
  }
  return write;
}

}  // namespace

bool sml_is_start(const uint8_t (&bytes)[SML_START_LEN]) {
  for (size_t i = 0; i < SML_ESCAPE_LEN; i++) {
    if (bytes[i] != SML_ESCAPE_BYTE) return false;
  }
  for (size_t i = SML_ESCAPE_LEN; i < SML_START_LEN; i++) {
    if (bytes[i] != SML_START_MARK) return false;
  }
  return true;
}

bool sml_ends_with_start(const std::vector<uint8_t> &telegram) {
  const size_t length = telegram.size();
  if (length < 2 * SML_START_LEN) return false;
  const size_t escape_at = length - SML_START_LEN;
  if (!is_escape_at(telegram, escape_at)) return false;
  for (size_t i = escape_at + SML_ESCAPE_LEN; i < length; i++) {
    if (telegram[i] != SML_START_MARK) return false;
  }
  return !is_escaped_payload_at(telegram, escape_at);
}

bool sml_is_complete(const std::vector<uint8_t> &telegram) {
  const size_t length = telegram.size();
  if (length < SML_START_LEN + SML_END_LEN) return false;
  const size_t escape_at = length - SML_END_LEN;
  return telegram[escape_at + SML_ESCAPE_LEN] == SML_END_MARK && is_framing_escape_at(telegram, escape_at);
}

SmlTelegramState sml_classify_telegram(const std::vector<uint8_t> &telegram, size_t max_length) {
  if (sml_ends_with_start(telegram)) return SmlTelegramState::RESTART;
  if (sml_is_complete(telegram)) return SmlTelegramState::COMPLETE;
  if (telegram.size() >= max_length) return SmlTelegramState::OVERFLOW;
  return SmlTelegramState::COLLECTING;
}

bool sml_crc_ok(const std::vector<uint8_t> &telegram) {
  const size_t length = telegram.size();
  if (length < SML_CRC_LEN) return false;
  uint16_t crc = CRC_X25_INIT;
  for (size_t i = 0; i < length - SML_CRC_LEN; i++) {
    crc ^= telegram[i];
    for (unsigned bit = 0; bit < BITS_PER_BYTE; bit++) {
      crc = (crc & 1) ? (uint16_t) ((crc >> 1) ^ CRC_POLY_REFLECTED) : (uint16_t) (crc >> 1);
    }
  }
  crc ^= CRC_X25_XOROUT;
  const uint16_t received =
      (uint16_t) (telegram[length - SML_CRC_LEN] | (telegram[length - 1] << BITS_PER_BYTE));
  return crc == received;
}

bool sml_decode_entries(std::vector<uint8_t> &telegram, std::vector<SmlEntry> &entries) {
  const size_t length = telegram.size();
  if (length < SML_START_LEN + SML_END_LEN) return false;
  if (telegram[length - SML_CRC_LEN - 1] > MAX_PADDING) return false;

  const size_t payload_length = unescape_payload(telegram);
  telegram.resize(payload_length);
  entries.reserve(EXPECTED_ENTRIES);
  Reader reader{telegram.data(), payload_length, 0};
  while (reader.pos < reader.size) {
    if (reader.data[reader.pos] == END_OF_MESSAGE) {
      reader.pos++;
      continue;
    }
    if (!decode_message(reader, entries)) return false;
  }
  return true;
}

}  // namespace electricity_meter_reader
}  // namespace esphome
