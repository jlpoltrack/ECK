**Documentation:** [Install](install.md) • [Backup & restore](backup-restore.md) • [Build](build.md) • [Config](config.md) • [Models](models.md) • [VIA](via.md) • [Debug](debug.md)

# Supported models

ECK runs on the keyboard's own STM32L072 (and, on the Hybrid, its nRF52832 Bluetooth module). No hardware changes: it installs over USB.

| Keyboard | Model numbers | Container ID | Build | Status |
| --- | --- | --- | --- | --- |
| HHKB Professional HYBRID Type-S | `PD-KB800BS` | `AHUX01` | `./build.sh hybrid` | **Tested** on hardware (USB and Bluetooth) |
| HHKB Professional Classic | `PD-KB401W`, `PD-KB401B`, `PD-KB401WN`, `PD-KB401BN` | `AHHX01` | `./build.sh classic` | Builds; untested on hardware |
| HHKB Professional Classic Type-S | `PD-KB401WSC`, `PD-KB401BSC`, `PD-KB401YSC` | `AHHS01` | `./build.sh type_s` | Builds; untested on hardware |

The model numbers are the ones the flashers (`tools/hhkb_flasher.py`, `web/`) recognise from the PFU updater; any other model number is refused unless the model check is skipped. An `N` suffix means blank keycaps; colour and legends do not change the electronics.

## Hybrid

Only the PD-KB800BS (PFU firmware `HHKB800_FW_A048.hfb`, model `AHUX01`) has been tested. Other HYBRID and HYBRID Type-S models probably share the board, but this is unverified and the flashers do not recognise their model numbers. See [Hybrid updates](hybrid-update.md) and [Hybrid hardware](hybrid-hardware.md).

## Classic and Classic Type-S

Both build from the same code as the Hybrid, without the Bluetooth parts; Classic and Type-S get the same Bank 2 image and differ only in the `.hfb` model ID and version (A4.50 for `AHHX01`, A8.50 for `AHHS01`, [Build](build.md#hfb-container-and-model-variants)). The STM32 pins and matrix match the Hybrid's (STM32L072RB, 4 × 15 matrix, dual-bank flash with PFU's updater in Bank 1). They have not been run on a Classic by this project; the Classic port comes from [aroum/dmk](https://github.com/aroum/dmk). Keep a backup ([Backup & restore](backup-restore.md)).

## Not supported

- **JIS models** (e.g. the `PD-KB421` series and Hybrid JIS): different key layout; ECK has no JIS matrix or keymap. The flashers refuse `PD-KB421` model numbers. Whether their boards otherwise match is unverified.
- **HHKB Professional 2 and older:** different controller and no USB firmware update path.
- **HHKB Studio:** different hardware (mechanical switches, pointing stick).
