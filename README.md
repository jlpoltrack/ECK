<p align="center"><img src="docs/logo.svg" alt="ECK logo: three lavender keycaps" width="304"></p>

# ECK

Open firmware for electrostatic-capacitive (Topre) keyboards built on the STM32L072. It runs on the keyboard's own controller and installs over USB: no soldering, no hardware changes.

## Supported keyboards

| Keyboard | Status | Install |
| --- | --- | --- |
| HHKB Professional Hybrid and Hybrid Type-S (`PD-KB800…`) | **Tested** on `PD-KB800BS`, USB and Bluetooth | [Web flasher](#web-flasher) or release `.hfb` |
| HHKB Professional Classic | Builds; untested on hardware | Web flasher or release `.hfb` |
| HHKB Professional Classic Type-S | Builds; untested on hardware | Web flasher or release `.hfb` |
| Topre Realforce R3S US TKL (`R3SD13`) | **Tested**, USB | Web flasher or release `.rfb` |

Model numbers, what isn't supported (JIS, HHKB Pro 2, Studio) and why: [docs/models.md](docs/models.md).

## Features

- **Key sensing:** 1000 Hz scanning over USB (stock HHKB: ~50 Hz), factory thresholds or per-key calibration, adjustable actuation point and hysteresis, Rapid Trigger.
- **[Vial](docs/vial.md)** on every keyboard, no definition file needed: keymap, macros, tap dance, combos, key overrides, Caps Word, one-shot keys, QMK settings.
- **[VIA](docs/via.md)** with the **HHKB Settings** menu (tap-hold, calibration, Rapid Trigger, LEDs): press Fn + Ctrl + V to switch to VIA mode until the next restart.
- Mod-Tap and Layer-Tap, two base/Fn layer pairs, assignable LEDs, stock DIP switch behaviour on the HHKB.
- **Hybrid:** Bluetooth LE with four host slots on Nordic's S132 SoftDevice ([`nrf52/`](nrf52/README.md)), the stock key combinations, and much lower battery drain ([below](#hybrid-battery-life)).

Full feature list and day-to-day use: [docs/overview.md](docs/overview.md). Open work: [docs/deferred.md](docs/deferred.md).

## Install

### Web flasher

**<https://jlpoltrack.github.io/ECK/>** (Chrome or Edge on a desktop, for WebHID and WebUSB). It carries the latest release, with one tab for the HHKB and one for the Realforce R3S. Click **Connect keyboard** and it offers what fits your keyboard: first install from stock (on the Hybrid, followed by ECK's boot stub, which enables USB updates and recovery), updates and settings backup on the Hybrid and the R3S, the SW6 reinstall steps on a Classic, and recovery for a Hybrid or R3S that doesn't start. On the R3S, an update with Topre's own `.rfb` (Advanced) puts the stock firmware back.

Tested on a Hybrid running ECK: keyboard and Bluetooth updates, settings backup and restore, both recovery buttons. First install from stock firmware is not yet tested on an HHKB. On the R3S, the flasher's code passed install from stock, update, backup, restore and the return to stock on the owner's keyboard (driven from Node by `tools/web_bench/bench.mjs`); its buttons haven't been clicked through in Chrome with an R3S yet.

> On Windows, ST's DFU loader needs the WinUSB driver once (Zadig, device "DFU in FS Mode").

Command-line alternatives: [Hybrid updates](docs/hybrid-update.md), [Classic install](docs/install.md), [backup and restore](docs/backup-restore.md).

### Realforce R3S

Use the web flasher's Realforce R3S tab or a release `.rfb`, or build it (see [Build](#build-test-flash)) and flash over USB; either works from Topre's stock firmware, from ECK, or from ST's DFU loader:

```bash
./build.sh r3s
uv run -q --with hidapi python3 tools/r3s/r3s_backup.py flash build/r3s/hhkb_ec_r3s.rfb
```

Chords (hold Fn + Ctrl): **W** switches Mac mode (Alt and Win swap on both sides, until unplugged; Simple Mode blinks blue twice for Mac, once for Win), **V** VIA mode (twice into VIA, once back to Vial). Num, Caps and Scroll Lock light as the computer reports them (macOS reports only Caps Lock). During a firmware update Simple Mode stays magenta.

The R3S has no recovery switch: hold Esc and Caps Lock while plugging it in and it starts ST's DFU loader (Simple Mode lit magenta), and the web flasher (Recover firmware) or `r3s_backup.py` can flash or restore from there (including Topre's stock `.rfb`). Hardware notes and measurements: [docs/r3s-hardware.md](docs/r3s-hardware.md).

