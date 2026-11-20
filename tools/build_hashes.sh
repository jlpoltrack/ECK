#!/bin/zsh
# Build every HHKB variant from a source tree with a fixed version string and print SHA-256 of each output.
# Usage: build_hashes.sh <source_root> <out_dir>
set -eu
src=${1:A}; out=${2:A}
mkdir -p "$out"
# name -> "variant [extra-defines]"
typeset -A variants
variants=(
  hybrid_bench "hybrid HHKB_ADC_DIAGNOSTICS,HHKB_SCAN_DIAGNOSTICS"
  hybrid       "hybrid"
  classic      "classic"
  type_s       "type_s"
  hybrid_jis   "hybrid_jis"
  type_s_jis   "type_s_jis"
)
for name in ${(ko)variants}; do
  spec=(${=variants[$name]})
  b="$out/$name"
  rm -rf "$b"
  HHKB_FW_VERSION=verify cmake -G "Unix Makefiles" -S "$src/stm32" -B "$b" -DCMAKE_BUILD_TYPE=MinSizeRel \
    "-DHHKB_VARIANT=${spec[1]}" "-DHHKB_EXTRA_DEFINES=${spec[2]:-}" > "$b.log" 2>&1
  cmake --build "$b" -j"$(sysctl -n hw.ncpu)" >> "$b.log" 2>&1 || { echo "$name: BUILD FAILED (see $b.log)"; exit 1; }
  for f in $(find "$b" -maxdepth 1 \( -name "*.bin" -o -name "*.hfb" \) | sort); do
    echo "$name/${f:t} $(shasum -a 256 "$f" | cut -c1-16)"
  done
done
echo "done"
