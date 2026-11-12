# Electricity meter reader host tests

Host tests for the parts of `components/electricity_meter_reader` that do
not depend on ESPHome: the SML decoder (`sml_decoder`), the OBIS line
rendering (`obis_line`) and the byte-stream reader (`meter_stream`). The
ESPHome glue is not covered here. The tests build with the host compiler;
no ESPHome install or device is needed.

## Run

```sh
tests/electricity_meter_reader/run.sh
```

Needs `g++` with C++17 and the address and undefined-behaviour sanitizers,
plus `timeout` from coreutils. Set `CXX` to use another compiler. The
script builds into a temporary directory and prints one `PASS` or `FAIL`
line per test, grouped by test file. It exits non-zero when a test fails,
when a sanitizer reports an error or when the run exceeds its time limit.

## Layout

| File | Holds |
| --- | --- |
| `sml_decoder_test.cpp` | Framing, CRC and decoding into entries |
| `obis_line_test.cpp` | Rendering an entry as an OBIS text line |
| `meter_stream_test.cpp` | Text lines, telegrams, resync and drops |
| `sml_builder.h` | SML telegrams built from the specification |
| `logarex_telegram.h` | A captured telegram and its expected lines |
| `harness.h`, `harness_main.cpp` | `TEST`, `EXPECT`, `REQUIRE` and the runner |

This repository is public: commit a captured telegram only after replacing
the meter identification and recomputing its CRCs, as `logarex_telegram.h`
describes.