## Hybrid battery life

Stock firmware is either on (5.1 mA idle on batteries) or off. On batteries, ECK steps down instead:

| State | When | Battery current | Wake |
| --- | --- | --- | --- |
| Active | In use | 4.3 mA | |
| Idle | 5 s without a key (Bluetooth) | 1.50 mA | Any key |
| Deep sleep | 60 min without a key (SW6 OFF), 10 min pairing, 1 min without reaching the host | 0.33 mA | Any key (not typed), switch or USB |
| Off | Power switch held 2 s | 0.20 mA (stock 0.20 mA) | Switch held 1 s, or USB |

A day of 2 h typing, 6 h idle and 16 h asleep comes to roughly 2-3 months on two AA cells (estimate). Details: [overview](docs/overview.md#hybrid-use), [measurements](docs/hybrid-hardware.md#5-power).

## Build, test, flash

Needs the Arm GNU Toolchain 15.3.Rel2 (`arm-none-eabi-gcc`; CI uses the same), CMake, `uv`, and for the Hybrid `.hfb` the nRF5 SDK 17.1.1, found through `$NRF5_SDK_ROOT`, `../../SDKs/nRF5_SDK_17.1.1` or `../nRF5_SDK_17.1.1`.

```bash
./build.sh hybrid    # build/hybrid/hhkb_ec_hybrid.bin and .hfb; also classic, type_s, r3s
tests/run_all_tests.sh
uv run --with hidapi python3 tools/hybrid_update.py stm32 build/hybrid/hhkb_ec_hybrid.bin
```

Options, CI and releases: [docs/build.md](docs/build.md).

## Layout

| Path | Contents |
| --- | --- |
| `stm32/app/` | Startup, keyboard task, FreeRTOS config |
| `stm32/keys/` | Layers, hold-tap, one-shot, macros |
| `stm32/via/` | Vial and VIA protocols, HHKB Settings menu, keymap/macro storage, HHKB `via.json` |
| `stm32/usb/` | USB HID (TinyUSB glue, descriptors) |
| `stm32/board/` | Shared firmware config, clocks, LEDs, EEPROM writer, linker script, FreeRTOS port |
| `stm32/boards/hhkb/` | HHKB board: matrix, layout and EEPROM map (`board_config.h`), default keymap, matrix scan, DIP switches |
| `stm32/boards/r3s/` | Realforce R3S board: matrix, layout, EEPROM map, scan, indicators, `via.json` |
| `stm32/ec/` | Scan timing, calibration, Rapid Trigger |
| `stm32/hybrid/` | Hybrid power, sleep, Bluetooth chords, SPI link to the nRF52 |
| `stm32/boot/` | Hybrid Bank 1 boot stub |
| `stm32/vendor/` | FreeRTOS 11.3.3, TinyUSB 0.21.0 subset, ST CMSIS and LL headers |
| `common/` | `ble_link.h`, shared by the STM32 and nRF52 builds |
| `nrf52/` | nRF52832 Bluetooth app (nRF5 SDK 17.1.1) |
| `tools/` | `.hfb` builder, flashers, Hybrid updater, build hash check, web bundler; `tools/bench/` (bench measurements), `tools/r3s/` (R3S flasher and analysis) |
| `web/` | Web flasher |
| `tests/` | Host unit tests (C and Python) |

## Origin and licence

Borrows code from [aroum/dmk](https://github.com/aroum/dmk) (MIT), including aroum's HHKB Classic port.

MIT ([LICENSE](LICENSE)). Third-party parts keep their own licences: FreeRTOS (MIT), TinyUSB (MIT, `stm32/vendor/tinyusb/LICENSE`), ST CMSIS and LL headers (`stm32/vendor/st/*/LICENSE.md`), Nordic linker script (see [nrf52/README.md](nrf52/README.md)). The released firmware also contains Nordic's SoftDevice and nRF5 SDK code; [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) lists everything in the binaries with its licence and ships with each release. PFU and Topre firmware is not included or redistributed.

ECK is an independent project, not affiliated with or endorsed by PFU Limited or Topre Corporation. HHKB and Happy Hacking Keyboard are trademarks of PFU Limited; Realforce is a trademark of Topre Corporation. They are used here only to name the keyboards ECK runs on.
