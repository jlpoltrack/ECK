[ECK docs](overview.md)

# Supported models

| Keyboard | Model numbers | Container ID | Build | Status |
| --- | --- | --- | --- | --- |
| HHKB Professional HYBRID Type-S | `PD-KB800BS` | `AHUX01` | `./build.sh hybrid` | **Tested** (USB and Bluetooth) |
| HHKB Professional Classic | `PD-KB401W`, `PD-KB401B`, `PD-KB401WN`, `PD-KB401BN` | `AHHX01` | `./build.sh classic` | Builds; untested on hardware |
| HHKB Professional Classic Type-S | `PD-KB401WSC`, `PD-KB401BSC`, `PD-KB401YSC` | `AHHS01` | `./build.sh type_s` | Builds; untested on hardware |
| HHKB Professional Classic Type-S JIS | `PD-KB421BSC`, `PD-KB421WSC`, `PD-KB421YSC` | `AHKS01` | `./build.sh type_s_jis` | Builds; untested on hardware ([JIS](#jis)) |
| HHKB Professional HYBRID (Type-S) JIS | `PD-KB820…` | `AHJX01` | `./build.sh hybrid_jis` | Builds; untested on hardware ([JIS](#jis)) |
| Topre Realforce R3S US TKL | `R3SD13` | `R3SD####` | `./build.sh r3s` | **Tested** (USB) ([R3S hardware](r3s-hardware.md)) |

The flashers accept only these model numbers unless the model check is skipped (`tools/hhkb_flasher.py` all of them; the web flasher not the JIS models yet). An `N` suffix means blank keycaps; colour and legends don't change the electronics.

- **Hybrid:** only the PD-KB800BS (PFU firmware `HHKB800_FW_A048.hfb`) has been tested. Other HYBRID models probably share the board, but that is unverified and the flashers don't accept them.
- **Classic and Type-S:** same STM32 code without the Bluetooth parts, and the same pins, matrix and dual-bank layout as the Hybrid. The Classic port comes from [aroum/dmk](https://github.com/aroum/dmk); this project has not run it on a Classic.

## JIS

Read from PFU's JIS images (`HHKB421_FW_JP_A904.hfb`, `HHKB820_FW_JP_A249.hfb`), compared with the ANSI ones, and PFU's JIS manuals (P3PC-6651 Hybrid, P3PC-7551 Classic Type-S) (2026-11-19); no JIS keyboard has run ECK yet.

- **Same board as ANSI, plus one row strobe:** 5 rows x 15 columns, the same column muxes, and a fifth strobe on PB11 (every PFU image, ANSI included, drives PB0, PB1, PB2, PB10 and PB11). Matrix positions: `LAYOUT` under `HHKB_JIS` in `stm32/boards/hhkb/board_config.h`. The Classic JIS and Hybrid JIS matrices are identical, and both PFU JIS images use the ANSI images' EEPROM addresses.
- **Keymap:** PFU's JIS base layer (¥, ろ, 半角/全角, 無変換, 変換, かな, arrow keys). The Fn layer follows the keycaps' front legends: as ANSI, plus Fn + A, S, D, F = Volume Down, Volume Up, Mute, Eject and Fn + Up, Down, Left, Right = Shift, ◇, Delete, Ctrl. Mac mode (SW1 or Fn + Ctrl + W) turns 半角/全角, 無変換, 変換 and かな into Caps Lock, 英数 (LANG2), かな (LANG1) and right ◇ (Command), as PFU's JIS Mac table does.
- **DIP switches differ from ANSI** (PFU's JIS manuals): SW1 ON Mac mode (OFF Win; no HHK mode); SW2 ON Control = 英数 and left Fn = Ctrl (also for the Fn + Ctrl chords; the right Fn stays Fn); SW3 ON BS = Delete (Fn + it Backspace); SW4 ON arrows = Shift, ◇, Delete, Ctrl; SW5 ON left ◇ and left Alt swapped; SW6 as ANSI (power saving on the Hybrid, recovery on the Type-S).
- **USB:** product ID `0x4012` (VIA tells the layouts apart by it; definition `stm32/via/via_jis.json`, written by `tools/make_jis_via_json.py`), Vial keyboard ID `ECKHHKB` 2, HID country code 15 (Japan), as PFU's. Over Bluetooth the same: the Hybrid JIS `.hfb` carries a JIS build of the nRF52 app (`make JIS=1`, country code 15 and product ID `0x4012`), as PFU's JIS nRF52 image differs from its ANSI one only there.
- **Pictures:** [layers and chords](layers-jis.svg), [DIP switches](dip-switches-jis.svg) (`tools/gen_layer_svg.py`).
- **Scan:** 75 readings instead of 60, so a scan takes about a quarter longer (estimated ~510 µs per interrupts-off half, over 1 ms per scan). So JIS scans at 500 Hz over USB (`BOARD_SCAN_RATE`; Bluetooth is 250 Hz on every model). Not measured.

## Not supported

- **HHKB Professional 2 and older:** different controller, no USB firmware update.
- **HHKB Studio:** different hardware.
