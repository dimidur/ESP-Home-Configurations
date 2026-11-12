# UART edge-timed receive host tests

Host tests for the parts of `components/uart_edge_rx` that do not depend on
ESPHome: the edge decoder (`edge_uart_decoder`) and the hand-off into the
software UART's ring buffer (`byte_ring.h`). The ESPHome glue, which reads
ESPHome internals and attaches the interrupt, is not covered here.

## Run

```sh
tests/uart_edge_rx/run.sh
```

Needs `g++` with C++17 and the address and undefined-behaviour sanitizers,
plus `timeout` from coreutils. Set `CXX` to use another compiler.

## Layout

| File | Holds |
| --- | --- |
| `edge_uart_decoder_test.cpp` | Decoding: frame formats, baud rates, distortion, errors, flush |
| `byte_ring_test.cpp` | The ring hand-off, read as ESPHome's software UART reads it |
| `edge_builder.h` | Line edges for given bytes and frame format; a collecting receiver |

The test harness is shared with the other suites in `tests/harness/`.
