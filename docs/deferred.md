# Deferred and open items

Decided but not done, or waiting on a decision. Delete an item when it's done.

## Waiting on the owner

- **Web flasher first install.** Everything else passed on the owner's Hybrid (2026-11-17): connect, backup, keyboard and Bluetooth updates, restore, both recovery buttons. Installing over PFU firmware is untested; it needs a keyboard on stock firmware.

- **Vial release, hand checks.** Fn+Ctrl+V and usevia.app's HHKB Settings menu in VIA mode (needs key presses); Vial on the R3S (built, not yet flashed; its record sits at `0x300`). The Hybrid passes `vial_check.py`, `via_keymap_check.py` and `eeprom_save_test.py` (2026-11-18) and was in daily use with Vial on 2026-11-18. Tap dance and combo timing haven't been checked by hand.

- **ANSI Fn + A / S: volume keys swapped against PFU.** PFU's Classic manual (P3PC-6661) and the JIS manuals give Fn + A = Volume Down, Fn + S = Volume Up, Fn + F = Eject; ECK's ANSI keymap has A = Up, S = Down, F unassigned. Changing it changes the ANSI binaries (a release) and `docs/layers.svg`.


## Deferred

- **USB VID/PID.** The firmware uses the test ID `CAFE:4010`. Replace it with a real ID (pid.codes suggested); the owner approves the exact ID. The tools find the keyboard by "HHKB" in the product string, so they keep working.

## Hybrid open items

- **Hand test not done:** Rapid Trigger. Tested (2026-11-17): slots 1 and 2 in daily use (slot 1 survives pairing slot 2), Fn+X cancel (now Fn+Ctrl+X), DIP SW1/SW3/SW5 (owner's settings); SW2, SW4, SW6 checked by code review and `tests/test_dip_modes.c` / `test_hybrid_power.c` only. LESC passkey pairing with a Mac (slot 4, 2026-11-16).
- **nRF52 P0.11/P0.12** (stock inputs with pull-ups): not traced. Other unknowns: [Hybrid hardware](hybrid-hardware.md#7-open-questions).
- **HSI48 start fix:** matches ST's HAL; Bank 1 leaves HSI48 running on the Hybrid, so verify on a Classic.
- **Link version:** the STM32 never checks `BLE_LINK_VERSION`. Left so: the `.hfb` always packs matching firmware, and refusing a mismatched nRF52 would block the update that fixes it.

## HHKB JIS (69 keys)

On branch `jis` (2026-11-19), from PFU's JIS images (its `docs/models.md#jis`); builds only, no JIS keyboard has run it. Open:

- **First run on a JIS keyboard:** every key's position (`ECK_PRODUCT=JIS key_depth.py`, or typing), rest and noise levels with the fifth row, scan time (`key_depth.py scantime`; estimated ~510 µs per interrupts-off half, which may need a third unmasked break) and whether 1000 Hz keeps up. Then release the JIS images and add them to the web flasher (its recovery paths assume the ANSI Hybrid image; `deviceModel` and the `0x4010` filter need the JIS models and `0x4012`).
- **Bluetooth on the Hybrid JIS:** HID country code 0 and product ID `0x4010`, from the shared nRF52 app; PFU's JIS nRF52 image uses country code 15. Needs a JIS build of the nRF52 app or a runtime setting.
- **Pictures:** `tools/gen_layer_svg.py` draws the ANSI layers and DIP switches only.

## Ideas from aroum/dmk (reviewed 2026-11-19, up to `5aea674`)

- **Done on branch `hhkb-updates`, not yet on the keyboard:** 50 Hz scanning on every model while the host has suspended USB (DMK cuts sensor power instead, with power saving only, which would stop Bluetooth on a USB charger and the Fn+Ctrl chords), a guarded **Reset all settings** in VIA, and the settings review: fixed scan rates, no oversampling or tap-preferred, Debug toggles moved to `tools/debug_channels.py` (owner, 2026-11-19). Flashed on the owner's Hybrid (2026-11-19): `eeprom_save_test.py`, `via_keymap_check.py`, `vial_check.py` and `settings_reset_check.py` pass. Left: the reset from usevia.app (menu shows), a key press waking the Mac with SW6 ON, typing right after the Mac wakes (the 50 Hz stage itself can't be watched over a suspended USB link; a USB power meter would show the drop).
- Not planned: the per-key travel curve (user-measured ADC points at 1/2/3 mm; adds work to the scan's hot path), Le Capybara board and WS2812 drivers (other hardware). DMK's scan-loop optimisations from 2026-11-15 are already in ECK.

## Realforce R3S (wired TKL) port

Started 2026-11-17 on branch `per-board` (HHKB code moved into `stm32/boards/hhkb/`, binaries unchanged). Known: STM32L072RBT6 (LQFP64) and two analog muxes, as on the HHKB; Topre's `R3SD_verA011.rfb` uses the `.hfb` container (same two CRC-16/MCRF4XX, 64 KB Bank 2 image linked at `0x08010000`) with a different 32-byte trailer: 12 × `0xFF`, model ID `R3SD####`, version bytes `0a 00 01 01` (A011), 4 × `0xFF`. Stock firmware answers the PFU updater query (`AA AA 02`: model `R3SD13`, version A011) under VID `0x0853`, PID `0x0313`, and accepts E0-E3 updates.

Backed up 2026-11-17 (EEPROM, Bank 1, Bank 2, option bytes; owner's `backups/r3s-stock-*`) with `tools/r3s/`: a temporary Bank 2 image installed through Topre's updater starts ST's DFU loader (the Hybrid's ROM entry matched), then the stock image went back over DFU. Readout protection is level 0. Topre's Bank 2 tag is the word at EEPROM `0x17E0` (CRC-16/MCRF4XX of the 64 KB image in bytes 2-3; the updater also wrote `01 00 02 00` after it, meaning unknown); `0x340-0x37F` is unused, unlike the HHKB. Other EEPROM data sits in `0x000-0x05F`, `0x500-0xBFF` and `0x17C0`.

Matrix, key order, scan pins and the scan sequence were read from the stock image (2026-11-17): [R3S hardware](r3s-hardware.md). A bring-up build runs and types (2026-11-17): per-key rest measured at start-up, 500 Hz.

VIA checked on the keyboard (2026-11-17): keycode round trips and keymap reset (`via_keymap_check.py`), settings and keymap persisting across a restart with Topre's EEPROM data untouched (`tools/r3s/persist_check.py`). Next: daily-use test by the owner, including the VIA web app with `stm32/boards/r3s/via.json`. `BOARD_USB_RECOVERY_DFU` stays (owner, 2026-11-17: the R3S has no recovery switch); caveat: a host that doesn't enumerate within 15 s (asleep, KVM on another port, power-only charger) leaves the R3S in DFU until replugged. To revisit: hold Esc while plugging in to enter DFU instead (owner prefers it; works whenever scanning works). Scan: 500 Hz (~1.59 ms), enough for the owner; a press session after the per-column change was skipped (rest levels moved under 8%). Later: indicator colour and brightness in VIA (now red at 20%, fixed), the roles of PC5/PC11/PA1, Topre's `0x500-0xBFF` EEPROM data, SWD pads.

## Considered, not planned

- **Bare metal instead of FreeRTOS** (2026-11-16): saves ~4.3 KB flash and ~3.5 KB RAM, but rewrites the task structure, tickless STOP sleep and Bluetooth link waits, all tested on hardware. Revisit only if RAM runs out.
