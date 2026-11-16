# ECK

Open firmware for the HHKB Professional Hybrid (and Classic / Classic Type-S) that runs on the keyboard's own STM32L072 and, on the Hybrid, its nRF52832 Bluetooth module. No hardware changes: it installs over USB through PFU's updater.

Tested on hardware: the Hybrid (PD-KB800BS). Classic and Classic Type-S build but are untested on hardware ([docs/models.md](docs/models.md)).

- STM32 side: electrostatic-capacitive key sensing, rapid trigger, VIA, USB HID, Hybrid power and battery handling, SPI link to the nRF52.
- nRF52 side ([`nrf52/`](nrf52/README.md)): BLE HID keyboard on PFU's S132 SoftDevice, staged updates through the STM32.

Documentation: [docs/overview.md](docs/overview.md) and the rest of [docs/](docs/); Hybrid details in [hybrid-port.md](docs/hybrid-port.md); open and deferred work in [deferred.md](docs/deferred.md).

## Layout

| Path | Contents |
| --- | --- |
| `stm32/` | STM32L072 firmware (CMake project, `stm32/CMakeLists.txt`) |
| `stm32/app/` | Startup, keyboard task, board event dispatch (`events.c`), memory, FreeRTOS config |
| `stm32/keys/` | Layers, hold-tap, one-shot, macros |
| `stm32/via/` | VIA protocol, HHKB settings menu, keymap/macro EEPROM storage, `via.json` |
| `stm32/usb/` | USB HID (TinyUSB glue, descriptors) |
| `stm32/board/` | Board config, clocks (`platform.c`), DIP switches and modes, LEDs and indicators, key positions, linker script, FreeRTOS port |
| `stm32/ec/` | Electrostatic-capacitive matrix scanning, calibration, rapid trigger |
| `stm32/hybrid/` | Hybrid power, sleep, Bluetooth chords, SPI link to the nRF52 |
| `stm32/boot/` | Hybrid Bank 1 boot stub (Makefile) |
| `stm32/vendor/` | FreeRTOS kernel; TinyUSB 0.18.0 subset; ST CMSIS and LL headers |
| `common/` | `ble_link.h`, shared by the STM32 and nRF52 builds |
| `nrf52/` | nRF52832 Bluetooth app (Makefile, nRF5 SDK 15.0.0) |
| `tools/` | `.hfb` builder, PFU-updater flasher, Hybrid USB updater, build hash check, web flasher bundler; `tools/bench/` bench tools |
| `web/` | Web flasher (Chrome/Edge, WebHID/WebUSB): first install, boot stub, Hybrid updates and backup |
| `tests/` | Host unit tests (C and Python) |
| `docs/` | Documentation |

## Build, test, flash

Needs `arm-none-eabi-gcc`, CMake, `uv`, and for the Hybrid `.hfb` the nRF5 SDK 15.0.0 unpacked at `../nRF5_SDK_15.0.0`.

```bash
./build.sh hybrid    # build/hybrid/hhkb_ec_hybrid.bin and .hfb
./build.sh classic   # build/classic/hhkb_ec_classic.bin and .hfb
./build.sh type_s    # build/type_s/hhkb_ec_type_s.bin and .hfb
tests/run_all_tests.sh
uv run --with hidapi python3 tools/hybrid_update.py stm32 build/hybrid/hhkb_ec_hybrid.bin
```

`-c` cleans first (including `nrf52/_build`); `-d HHKB_ADC_DIAGNOSTICS,...` adds bench options ([docs/build.md](docs/build.md)). `tools/build_hashes.sh <source> <out>` builds every variant with a fixed version string and prints their hashes, to check that a refactor leaves the binaries unchanged. Installing and updating the Hybrid: [docs/hybrid-update.md](docs/hybrid-update.md).

A `v*` tag makes CI publish a GitHub release with the three `.hfb` files, `hybrid_boot.bin` and `eck-web-flasher-<tag>.zip` (the web flasher with those images bundled; serve it locally, e.g. `python3 -m http.server`).

## Origin and licence

Borrows code from [aroum/dmk](https://github.com/aroum/dmk) (MIT), including aroum's HHKB Classic port.

MIT ([LICENSE](LICENSE)). Third-party parts keep their own licences: FreeRTOS (MIT), TinyUSB (MIT, `stm32/vendor/tinyusb/LICENSE`), ST CMSIS and LL headers (`stm32/vendor/st/*/LICENSE.md`), Nordic linker script (see [nrf52/README.md](nrf52/README.md)). PFU firmware is not included or redistributed.
