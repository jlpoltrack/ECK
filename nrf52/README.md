# nrf52: Bluetooth app for the HHKB Hybrid

Runs on the Hybrid's nRF52832 (module MD1): takes key reports from ECK on the STM32 over SPI and sends them as a BLE HID keyboard, and relays VIA requests between the host and the STM32 ([VIA over Bluetooth](../docs/hybrid-port.md#bluetooth)). It replaces PFU's nRF52 app and SoftDevice (S132 v6.0.0 → Nordic's v7.2.0, same `0x26000` app start); PFU's MBR and bootloader stay on the chip.

Elsewhere: Bluetooth behaviour in [hybrid-port.md](../docs/hybrid-port.md#bluetooth); wiring, flash map and SWD in [hybrid-hardware.md](../docs/hybrid-hardware.md); PFU's staging format in [hybrid-stock-behaviour.md](../docs/hybrid-stock-behaviour.md).

## Build

Needs `arm-none-eabi-gcc` and the nRF5 SDK 17.1.1 (default `../nRF5_SDK_17.1.1`; `../build.sh` also finds it through `$NRF5_SDK_ROOT` or `../../SDKs/`). From this directory:

```
make                           # or: make SDK_ROOT=/path/to/nRF5_SDK_17.1.1
make clean && make NRF_LOG=1   # with the SEGGER RTT log for SWD debugging (~15 KB larger)
```

Output: `_build/nrf52832_xxaa.bin` and `.hex`, linked at `0x26000`. It must fit the 64 KB app slot, hence `-Os`. `./build.sh hybrid` at the root builds it and packs it into the Hybrid `.hfb`. `make JIS=1` builds the Hybrid JIS app into `_build_jis/` (HID country code 15, product ID `0x4012`), which `./build.sh hybrid_jis` packs.

## Install

Over USB through ECK: `uv run --with hidapi python3 tools/hybrid_update.py nrf <.hfb>` from the root ([Hybrid updates](../docs/hybrid-update.md)). ECK relays the image as `BLE_LINK_UPDATE` frames; `nrf_update.c` stops the SoftDevice, stages it at `0x37000` with PFU's record at `0x36000`, and PFU's bootloader installs it at the next reset (~4 s). Bonds survive. Since PFU's bootloader and record format are kept, PFU's `.hfb` goes back the same way.

Fallback over SWD (vias below the module, photo below), from this directory:

```
uv run --with pyocd python3 tools/nrf_flash.py --nrf <probe uid>                  # app only
uv run --with pyocd python3 tools/nrf_flash.py --nrf <probe uid> --hfb <file.hfb>   # SoftDevice + app
```

It erases only the sectors it writes, verifies and resets. App-only mode refuses unless the chip holds S132 v7.2.0; `--hfb` writes a `.hfb`'s whole nRF section (`0x1000-0x35FFF`, e.g. PFU's to go back to stock), leaving the MBR and bootloader alone. `make flash` writes the app without these checks. Never let pyOCD auto-unlock: it mass-erases the chip, including PFU's bootloader.

![nRF52 SWD vias below module MD1: SWDCLK (green) and SWDIO (red)](docs/images/hybrid-nrf52-swd.png)

## Recovery mode

Replaces an app whose Bluetooth side is broken (crashes in SoftDevice or BLE start-up, or never answers) over USB. The STM32 holds PB6 high while it resets the nRF52 (`hybrid_update.py nrf --recovery FILE.hfb`, or the web flasher's checkbox). The app samples P0.18 first thing in `main()` (pull-down, 8 samples) and, if high, starts only the SPI link and the update receiver: no SoftDevice, status flag `BLE_LINK_ST_RECOVERY`. Any normal restart drops PB6. The STM32 resets an nRF52 found in recovery mode it didn't ask for (at most 3 times) and restarts it normally before power-off. A crash in recovery mode resets into it again. An app too broken to reach `main()` still needs SWD.

## Link protocol

`common/ble_link.h` defines the SPI link (32-byte frames, STM32 master, nRF52 slave); both builds include it. Bump `BLE_LINK_VERSION` (now 8) on any frame change. The nRF52 reports its version in every status reply; the STM32 doesn't check it.

## Licence

- `main.c`, `nrf_update.c/.h`, `config/app_config.h` and `tools/` are under the repository's MIT licence. `main.c`'s peer manager and advertising setup follow the SDK 17.1.1 `ble_app_hids_keyboard` example; `config/app_config.h` overrides that example's `sdk_config.h`, which the Makefile includes from the SDK.
- `Makefile` follows the SDK examples' layout (their makefiles carry no Nordic notice).
- `hybrid_ble_gcc_nrf52.ld` is adapted from the SDK example's linker script (which carries no notice) and stays under Nordic Semiconductor's 5-clause licence. No SDK sources are in this repository.
- That licence allows binaries only on Nordic chips (true here) and exempts binaries inside a product's software update from reproducing the notice; a release shipping `nrf52832_xxaa.hex` on its own should include it anyway.
