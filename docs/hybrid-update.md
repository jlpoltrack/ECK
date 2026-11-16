**Documentation:** [Install](install.md) • [Backup & restore](backup-restore.md) • [Build](build.md) • [Config](config.md) • [VIA](via.md) • [Hybrid port](hybrid-port.md) • [Hybrid updates](hybrid-update.md) • [Hybrid hardware](hybrid-hardware.md)

# HHKB Professional Hybrid: installing, updating and going back to stock

The Hybrid has two chips: the STM32 (keyboard, USB) and the nRF52 (Bluetooth). One ECK `.hfb` (`build/hybrid/hhkb_ec_hybrid.hfb`, from `./build.sh hybrid`) carries firmware for both. All steps below run over USB and were tested end to end on a PD-KB800BS. You need `dfu-util` (e.g. `brew install dfu-util`) and `uv`.

## 1. First install (from stock)

PFU's own updater installs both chips from one `.hfb`:

```sh
uv run --with hidapi python3 tools/hhkb_flasher.py build/hybrid/hhkb_ec_hybrid.hfb
```

Keep it plugged in until the tool says the Bluetooth module wait has finished (about 15 s).

Then replace PFU's updater in the STM32's first flash bank with ECK's small boot stub; this enables recovery (section 4). Build the stub first (`arm-none-eabi-gcc`; output `_build/hybrid_boot.bin`):

```sh
make -C stm32/boot
uv run --with hidapi python3 tools/hybrid_update.py boot
```

**Keep the backup.** Going back to stock needs your keyboard's original Bank 1 (64 KB at `0x08000000`), which PFU's update files do not contain. `boot` saves it the first time, with the data EEPROM, as `hybrid-bank1-backup.bin` and `hybrid-eeprom-backup.bin` in the current folder (it stops if those files already exist).

## 2. Updating ECK

With ECK running, `hybrid_update.py` puts the keyboard into the right mode itself:

```sh
H=build/hybrid/hhkb_ec_hybrid.hfb
uv run --with hidapi python3 tools/hybrid_update.py stm32 $H   # keyboard firmware, ~30 s
uv run --with hidapi python3 tools/hybrid_update.py nrf $H     # Bluetooth firmware, ~40 s
```

- **STM32:** ECK starts ST's USB DFU loader, built into the chip (VIA's bootloader command, or the bootloader key). The tool writes the image, verifies it, and only then writes the tag that lets it boot. It refuses unless Bank 1 holds the boot stub (section 1), since without it a failed write has no USB recovery; `--force` overrides.
- **nRF52:** ECK passes the image to its nRF52 app, which stops Bluetooth, stores the image, and checks its MD5. On the following restart PFU's nRF52 bootloader (unchanged in the chip) installs it. Bluetooth pairings are kept.
- Update the nRF52 only when its part changed. If both changed, either order works.

## 3. Going back to stock

The nRF52 first (ECK has to be running to pass it on), then the STM32:

```sh
uv run --with hidapi python3 tools/hybrid_update.py nrf HHKB800_FW_A048.hfb
uv run --with hidapi python3 tools/hybrid_update.py restore-stm32 --hfb HHKB800_FW_A048.hfb --bank1 hybrid-bank1-backup.bin
```

Then unplug USB and switch the keyboard off and on. Both chips then hold PFU's firmware again (tested byte for byte), and PFU's own updater works as before.

## 4. Recovery

If ECK does not start (a broken or half-written update), or to force the loader: **hold the power switch while plugging in USB**. The boot stub then starts ST's USB DFU loader (it also does so by itself whenever the keyboard firmware fails its check), and `hybrid_update.py stm32 ...` writes a working image.

If the Bluetooth firmware is broken (no Bluetooth, but the keyboard works over USB), stage a working image through the nRF52's recovery mode, which starts without Bluetooth:

```sh
uv run --with hidapi python3 tools/hybrid_update.py nrf --recovery build/hybrid/hhkb_ec_hybrid.hfb
```

The loader is in the chip's ROM and cannot be overwritten. Two windows still need SWD to recover: power lost while the boot stub or the stock Bank 1 is being written (a few seconds), or an nRF52 app too broken to start its recovery mode.

## Why not PFU's updater for updates

PFU's updater lives in the STM32's Bank 1 and only works while PFU's own nRF52 app answers it. With ECK's nRF52 app installed it never responds, so ECK uses ST's loader for the STM32 and its own link for the nRF52 instead. The details: [hybrid-port.md](hybrid-port.md), `stm32/boot/README.md`.
