[ECK docs](overview.md)

# Supported models

| Keyboard | Model numbers | Container ID | Build | Status |
| --- | --- | --- | --- | --- |
| HHKB Professional HYBRID Type-S | `PD-KB800BS` | `AHUX01` | `./build.sh hybrid` | **Tested** (USB and Bluetooth) |
| HHKB Professional Classic | `PD-KB401W`, `PD-KB401B`, `PD-KB401WN`, `PD-KB401BN` | `AHHX01` | `./build.sh classic` | Builds; untested on hardware |
| HHKB Professional Classic Type-S | `PD-KB401WSC`, `PD-KB401BSC`, `PD-KB401YSC` | `AHHS01` | `./build.sh type_s` | Builds; untested on hardware |
| Topre Realforce R3S US TKL | `R3SD13` | `R3SD####` | `./build.sh r3s` | Bring-up: types, indicators, VIA (`stm32/boards/r3s/via.json`) ([R3S hardware](r3s-hardware.md)) |

The flashers (`tools/hhkb_flasher.py`, `web/`) accept only these model numbers unless the model check is skipped. An `N` suffix means blank keycaps; colour and legends don't change the electronics.

- **Hybrid:** only the PD-KB800BS (PFU firmware `HHKB800_FW_A048.hfb`) has been tested. Other HYBRID models probably share the board, but that is unverified and the flashers don't accept them.
- **Classic and Type-S:** same STM32 code without the Bluetooth parts, and the same pins, matrix and dual-bank layout as the Hybrid. The Classic port comes from [aroum/dmk](https://github.com/aroum/dmk); this project has not run it on a Classic.

## Not supported

- **JIS models** (e.g. `PD-KB421` series, Hybrid JIS): ECK has no JIS matrix or keymap; the flashers refuse them.
- **HHKB Professional 2 and older:** different controller, no USB firmware update.
- **HHKB Studio:** different hardware.
