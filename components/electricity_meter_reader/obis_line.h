#pragma once

// Renders an SML entry as the OBIS text line a push-mode IEC 62056-21
// meter sends, for example "1-0:1.8.0*255(9.3440*kWh)", so the electricity
// meter package parses both meter types the same way.

#include <string>

#include "sml_decoder.h"

namespace esphome {
namespace electricity_meter_reader {

// Numbers become "value*unit": the exact decimal of raw * 10^scaler, with
// energy in Wh converted to kWh. A meter identification number (96.1.0)
// holding an SML server ID becomes the text a text meter sends, e.g.
// "001LOG0012345678". Other octet strings become their text when
// printable and free of the line's delimiters "()*", else lower-case hex.
// Returns false when the entry cannot be rendered.
bool obis_line(const SmlEntry &entry, std::string &line);

}  // namespace electricity_meter_reader
}  // namespace esphome
