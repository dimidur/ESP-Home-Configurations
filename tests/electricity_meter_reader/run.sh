#!/usr/bin/env bash
# Builds the electricity meter reader host tests with sanitizers into a
# temporary directory and runs them. Exits non-zero when a test fails.
set -euo pipefail

# A decoder stuck in a loop fails the run instead of hanging it.
readonly TIMEOUT_SECONDS=120

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
component="${here}/../../components/electricity_meter_reader"
build_dir="$(mktemp -d)"
trap 'rm -rf "${build_dir}"' EXIT

"${CXX:-g++}" -std=c++17 -Wall -Wextra -Werror -g -O1 \
  -fsanitize=address,undefined -fno-sanitize-recover=all \
  -fno-omit-frame-pointer \
  "${component}/sml_decoder.cpp" \
  "${component}/obis_line.cpp" \
  "${component}/meter_stream.cpp" \
  "${here}/../../components/uart_edge_rx/edge_uart_decoder.cpp" \
  "${here}/../harness/harness_main.cpp" \
  "${here}/edge_rx_end_to_end_test.cpp" \
  "${here}/sml_decoder_test.cpp" \
  "${here}/obis_line_test.cpp" \
  "${here}/meter_stream_test.cpp" \
  -o "${build_dir}/electricity_meter_reader_test"

timeout "${TIMEOUT_SECONDS}" "${build_dir}/electricity_meter_reader_test"
