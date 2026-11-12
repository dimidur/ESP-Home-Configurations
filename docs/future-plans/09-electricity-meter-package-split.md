# Plan 09: Split the electricity-meter package

**Status:** documented 2026-11-11 as a follow-up to the SML support; no
code yet. Value first: the SML support ships before this.
**Scope:** `packages/device-configs/electricity-meter.yaml`,
`components/electricity_meter_reader/`, `tests/electricity_meter_reader/`
**Owner:** Dimitri

## TL;DR

The package is past 1,500 lines and holds unrelated concerns. Two moves
shrink it without changing a single Home Assistant entity:

1. Move the ESP8266 heap sweep into its own package.
2. Move the line parsing, validation and dispatch C++ out of the `on_value`
   lambda into host-tested C++ in the component.

## 1. Heap sweep into its own package

The `memory_sweep_hack` script, its `interval:` trigger and the
`heap_sweep_*` substitutions have nothing to do with meters: they work
around ESP8266 heap fragmentation. A package such as
`packages/esp8266-heap-sweep.yaml` lets any ESP8266 device include it, and
the meter package stops carrying it.

Open question: whether the sweep helps at all. It frees each block right
after allocating it, which may not compact the heap. Logging fragmentation
with the script on and off settles it before the move.

When moving it, guard `ESP.getMaxFreeBlockSize() - BLOCK_SAFETY_MARGIN`
against underflow: a largest block under the margin wraps the subtraction.

## 2. Filtering logic into host-tested C++

The `on_value` lambda (about 750 lines) parses each OBIS line, applies the
noise filtering and dispatches to the sensors. The filtering is the
package's main feature, yet it can only run on a device: no host test
covers it.

- Parsing: OBIS code and value extraction, number parsing.
- Validation: range, monotonic, spike and repeated-reading confirmation,
  each a pure function of the reading, the previous value and the limits.
- Line shape: the `obis_raw_line` `filters:` lambda (minimum length, first
  character, required `*`, `(` and `)`) is validation too and moves with
  the rest.
- Dispatch: OBIS code to sensor, enable flag and validator; today a
  26-case switch. The power and current arms also publish the positioning
  mode raw sensors, the same block four times, so the dispatch table needs
  an optional raw sensor per code.

These become component files with tests beside the existing ones. The YAML
keeps entities, substitutions and wiring. Decide whether the validation
state (`prev_*`, `*_last_hash`, `*_count` globals) moves into the component
too, so that the reset button becomes one call instead of a list.

**Acceptance:** entity names unchanged; no lambda in the package over about
30 lines; the line-shape filter and the validation covered by
`tests/electricity_meter_reader/run.sh`.

## 3. Optional: split the SML decoder

`components/electricity_meter_reader/sml_decoder.cpp` holds two layers that
change for different reasons: transport (framing, escaping, CRC) and
message decoding (type-length fields, GetList). At under 400 lines it is
manageable as one file; split it into `sml_transport` and `sml_message`
if either layer grows.
