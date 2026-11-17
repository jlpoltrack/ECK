#!/usr/bin/env bash
# Build one HHKB variant into build/<variant>/. Usage: ./build.sh hybrid|classic|type_s [-c] [-d EXTRA_DEFS]
# -c cleans first; -d adds bench options, comma-separated (docs/build.md).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")" && pwd)"
usage() { echo "Usage: $0 hybrid|classic|type_s [-c] [-d DEF1,DEF2,...]" >&2; exit 2; }
[[ $# -ge 1 ]] || usage
VARIANT="$1"; shift
case "$VARIANT" in hybrid|classic|type_s) ;; *) usage ;; esac
BUILD_DIR="$ROOT/build/$VARIANT"
EXTRA=""
CLEAN=false
while getopts "cd:" opt; do
    case $opt in
        c) CLEAN=true ;;
        d) EXTRA="$OPTARG" ;;
        *) usage ;;
    esac
done
JOBS=$(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)

if [[ "$CLEAN" == true ]]; then rm -rf "$BUILD_DIR" "$ROOT/nrf52/_build"; fi

# The Hybrid .hfb packs the nRF52 app; build it first when the nRF5 SDK is present.
if [[ "$VARIANT" == hybrid ]]; then
    if [[ -d "$ROOT/../nRF5_SDK_17.1.1" ]]; then
        make --no-print-directory -C "$ROOT/nrf52" -j"$JOBS" # GNU make 4 would leak "Entering directory" into the SDK link line
    else
        echo "nRF5 SDK not found at ../nRF5_SDK_17.1.1: building the STM32 .bin only" >&2
    fi
fi

cmake -G "Unix Makefiles" -S "$ROOT/stm32" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=MinSizeRel \
    "-DHHKB_VARIANT=$VARIANT" "-DHHKB_EXTRA_DEFINES=$EXTRA"
cmake --build "$BUILD_DIR" -j"$JOBS"
ln -sfn "$VARIANT/compile_commands.json" "$ROOT/build/compile_commands.json" # for clangd and IDEs
ls -l "$BUILD_DIR"/hhkb_ec_"$VARIANT".{bin,hfb} 2>/dev/null || true
