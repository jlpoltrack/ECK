#!/usr/bin/env bash
# Build one HHKB variant into build/<variant>/. Usage: ./build.sh hybrid|classic|type_s|hybrid_jis|type_s_jis|r3s [-c] [-d EXTRA_DEFS]
# -c cleans first; -d adds bench options, comma-separated (docs/build.md).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")" && pwd)"
usage() { echo "Usage: $0 hybrid|classic|type_s|hybrid_jis|type_s_jis|r3s [-c] [-d DEF1,DEF2,...]" >&2; exit 2; }
[[ $# -ge 1 ]] || usage
VARIANT="$1"; shift
case "$VARIANT" in hybrid|classic|type_s|hybrid_jis|type_s_jis|r3s) ;; *) usage ;; esac
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

if [[ "$CLEAN" == true ]]; then rm -rf "$BUILD_DIR" "$ROOT/nrf52/_build" "$ROOT/nrf52/_build_jis"; fi

# nRF5 SDK: $NRF5_SDK_ROOT, else Projects/SDKs (owner's layout), else ../ (CI)
SDK="${NRF5_SDK_ROOT:-}"
for d in "$ROOT/../../SDKs/nRF5_SDK_17.1.1" "$ROOT/../nRF5_SDK_17.1.1"; do
    if [[ -z "$SDK" && -d "$d" ]]; then SDK="$(cd "$d" && pwd)"; fi
done
SDK_ARGS=()

# The Hybrid .hfb packs the nRF52 app; build it first when the nRF5 SDK is present.
if [[ "$VARIANT" == hybrid* ]]; then
    if [[ -n "$SDK" && -d "$SDK" ]]; then
        NRF_JIS=0; [[ "$VARIANT" == hybrid_jis ]] && NRF_JIS=1 # JIS: country code and product ID (nrf52/_build_jis)
        make --no-print-directory -C "$ROOT/nrf52" -j"$JOBS" SDK_ROOT="$SDK" JIS=$NRF_JIS # GNU make 4 would leak "Entering directory" into the SDK link line
        SDK_ARGS=("-DHHKB_HYBRID_SOFTDEVICE=$SDK/components/softdevice/s132/hex/s132_nrf52_7.2.0_softdevice.hex")
    else
        echo "nRF5 SDK 17.1.1 not found (NRF5_SDK_ROOT, ../../SDKs/ or ../): building the STM32 .bin only" >&2
    fi
fi

cmake -G "Unix Makefiles" -S "$ROOT/stm32" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=MinSizeRel \
    "-DHHKB_VARIANT=$VARIANT" "-DHHKB_EXTRA_DEFINES=$EXTRA" ${SDK_ARGS[@]+"${SDK_ARGS[@]}"}
cmake --build "$BUILD_DIR" -j"$JOBS"
ln -sfn "$VARIANT/compile_commands.json" "$ROOT/build/compile_commands.json" # for clangd and IDEs
ls -l "$BUILD_DIR"/hhkb_ec_"$VARIANT".{bin,hfb,rfb} 2>/dev/null || true
