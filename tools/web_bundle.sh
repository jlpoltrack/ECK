#!/bin/zsh
# Copy firmware into web/firmware/ with a manifest, for the web flasher. Usage: tools/web_bundle.sh [version [srcdir]]
# Without srcdir it takes the local build outputs; with it, a folder of release files (GitHub Pages deploy).
# The R3S files are optional: older releases lack them, and the page then hides the R3S downloads.
set -eu
root=${0:A:h:h}
out=$root/web/firmware
version=${1:-$(git -C "$root" describe --tags --always --dirty)}
mkdir -p "$out"
if (( $# >= 2 )); then
  cp "$2/hhkb_ec_hybrid.hfb" "$2/hhkb_ec_classic.hfb" "$2/hhkb_ec_type_s.hfb" "$2/hybrid_boot.bin" "$2/via.json" "$out/"
  r3s=("$2/hhkb_ec_r3s.rfb" "$2/r3s_via.json")
  jis=("$2/hhkb_ec_hybrid_jis.hfb" "$2/hhkb_ec_type_s_jis.hfb" "$2/via_jis.json")
else
  cp "$root/build/hybrid/hhkb_ec_hybrid.hfb" "$root/build/classic/hhkb_ec_classic.hfb" \
     "$root/build/type_s/hhkb_ec_type_s.hfb" "$root/stm32/boot/_build/hybrid_boot.bin" "$root/stm32/via/via.json" "$out/"
  r3s=("$root/build/r3s/hhkb_ec_r3s.rfb" "$root/stm32/boards/r3s/via.json")
  jis=("$root/build/hybrid_jis/hhkb_ec_hybrid_jis.hfb" "$root/build/type_s_jis/hhkb_ec_type_s_jis.hfb" "$root/stm32/via/via_jis.json")
fi
# JIS (experimental) is optional too: releases before v0.7.2 lack it
jis_files=""
if [[ -f $jis[1] && -f $jis[2] && -f $jis[3] ]]; then
  cp "$jis[@]" "$out/"
  jis_files=', "hybrid_jis": "hhkb_ec_hybrid_jis.hfb", "type_s_jis": "hhkb_ec_type_s_jis.hfb", "via_jis": "via_jis.json"'
else
  rm -f "$out/hhkb_ec_hybrid_jis.hfb" "$out/hhkb_ec_type_s_jis.hfb" "$out/via_jis.json"
  echo "no JIS firmware: bundling without it"
fi
r3s_files=""
if [[ -f $r3s[1] && -f $r3s[2] ]]; then
  cp "$r3s[1]" "$out/hhkb_ec_r3s.rfb"
  cp "$r3s[2]" "$out/r3s_via.json"
  r3s_files=', "r3s": "hhkb_ec_r3s.rfb", "r3s_via": "r3s_via.json"'
else
  rm -f "$out/hhkb_ec_r3s.rfb" "$out/r3s_via.json"
  echo "no R3S firmware: bundling the HHKB files only"
fi
cat > "$out/manifest.json" <<JSON
{"version": "$version", "files": {"hybrid": "hhkb_ec_hybrid.hfb", "classic": "hhkb_ec_classic.hfb",
 "type_s": "hhkb_ec_type_s.hfb", "boot": "hybrid_boot.bin", "via": "via.json"$r3s_files$jis_files}}
JSON
echo "bundled $version into $out"
