# Electricity Meter SML Decoding

Why `components/electricity_meter_reader` decodes SML itself and renders it
as OBIS text lines, and what was rejected on the way.

## Context

The `electricity-meter.yaml` package was written for a Logarex meter that
pushes IEC 62056-21 text at 9600 7E1, one OBIS value per line, for example
`1-0:1.8.0*255(031719.5078*kWh)`. The package's value is its noise
filtering: range, monotonic and spike checks and repeated-reading
confirmation keep Home Assistant's values stable when the IR signal is bad.
That filtering works on OBIS text lines.

Its replacement, a Logarex LK13BE606739, pushes binary SML at 9600 8N1
instead. A captured telegram is 660 bytes and carries 22 values.

## Decision

The component is a protocol adapter in front of the unchanged filtering:
it turns whatever the IR head receives into standard OBIS text lines.

- It recognises the protocol by itself. An SML start sequence starts a
  telegram; every other byte is text. One firmware reads either kind of
  meter without a setting.
- Each SML value is rendered as the line an IEC meter would send, so the
  package's filtering handles both protocols alike.

The UART runs at 8N1. A 7E1 byte read at 8N1 arrives with its parity bit
on top, so text bytes are masked to 7 bits before they join a line. The
UART can then no longer flag parity errors in text; the package's
filtering is what rejects a corrupted text line.

### Rendering

- Energy in Wh is rendered in kWh, matching what the IEC meter sent.
- Decimal values are produced exactly from the integer and its scaler, with
  no floating point, so a 64-bit counter loses no digits.
- Octet strings are printed as text when printable and free of `(`, `)`
  and `*`, which would break the line format; otherwise as hex.

### Framing and checks

- Inside the payload, `1b 1b 1b 1b` is sent twice on a 4-byte boundary.
  An escape counts as framing only when it is aligned and follows an even
  run of escapes; the payload is un-escaped before decoding.
- Only CRC-16/X25, sent low byte first, is accepted. That is what the
  LK13BE606739 sends. ESPHome's built-in component also accepts
  CRC-16/KERMIT; no meter read here needs it.
- A new start sequence inside a telegram cuts it short, aligned or not, so
  the reader resynchronises after lost bytes. The cost: a valid telegram
  whose payload happens to contain an unaligned start sequence is dropped
  every time.
- After a dropped telegram, bytes are skipped until the next start sequence
  instead of being read as text.

### Text lines

The text path splits lines the way `uart_line_reader` does, about ten
lines of code. Sharing them would mean changing `uart_line_reader`, a
generic component other devices use, and recognising the protocol needs a
single reader that sees every byte; so the rules are kept as a copy.

## Rejected

### ESPHome's built-in `sml` component

Checked against ESPHome 2026.9.1. Its parser falls short of the decoder
here:

- It indexes SML list nodes by position without checking how many the list
  has (`sml_parser.cpp`, `get_obis_info` and the `ObisInfo` constructor,
  for example `message_body.nodes[1]` and `val_list_entry.nodes[5]`). It
  checks the CRC first (`sml.cpp`, `Sml::loop`), so only a malformed
  telegram that still passes the CRC reads past the end of the vector.
- It hands the bytes between the start and end sequences to the parser as
  received (`sml.cpp`, `Sml::loop`), without undoing the payload escaping.
- It detects the end sequence at any byte position, not only on a 4-byte
  boundary (`Sml::check_start_end_bytes_`).
- Its OBIS codes have five groups (`obis_code` in `__init__.py`), so it
  cannot tell `1-0:1.8.0*96` from `1-0:1.8.0*255`.

It is also the `UARTDevice` that reads the meter, so it cannot share the
UART with a text reader, and the protocol could not be recognised
automatically.

### SML support in `uart_line_reader`

`uart_line_reader` splits any UART stream into text lines and is used by
other devices. Meter protocol knowledge does not belong in it, so the
decoder lives in a meter-specific component.

### Decoder as package-local includes

`esphome: includes:` makes functions available to lambdas, but the reader
needs its own UART loop, a configuration schema and an
`on_telegram_dropped` trigger, which only a component can register.
