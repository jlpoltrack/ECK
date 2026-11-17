[ECK docs](overview.md)

# Install (Classic and Classic Type-S)

For the Classic and Classic Type-S, which build but are untested on hardware. For the Hybrid see [Hybrid updates](hybrid-update.md). The [web flasher](https://jlpoltrack.github.io/ECK/) offers the same install.

> [!WARNING]
> You flash the keyboard at your own risk. The author accepts no responsibility for damage, data loss, or flashing the wrong image or model.

A fresh install uses PFU's factory thresholds from the keyboard's EEPROM, so no calibration is needed. Read [Backup & restore](backup-restore.md) first.

## Before flashing

Build the image (`./build.sh classic` or `type_s`; [Build](build.md)). ECK goes in Flash Bank 2; PFU's updater stays in Bank 1 and installs it.

- **Stock firmware:** PFU's updater is already reachable. Do **not** use SW6.
- **ECK already installed:** ECK has its own USB identity, so return to Bank 1 first: with the keyboard unplugged, set SW6 ON, connect, then set SW6 OFF once Bank 1 runs. This erases the start of Bank 2 ([SW6 recovery](backup-restore.md#sw6-recovery)).

Make sure SW6 is OFF before flashing.

## Install with PFU's Keymap Tool

Open the `.hfb` in HHKB Keymap Tool and run the update. At the end it reports "An error occurred during writing to HHKB…". That is expected: ECK has started and reconnected under its own USB ID, which Keymap Tool doesn't know.

## Install with `hhkb_flasher.py`

A cross-platform alternative to Keymap Tool that talks to Bank 1's update interface:

```bash
uv run --with hidapi python3 tools/hhkb_flasher.py --info      # check the model it sees
uv run --with hidapi python3 tools/hhkb_flasher.py build/<variant>/hhkb_ec_<variant>.hfb
```

Before sending, it checks both CRCs, that the model in the file matches the keyboard, and that the reset vector points into Bank 2. `--skip-crc` and `--skip-model-check` turn off the local checks (Bank 1 still validates the image); `--force` skips the reset-vector check for custom images.

PFU's update protocol (verified on hardware; an SWD readback of Bank 2 matched the container byte for byte, 2026-11-11):

- `0xE0` enter update mode; the device re-enumerates (~10 s).
- `0xE1` start: container size (65,570) and outer CRC; Bank 1 erases Bank 2.
- `0xE2` send: the container minus its first two bytes, 57 bytes per 64-byte report, header `AA AA E2 00 len seq_lo seq_hi`.
- `0xE3` end: verify and reboot.

Container layout: [Build](build.md#hfb-container).

## DFU and SWD

ST's ROM DFU loader and SWD are recovery and development paths ([Backup & restore](backup-restore.md)). DFU writes a raw Bank 2 `.bin` but doesn't update Bank 1's metadata, so Bank 1 may refuse the image; use PFU's updater for normal installs.
