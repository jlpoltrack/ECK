[ECK docs](overview.md)

# Hybrid: install, update, go back to stock

The Hybrid has two chips: the STM32 (keyboard, USB) and the nRF52 (Bluetooth). One `.hfb` (`./build.sh hybrid`, or from a release) carries both. Every step runs over USB and was tested end to end on a PD-KB800BS. You need `dfu-util` (e.g. `brew install dfu-util`) and `uv`. The [web flasher](https://jlpoltrack.github.io/ECK/) does the same from a page, but hasn't been run on a keyboard yet.

## 1. First install (from stock)

PFU's own updater installs both chips. Keep the keyboard plugged in until the tool says the Bluetooth wait is over (~15 s):

```sh
uv run --with hidapi python3 tools/hhkb_flasher.py build/hybrid/hhkb_ec_hybrid.hfb
```

Then replace PFU's updater in Bank 1 with ECK's boot stub, which enables updates and recovery over USB (`--stub FILE` picks another stub image):

```sh
make -C stm32/boot
uv run --with hidapi python3 tools/hybrid_update.py boot
```

**Keep the backup.** `boot` first saves your keyboard's original Bank 1 and data EEPROM as `hybrid-bank1-backup.bin` and `hybrid-eeprom-backup.bin` in the current folder (it stops if they exist). Going back to stock needs them; PFU's files don't contain Bank 1.

## 2. Updating ECK

With ECK running, `hybrid_update.py` switches the keyboard into the right mode itself:

```sh
H=build/hybrid/hhkb_ec_hybrid.hfb
uv run --with hidapi python3 tools/hybrid_update.py stm32 $H   # keyboard firmware, ~30 s
uv run --with hidapi python3 tools/hybrid_update.py nrf $H     # Bluetooth firmware, ~40 s
```

- **STM32:** ECK starts ST's USB DFU loader (in the chip's ROM). The tool writes and verifies the image, and only then writes the tag that lets it boot. It refuses unless Bank 1 holds the boot stub (`--force` overrides), since otherwise a failed write has no USB recovery.
- **nRF52:** ECK's nRF52 app stores the image and checks its MD5; PFU's nRF52 bootloader installs it at the next restart. Pairings are kept. Update it only when it changed; order doesn't matter.

Back up keymap, macros and settings (data EEPROM) and Bank 2, and restore the EEPROM later (the boot tag is kept, so the installed firmware still boots):

```sh
uv run --with hidapi python3 tools/hybrid_update.py backup backups/mine/   # eeprom.bin, bank2.bin
uv run --with hidapi python3 tools/hybrid_update.py restore-eeprom backups/mine/eeprom.bin
```

## 3. Going back to stock

nRF52 first (ECK must be running to pass it on), then the STM32, then unplug and switch the keyboard off and on:

```sh
uv run --with hidapi python3 tools/hybrid_update.py nrf HHKB800_FW_A048.hfb
uv run --with hidapi python3 tools/hybrid_update.py restore-stm32 --hfb HHKB800_FW_A048.hfb --bank1 hybrid-bank1-backup.bin
```

Both chips then hold PFU's firmware byte for byte, and PFU's updater works again.

## 4. Recovery

If ECK doesn't start, **take the batteries out, then hold the power switch while plugging in USB** (keep holding about 2 s). The boot stub checks the switch only at a reset, and with batteries in, plugging in USB doesn't reset a running keyboard. It then starts ST's DFU loader (it also does so whenever Bank 2 fails its check), and `hybrid_update.py stm32 …` writes a working image.

If Bluetooth is broken but USB works, stage an image through the nRF52's recovery mode, which starts without Bluetooth:

```sh
uv run --with hidapi python3 tools/hybrid_update.py nrf --recovery build/hybrid/hhkb_ec_hybrid.hfb
```

Only SWD recovers a power loss during the few seconds Bank 1 is written, or an nRF52 app too broken to reach recovery mode.

**Why not PFU's updater?** It works only while PFU's nRF52 app answers it; with ECK's nRF52 app it enumerates but never responds. Details: [Hybrid port](hybrid-port.md#updates), `stm32/boot/README.md`.
