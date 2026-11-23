#!/bin/zsh
# Build every HHKB variant from a source tree with a fixed version string; print "sha256  path" per image, sorted.
# Usage: tools/build_hashes.sh [source_root] [out_dir]   (defaults: this repo, a temp dir). Diff two trees' output.
set -eu
src=${1:-${0:A:h:h}}; src=${src:A}
out=${2:-$(mktemp -d)}; mkdir -p "$out"; out=${out:A}
# name -> "variant [extra-defines]"
typeset -A variants
variants=(
  hybrid_bench "hybrid HHKB_ADC_DIAGNOSTICS,HHKB_SCAN_DIAGNOSTICS"
  hybrid       "hybrid"
  classic      "classic"
  type_s       "type_s"
)
for name in ${(ko)variants}; do
  spec=(${=variants[$name]})
  b="$out/$name"
  rm -rf "$b"
  HHKB_FW_VERSION=verify cmake -G "Unix Makefiles" -S "$src/stm32" -B "$b" -DCMAKE_BUILD_TYPE=MinSizeRel \
    "-DHHKB_VARIANT=${spec[1]}" "-DHHKB_EXTRA_DEFINES=${spec[2]:-}" > "$b.log" 2>&1
  cmake --build "$b" -j"$(sysctl -n hw.ncpu)" >> "$b.log" 2>&1 || { echo "$name: BUILD FAILED (see $b.log)" >&2; exit 1; }
done
(cd "$out" && shasum -a 256 */*.bin(N) */*.hfb(N) | sort -k2)
if [[ -z ${2:-} ]]; then rm -rf "$out"; fi
