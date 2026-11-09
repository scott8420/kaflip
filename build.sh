#!/usr/bin/env bash
# Ka-flip build.
# Deps (Fedora): gtkmm4.0-devel spdlog-devel webkitgtk6.0-devel zlib-ng-compat-devel cmake gcc-c++ ninja-build
# Deps (Ubuntu): libgtkmm-4.0-dev libspdlog-dev libwebkitgtk-6.0-dev zlib1g-dev cmake g++ ninja-build
#   (+ librsvg2-common at run time, or every symbolic SVG icon shows as missing)
set -e
cd "$(dirname "${BASH_SOURCE[0]}")"
GEN=()
if [[ ! -f build/CMakeCache.txt ]] && command -v ninja >/dev/null; then GEN=(-G Ninja); fi
cmake -B build "${GEN[@]}"
cmake --build build -j"$(nproc)"
echo ""
echo "Built: ./build/kaflip  and  ./build/kaflip_selftest"
