#!/usr/bin/env bash
# PBC-gAPI — Copyright Pablo B. C.
set -euo pipefail
cd "$(dirname "$0")"
mkdir -p build
"${CXX:-g++}" -std=c++11 -O1 -g -Wall -Wextra -Werror -pedantic \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  -I ../PBC_gAPI_ESP32/src test_renderer.cpp -o build/test_renderer
cd build
./test_renderer
