#pragma once

// SML (Smart Message Language) transport and message decoding: framing,
// escaping and CRC of a telegram, and the value list of its GetList
// responses as plain entries. Knows nothing about how the entries are
// rendered or published.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace esphome {
namespace electricity_meter_reader {

// Length of the start sequence 1b 1b 1b 1b 01 01 01 01.
inline constexpr size_t SML_START_LEN = 8;
inline constexpr size_t OBIS_CODE_LEN = 6;

// One SML_ListEntry: its OBIS code, optional unit and scaler, and a value
// that is either a number or an octet string.
struct SmlEntry {
  uint8_t obis[OBIS_CODE_LEN];
  bool has_unit;
  uint8_t unit;  // DLMS unit code
  int8_t scaler;  // value is raw * 10^scaler
  bool is_octets;
  bool is_signed;
  uint64_t raw;  // number, two's complement when signed
  std::string octets;
};

// True when the bytes are the start sequence 1b 1b 1b 1b 01 01 01 01.
bool sml_is_start(const uint8_t (&bytes)[SML_START_LEN]);

// True when telegram (collected from its start sequence) now ends with a
// start sequence, unless its escape is provably payload: on a 4-byte
// boundary and the second half of an escaped 1b 1b 1b 1b. An unaligned
// start counts, so a reader resynchronises after lost or extra bytes.
bool sml_ends_with_start(const std::vector<uint8_t> &telegram);

// True when telegram now ends with a complete end sequence, its escape
// lying on a 4-byte boundary and not being the second half of an escaped
// 1b 1b 1b 1b.
bool sml_is_complete(const std::vector<uint8_t> &telegram);

enum class SmlTelegramState { COLLECTING, RESTART, COMPLETE, OVERFLOW };

// What the newest byte made of the telegram being collected: a new start
// sequence (keep its last SML_START_LEN bytes), a complete telegram, too
// long to be one (max_length bytes), or still collecting.
SmlTelegramState sml_classify_telegram(const std::vector<uint8_t> &telegram, size_t max_length);

// CRC-16/X25 over everything but the last two bytes, sent low byte first.
bool sml_crc_ok(const std::vector<uint8_t> &telegram);

// Appends the entries of every GetList response in a complete, CRC-checked
// telegram. Entries without a 6-byte OBIS code, without a value, with a
// list as value, with a unit outside Unsigned8 or with a scaler outside
// Integer8 are left out. Undoes the
// payload escaping in place, so telegram is consumed. Returns false on a
// malformed telegram; entries is then incomplete and must be discarded.
bool sml_decode_entries(std::vector<uint8_t> &telegram, std::vector<SmlEntry> &entries);

}  // namespace electricity_meter_reader
}  // namespace esphome
