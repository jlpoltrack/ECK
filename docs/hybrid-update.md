[ECK docs](overview.md)

# Hybrid: install, update, go back to stock

The Hybrid has two chips: the STM32 (keyboard, USB) and the nRF52 (Bluetooth). One `.hfb` (`./build.sh hybrid`, or from a release) carries both. Every step runs over USB and was tested end to end on a PD-KB800BS. You need `dfu-util` (e.g. `brew install dfu-util`) and `uv`. On Linux the tools use hidapi's hidraw backend and need read/write access to the keyboard's `/dev/hidraw*` nodes: run them with `sudo`, or add a udev rule such as `SUBSYSTEM=="hidraw", KERNELS=="*:CAFE:*", TAG+="uaccess"`. The [web flasher](https://jlpoltrack.github.io/ECK/) does the same from a page, but hasn't been run on a keyboard yet.

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

**Over Bluetooth** (from v0.9.0, which installs its boot stub over USB once: don't unplug during that update), with the keyboard connected to this computer by Bluetooth. First press **Fn + Ctrl + U** on the keyboard: it allows Bluetooth updates for 5 minutes (two orange blinks), so a computer alone can't install firmware; on low batteries (level 2-3) it refuses with one long orange blink: plug in USB or replace them.

```sh
uv run --with hidapi python3 tools/hybrid_update.py stm32 --bluetooth $H   # ~25 s, orange LED solid
uv run --with hidapi python3 tools/hybrid_update.py nrf --bluetooth $H     # 15-25 s, orange LED blinks
```

Typing pauses meanwhile. The nRF52 stores the image (only changed 4 KB pages for its own; unchanged ones it copies), checks it, then:
- **STM32:** the STM32 pulls it into Bank 1 above the boot stub, checks its CRC and restarts; the boot stub copies it to Bank 2 (~5 s). A power cut repeats the copy; a bad image is dropped and the old firmware boots. The tool (and the web flasher) then waits until the keyboard reports the new firmware's CRC, which the STM32 tells the nRF52 at each start. An older boot stub can't install it: the update stops with a message before the restart.
- **nRF52:** PFU's bootloader installs it at the restart, as over USB; Bluetooth drops for ~10 s.

Hosts paired before v0.9.0 use the 32-byte VIA channel (~75 s for the STM32): forget the keyboard and pair again for the 244-byte update channel. The web flasher's **Update over Bluetooth** does both chips (the Bluetooth firmware only when it changed). How it works and timings: [Hybrid port](hybrid-port.md#updates).

Back up keymap, macros and settings (data EEPROM) and Bank 2, and restore the EEPROM later (the boot tag is kept, so the installed firmware still boots):

```sh
uv run --with hidapi python3 tools/hybrid_update.py backup backups/mine/   # eeprom.bin, bank1.bin, bank2.bin
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
