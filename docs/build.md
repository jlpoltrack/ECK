[ECK docs](overview.md)

# Build

Run from the repository root. Output goes to `build/<variant>/`: `hhkb_ec_<variant>.elf`, `.bin` (raw Bank 2 image) and `.hfb` (PFU update container). Building does not flash.

```bash
./build.sh hybrid    # build/hybrid/hhkb_ec_hybrid.{bin,hfb}
./build.sh classic   # build/classic/hhkb_ec_classic.{bin,hfb}
./build.sh type_s    # build/type_s/hhkb_ec_type_s.{bin,hfb}
./build.sh r3s       # build/r3s/hhkb_ec_r3s.{bin,rfb} (Realforce R3S; flash with tools/r3s/r3s_backup.py flash)
```

- `-c` cleans first (including `nrf52/_build`).
- Vial is on by default; `cmake -DECK_VIAL=OFF` builds VIA-only ([Vial](vial.md)).
- `-d A,B` adds bench options: `HHKB_ADC_DIAGNOSTICS` (per-key readings over VIA for `tools/bench/key_depth.py`), `HHKB_SCAN_DIAGNOSTICS` (scan timing counters, shown by the Polling Rate Monitor), `HHKB_HYBRID_TEST_CMD` (bridge commands over SWD for `tools/bench/hybrid_cmd.py`).
- The Hybrid build first builds the nRF52 app (`nrf52/`) and packs it with Nordic's S132 v7.2.0 SoftDevice from `../nRF5_SDK_17.1.1`; without the SDK only the `.bin` is built.
- Classic and Type-S share one image; only the `.hfb` model ID and version differ.
- Builds are warning-free with `-Wall -Wextra`.
- `build/compile_commands.json` links to the last variant built, for clangd and IDEs.
- Board-specific code lives in `stm32/boards/<board>/`: `board_config.h` (name, matrix, layout, EEPROM map), `default_keymap.h`, the matrix scan driver and board-only modules. `CMakeLists.txt` picks the board (`r3s` for the Realforce R3S, `hhkb` otherwise); shared code reaches board hardware through `board.h`, `board_init.h` and `ec/matrix_scan.h`; shared settings are in `stm32/board/config.h`.
- `tools/build_hashes.sh <source> <out>` builds every variant with a fixed version string and prints the hashes, to show a refactor leaves the binaries unchanged.

Everything else (Rapid Trigger, calibration, LEDs, hold-tap timing) is a runtime VIA setting; scan rates are fixed.

## HFB container

A Classic `.hfb` is 65,570 bytes (the Hybrid's adds the nRF52 image, [Hybrid hardware](hybrid-hardware.md#hfb-container)):

| Offset | Content |
| --- | --- |
| `0x00000` | Outer CRC-16/MCRF4XX of the rest of the file, little-endian |
| `0x00002` | Inner CRC-16/MCRF4XX of the 64 KB payload |
| `0x00004` | Bank 2 image (linked at `0x08010000`), padded with `0xFF` to 65,536 bytes |
| `0x10004` | 14 × `0xFF`, model ID (6 ASCII), BCD version (4), 6 × `0xFF` |

Both CRCs: reflected polynomial `0x8408`, initial `0xFFFF`, no final XOR.

| | Classic | Classic Type-S | Hybrid |
| --- | --- | --- | --- |
| Model ID | `AHHX01` | `AHHS01` | `AHUX01` |
| ECK version | A4.50 | A8.50 | A0.48 |

`tools/make_hfb.py` sets these and the CRCs; by hand: `--model AHHX01 --version A4.50`.

## Flash and RAM usage

Bank 2 holds 65,536 bytes; the STM32L072 has 20,480 bytes of RAM. At 2026-11-21 (with Vial, which adds ~6.3 KB and 1,544 bytes to each: [Vial](vial.md#size)):

| Variant | Bank 2 image | RAM at link time |
| --- | --- | --- |
| Hybrid | 49,756 bytes (75.9 %) | 15,672 bytes (76.5 %) |
| Classic, Type-S | 41,304 bytes (63.0 %) | 14,704 bytes (71.8 %) |
| Realforce R3S | 42,260 bytes (64.5 %) | 17,464 bytes (85.3 %) |

RAM is the static layout (RAM code, `.data`, `.bss` with all task stacks; FreeRTOS has no heap) plus the 1 KB reserved for the interrupt stack (static worst case ~720 B, AGENTS.md). That stack grows down from the top of RAM into everything unused (3 KB+ more); task stacks are fixed arrays in `.bss`, sized from `tools/stack_depth.py` worst cases.

## CI and releases

[CI](../.github/workflows/ci.yml) runs on every push and pull request with the Arm GNU Toolchain 15.3.Rel2 (SHA-256 pinned, cached), the same compiler as bench builds: host tests, then clean builds of all three variants (fetching the nRF5 SDK, SHA-256 checked, so the Hybrid `.hfb` is built) and the boot stub (`make -C stm32/boot`). Each run uploads the Hybrid images as an artifact.

A `v*` tag also publishes a GitHub release with the three `.hfb` files, the R3S `.rfb` and `r3s_via.json`, `hybrid_boot.bin` and `eck-web-flasher-<tag>.zip` (the web flasher with those images, built by `tools/web_bundle.sh`; serve it with e.g. `python3 -m http.server`). `.github/workflows/pages.yml` then publishes the web flasher with that release's images to <https://jlpoltrack.github.io/ECK/>; run it from the Actions tab to republish.
