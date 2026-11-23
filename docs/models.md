[ECK docs](overview.md)

# Supported models

| Keyboard | Model numbers | Container ID | Build | Status |
| --- | --- | --- | --- | --- |
| HHKB Professional HYBRID and HYBRID Type-S | `PD-KB800` + `B`/`W`/`Y`, optional `N`, optional `S`, `SC`, `SC1`… | `AHUX01` | `./build.sh hybrid` | **Tested** on `PD-KB800BS` (USB and Bluetooth) |
| HHKB Professional Classic | `PD-KB401` + `B`/`W`, optional `N` | `AHHX01` | `./build.sh classic` | Builds; untested on hardware |
| HHKB Professional Classic Type-S | `PD-KB401` + `B`/`W`/`Y`, optional `N`, `SC`, `SC1`… | `AHHS01` | `./build.sh type_s` | Builds; untested on hardware |
| Topre Realforce R3S US TKL | `R3SD13` | `R3SD####` | `./build.sh r3s` | **Tested** (USB) ([R3S hardware](r3s-hardware.md)) |

The flashers (`tools/hhkb_flasher.py`, `web/`) accept only these model numbers unless the model check is skipped. An `N` suffix means blank keycaps, `S` Type-S switches, `SC`/`SC1` special editions; colour, legends and switches don't change the electronics.

- **Hybrid:** tested on the PD-KB800BS. PFU ships one firmware (`HHKB800_FW_A048.hfb`, model `AHUX01`) for every ANSI HYBRID, so they share the board; aroum/dmk reports its Hybrid build running across the family (2026-11-22).
- **Classic and Type-S:** same STM32 code without the Bluetooth parts, and the same pins, matrix and dual-bank layout as the Hybrid. The Classic port comes from [aroum/dmk](https://github.com/aroum/dmk); this project has not run it on a Classic.

## Not supported

- **JIS models** (e.g. `PD-KB421` series, Hybrid JIS): ECK has no JIS matrix or keymap; the flashers refuse them.
- **HHKB Professional 2 and older:** different controller, no USB firmware update.
- **HHKB Studio:** different hardware.
