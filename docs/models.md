[ECK docs](overview.md)

# Supported models

| Keyboard | Model numbers | Container ID | Build | Status |
| --- | --- | --- | --- | --- |
| HHKB Professional HYBRID and HYBRID Type-S | `PD-KB800` + `B`/`W`/`Y`, optional `N`, optional `S`, `SC`, `SC1`… | `AHUX01` | `./build.sh hybrid` | **Tested** on `PD-KB800BS` (USB and Bluetooth) |
| HHKB Professional Classic | `PD-KB401` + `B`/`W`, optional `N` | `AHHX01` | `./build.sh classic` | Builds; untested on hardware |
| HHKB Professional Classic Type-S | `PD-KB401` + `B`/`W`/`Y`, optional `N`, `SC`, `SC1`… | `AHHS01` | `./build.sh type_s` | Builds; untested on hardware |
| HHKB Professional Classic Type-S JIS | `PD-KB421` + `B`/`W`/`Y`, optional `N`, `SC`, `SC1`… | `AHKS01` | `./build.sh type_s_jis` | **Experimental:** builds; untested on hardware ([JIS](#jis)) |
| HHKB Professional HYBRID (Type-S) JIS | `PD-KB820` + `B`/`W`/`Y`, optional `N`, optional `S`, `SC`, `SC1`… | `AHJX01` | `./build.sh hybrid_jis` | **Experimental:** builds; untested on hardware ([JIS](#jis)) |
| Topre Realforce R3S US TKL | `R3SD13` | `R3SD####` | `./build.sh r3s` | **Tested** (USB) ([R3S hardware](r3s-hardware.md)) |

The flashers (`tools/hhkb_flasher.py`, `web/`) accept only these model numbers unless the model check is skipped. Both refuse to write a Hybrid image for the other layout (ANSI or JIS), checked from the running keyboard or, in ST's DFU loader, from the ECK image in Bank 2. An `N` suffix means blank keycaps, `S` Type-S switches, `SC`/`SC1` special editions; colour, legends and switches don't change the electronics.

- **Hybrid:** tested on the PD-KB800BS. PFU ships one firmware (`HHKB800_FW_A048.hfb`, model `AHUX01`) for every ANSI HYBRID, so they share the board; aroum/dmk reports its Hybrid build running across the family (2026-11-22).
- **Classic and Type-S:** same STM32 code without the Bluetooth parts, and the same pins, matrix and dual-bank layout as the Hybrid. The Classic port comes from [aroum/dmk](https://github.com/aroum/dmk); this project has not run it on a Classic.

## JIS

Read from PFU's JIS images (`HHKB421_FW_JP_A904.hfb`, `HHKB820_FW_JP_A249.hfb`), compared with the ANSI ones, and PFU's JIS manuals (P3PC-6651 Hybrid, P3PC-7551 Classic Type-S) (2026-11-19); no JIS keyboard has run ECK yet.

- **Same board as ANSI, plus one row strobe:** 5 rows x 15 columns, the same column muxes, and a fifth strobe on PB11 (every PFU image, ANSI included, drives PB0, PB1, PB2, PB10 and PB11). Matrix positions: `LAYOUT` under `HHKB_JIS` in `stm32/boards/hhkb/board_config.h`. The Classic JIS and Hybrid JIS matrices are identical, and both PFU JIS images use the ANSI images' EEPROM addresses.
- **Keymap:** PFU's JIS base layer (¥, ろ, 半角/全角, 無変換, 変換, かな, arrow keys). The Fn layer follows the keycaps' front legends: as ANSI, plus Fn + A, S, D, F = Volume Down, Volume Up, Mute, Eject and Fn + Up, Down, Left, Right = Shift, ◇, Delete, Ctrl. Mac mode (SW1 or Fn + Ctrl + W) turns 半角/全角, 無変換, 変換 and かな into Caps Lock, 英数 (LANG2), かな (LANG1) and right ◇ (Command), as PFU's JIS Mac table does.
- **DIP switches differ from ANSI** (PFU's JIS manuals): SW1 ON Mac mode (OFF Win; no HHK mode); SW2 ON Control = 英数 and left Fn = Ctrl (also for the Fn + Ctrl chords; the right Fn stays Fn); SW3 ON BS = Delete (Fn + it Backspace); SW4 ON arrows = Shift, ◇, Delete, Ctrl; SW5 ON left ◇ and left Alt swapped; SW6 as ANSI (power saving on the Hybrid, recovery on the Type-S).
- **USB:** product ID `0x4012` (VIA tells the layouts apart by it; definition `stm32/via/via_jis.json`, written by `tools/make_jis_via_json.py`), Vial keyboard ID `ECKHHKB` 2, HID country code 15 (Japan), as PFU's. Over Bluetooth the same: the Hybrid JIS `.hfb` carries a JIS build of the nRF52 app (`make JIS=1`, country code 15 and product ID `0x4012`), as PFU's JIS nRF52 image differs from its ANSI one only there.
- **Pictures:** [layers and chords](layers-jis.svg), [DIP switches](dip-switches-jis.svg) (`tools/gen_layer_svg.py`).
- **Scan:** 75 readings instead of 60. Measured 2026-11-21 by running the JIS diagnostics build on the ANSI Hybrid (its PB11 strobe drives no keys, so timing holds; `hybrid_update.py stm32 --force` after a backup, then `bank2.bin` and `restore-eeprom` back; the JIS build left EEPROM untouched): longest interrupts-off half 541 µs (ANSI 408), whole scan 1.12-1.16 ms. At 500 Hz no slot is missed; at 1 kHz about 506 a second are, so JIS scans at 500 Hz over USB (`BOARD_SCAN_RATE`; Bluetooth is 250 Hz on every model). The 541 µs only delays the 1 ms tick and USB interrupts, so no third unmasked break is needed. Re-measured 2026-11-23 with v0.7.2's shorter scan loop: 532 µs, whole scan 1.11-1.14 ms, still over 1 ms, so 500 Hz stays. Rest and noise with real fifth-row keys need a JIS keyboard.

## Not supported

- **HHKB Professional 2 and older:** different controller, no USB firmware update.
- **HHKB Studio:** different hardware.
