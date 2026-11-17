<p align="center"><img src="docs/logo.svg" alt="ECK logo: three lavender keycaps" width="304"></p>

# ECK

Open firmware for the HHKB Professional Hybrid (and Classic / Classic Type-S). It runs on the keyboard's own STM32L072 and, on the Hybrid, its nRF52832 Bluetooth module, and installs over USB: no hardware changes.

Tested on the Hybrid (PD-KB800BS); Classic and Classic Type-S build but are untested on hardware ([models](docs/models.md)).

- STM32: electrostatic-capacitive key sensing, Rapid Trigger, VIA, USB HID, Hybrid power and battery handling.
- nRF52 ([`nrf52/`](nrf52/README.md)): BLE HID keyboard on Nordic's S132 v7.2.0 SoftDevice, updated through the STM32.

Documentation starts at [docs/overview.md](docs/overview.md); open work is in [docs/deferred.md](docs/deferred.md).

## Web flasher

**<https://jlpoltrack.github.io/ECK/>** (Chrome or Edge on a desktop; it needs WebHID and WebUSB). It carries the latest release. Click **Connect keyboard** and it offers what applies to your keyboard: first install from stock (on the Hybrid, followed by ECK's boot stub, which enables USB updates and recovery), updates and settings backup on the Hybrid, the SW6 reinstall steps on a Classic, and recovery for a Hybrid that doesn't start.

Tested on a Hybrid running ECK: keyboard and Bluetooth updates, settings backup and restore, and both recovery buttons. First install from stock firmware is not yet tested.

> On Windows, ST's DFU loader needs the WinUSB driver once (Zadig, device "DFU in FS Mode").

After installing, change keymaps and settings in [VIA](https://www.usevia.app/) with [`stm32/via/via.json`](stm32/via/via.json).

## Hybrid power

Stock firmware is either on (5.1 mA idle on batteries) or off; its auto power-off turns the keyboard fully off, and only the power switch or USB power brings it back. On batteries, ECK steps down instead:

| State | When | Battery current | Wake |
| --- | --- | --- | --- |
| Active | In use | 4.3 mA | |
| Idle | 5 s without a key (Bluetooth) | 1.50 mA | Any key |
| Deep sleep | 60 min without a key (SW6 OFF), 10 min pairing, 1 min without reaching the host | 0.33 mA | Any key (not typed), switch or USB |
| Off | Power switch held 2 s | 0.20 mA (stock 0.20 mA) | Switch held 1 s, or USB |

A day of 2 h typing, 6 h idle and 16 h asleep comes to roughly 2-3 months on two AA cells (estimate). Details: [overview](docs/overview.md#hybrid-use), [measurements](docs/hybrid-hardware.md#5-power).

## Layout

| Path | Contents |
| --- | --- |
| `stm32/app/` | Startup, keyboard task, board events, FreeRTOS config |
| `stm32/keys/` | Layers, hold-tap, one-shot, macros |
| `stm32/via/` | VIA protocol, HHKB Settings menu, keymap/macro storage, `via.json` |
| `stm32/usb/` | USB HID (TinyUSB glue, descriptors) |
| `stm32/board/` | Board config, clocks, DIP switches, LEDs, linker script, FreeRTOS port |
| `stm32/ec/` | Key matrix scanning, calibration, Rapid Trigger |
| `stm32/hybrid/` | Hybrid power, sleep, Bluetooth chords, SPI link to the nRF52 |
| `stm32/boot/` | Hybrid Bank 1 boot stub |
| `stm32/vendor/` | FreeRTOS 11.3.3, TinyUSB 0.21.0 subset, ST CMSIS and LL headers |
| `common/` | `ble_link.h`, shared by the STM32 and nRF52 builds |
| `nrf52/` | nRF52832 Bluetooth app (nRF5 SDK 17.1.1) |
| `tools/` | `.hfb` builder, flashers, Hybrid updater, build hash check, web bundler; `tools/bench/` |
| `web/` | Web flasher |
| `tests/` | Host unit tests (C and Python) |

## Build, test, flash

Needs the Arm GNU Toolchain 15.3.Rel2 (`arm-none-eabi-gcc`; CI uses the same), CMake, `uv`, and for the Hybrid `.hfb` the nRF5 SDK 17.1.1 at `../nRF5_SDK_17.1.1`.

```bash
./build.sh hybrid    # build/hybrid/hhkb_ec_hybrid.bin and .hfb (also classic, type_s)
tests/run_all_tests.sh
uv run --with hidapi python3 tools/hybrid_update.py stm32 build/hybrid/hhkb_ec_hybrid.bin
```

Options, CI and releases: [docs/build.md](docs/build.md).

## Origin and licence

Borrows code from [aroum/dmk](https://github.com/aroum/dmk) (MIT), including aroum's HHKB Classic port.

MIT ([LICENSE](LICENSE)). Third-party parts keep their own licences: FreeRTOS (MIT), TinyUSB (MIT, `stm32/vendor/tinyusb/LICENSE`), ST CMSIS and LL headers (`stm32/vendor/st/*/LICENSE.md`), Nordic linker script (see [nrf52/README.md](nrf52/README.md)). The released firmware also contains Nordic's SoftDevice and nRF5 SDK code; [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) lists everything in the binaries with its licence and ships with each release. PFU firmware is not included or redistributed.

ECK is an independent project, not affiliated with or endorsed by PFU Limited. HHKB and Happy Hacking Keyboard are trademarks of PFU Limited; they are used here only to name the keyboards ECK runs on.
