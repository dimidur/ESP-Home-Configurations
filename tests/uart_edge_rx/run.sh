#!/usr/bin/env bash
# Builds the uart_edge_rx host tests with sanitizers into a temporary
# directory and runs them. Exits non-zero when a test fails.
set -euo pipefail

# A decoder stuck in a loop fails the run instead of hanging it.
readonly TIMEOUT_SECONDS=120

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
component="${here}/../../components/uart_edge_rx"
harness="${here}/../harness"
build_dir="$(mktemp -d)"
trap 'rm -rf "${build_dir}"' EXIT

"${CXX:-g++}" -std=c++17 -Wall -Wextra -Werror -g -O1 \
  -fsanitize=address,undefined -fno-sanitize-recover=all \
  -fno-omit-frame-pointer \
  "${component}/edge_uart_decoder.cpp" \
  "${harness}/harness_main.cpp" \
  "${here}/edge_uart_decoder_test.cpp" \
  "${here}/byte_ring_test.cpp" \
  -o "${build_dir}/uart_edge_rx_test"

timeout "${TIMEOUT_SECONDS}" "${build_dir}/uart_edge_rx_test"
