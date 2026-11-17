#!/bin/zsh
# Copy firmware into web/firmware/ with a manifest, for the web flasher. Usage: tools/web_bundle.sh [version [srcdir]]
# Without srcdir it takes the local build outputs; with it, a folder of release files (GitHub Pages deploy).
set -eu
root=${0:A:h:h}
out=$root/web/firmware
version=${1:-$(git -C "$root" describe --tags --always --dirty)}
mkdir -p "$out"
if (( $# >= 2 )); then
  cp "$2/hhkb_ec_hybrid.hfb" "$2/hhkb_ec_classic.hfb" "$2/hhkb_ec_type_s.hfb" "$2/hybrid_boot.bin" "$2/via.json" "$out/"
else
  cp "$root/build/hybrid/hhkb_ec_hybrid.hfb" "$root/build/classic/hhkb_ec_classic.hfb" \
     "$root/build/type_s/hhkb_ec_type_s.hfb" "$root/stm32/boot/_build/hybrid_boot.bin" "$root/stm32/via/via.json" "$out/"
fi
cat > "$out/manifest.json" <<JSON
{"version": "$version", "files": {"hybrid": "hhkb_ec_hybrid.hfb", "classic": "hhkb_ec_classic.hfb",
 "type_s": "hhkb_ec_type_s.hfb", "boot": "hybrid_boot.bin", "via": "via.json"}}
JSON
echo "bundled $version into $out"
