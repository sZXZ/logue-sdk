#!/bin/sh
# Build + run the host-side audio regression test for the NTS-3 DRUMS unit.
set -e
cd "$(dirname "$0")"

DRUMS_DIR=../../platform/nts-3_kaoss/drums

mkdir -p build
clang++ -std=c++17 -O2 -Wall -Wextra \
  -Istubs \
  -I"$DRUMS_DIR" \
  test_drums.cpp -o build/test_drums -lm

./build/test_drums "$@"