#!/usr/bin/env bash
# Configure and build the native game and diagnostic runtime on Linux.
set -euo pipefail
repo=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
rt="$repo/runtime"
pr="$rt/PS2Recomp"
game="$repo/recomp/retail"
build="$rt/build_dev"

[[ -f "$pr/ps2xRuntime/CMakeLists.txt" ]] || { echo "Run tools/setup_runtime.sh first." >&2; exit 2; }
[[ -f "$game/register_functions.cpp" ]] || { echo "Missing generated game code; run tools/setup_runtime.sh with your ISO." >&2; exit 2; }
for tool in cmake ninja pkg-config; do command -v "$tool" >/dev/null || { echo "Missing prerequisite: $tool" >&2; exit 2; }; done
pkg-config --exists libavcodec libavformat libavutil libswresample libswscale || {
    echo "FFmpeg development libraries are required (libavcodec, libavformat, libavutil, libswresample, libswscale)." >&2; exit 2;
}

mkdir -p "$build/wotm_gen"
version=$(git -C "$repo" describe --tags --always --dirty 2>/dev/null || echo dev)
printf '#define WOTM_VERSION "%s"\n' "$version" > "$build/wotm_gen/wotm_version.gen.h"
elf="$repo/disc/SCUS_971.97"
if [[ -f "$elf" ]]; then
    python3 "$repo/tools/vu0native.py" "$elf" "$build/wotm_gen/wotm_vu0_native.gen.inc"
fi

cmake -S "$pr" -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DWOTM_NATIVE_DIR="$repo/native" -DPS2X_RUNNER_DIR="$game" -DPS2X_FAST_LINK=ON \
    -DPS2X_ENABLE_AGRESSIVE_LOGS=OFF -DPS2X_ENABLE_IOP_RPC_TRACE=OFF \
    -DPS2X_BUILD_RECOMP=OFF -DPS2X_BUILD_ANALYZER=OFF -DPS2X_BUILD_TEST=OFF \
    -DPS2X_BUILD_STUDIO=OFF -DPS2X_ENABLE_DEBUG_UI=OFF
cmake --build "$build" --target ps2EntryRunner wotm --parallel "${JOBS:-$(nproc)}"
echo "Build complete: $build/ps2xRuntime/wotm"
