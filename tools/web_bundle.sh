#!/bin/zsh
# Copy built firmware into web/firmware/ with a manifest, for the web flasher. Usage: tools/web_bundle.sh [version]
set -eu
root=${0:A:h:h}
out=$root/web/firmware
version=${1:-$(git -C "$root" describe --tags --always --dirty)}
mkdir -p "$out"
cp "$root/build/hybrid/hhkb_ec_hybrid.hfb" "$root/build/classic/hhkb_ec_classic.hfb" \
   "$root/build/type_s/hhkb_ec_type_s.hfb" "$root/stm32/boot/_build/hybrid_boot.bin" "$out/"
cat > "$out/manifest.json" <<JSON
{"version": "$version", "files": {"hybrid": "hhkb_ec_hybrid.hfb", "classic": "hhkb_ec_classic.hfb",
 "type_s": "hhkb_ec_type_s.hfb", "boot": "hybrid_boot.bin"}}
JSON
echo "bundled $version into $out"
