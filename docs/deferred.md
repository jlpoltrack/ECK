# Deferred and open items

Decided but not done, or waiting on a decision. Delete an item when it's done.

## Waiting on the owner

- **Web flasher first install.** Everything else passed on the owner's Hybrid (2026-11-17): connect, backup, keyboard and Bluetooth updates, restore, both recovery buttons. Installing over PFU firmware is untested; it needs a keyboard on stock firmware.

## Deferred

- **USB VID/PID.** The firmware uses the test ID `CAFE:4010`. Replace it with a real ID (pid.codes suggested); the owner approves the exact ID. The tools find the keyboard by "HHKB" in the product string, so they keep working.
- **USB hardware-in-the-loop tests.** One command that runs the bench checks over USB (flash, VIA round trips, EEPROM persistence), e.g. before a release. The pieces are in `tools/bench/` (`eeprom_save_test.py`, `via_keymap_check.py`, `key_depth.py scantime`/`noise`).

## Hybrid open items

- **Hand tests not done:** Rapid Trigger, a second host in slot 2 (slot 1 must survive), Fn+X cancel, DIP switch modes. LESC passkey pairing works with a Mac (slot 4, 2026-11-16).
- **nRF52 P0.11/P0.12** (stock inputs with pull-ups): not traced. Other unknowns: [Hybrid hardware](hybrid-hardware.md#7-open-questions).
- **HSI48 start fix:** matches ST's HAL; Bank 1 leaves HSI48 running on the Hybrid, so verify on a Classic.
- **Link version:** the STM32 never checks `BLE_LINK_VERSION`. Left so: the `.hfb` always packs matching firmware, and refusing a mismatched nRF52 would block the update that fixes it.

## Considered, not planned

- **Bare metal instead of FreeRTOS** (2026-11-16): saves ~4.3 KB flash and ~3.5 KB RAM, but rewrites the task structure, tickless STOP sleep and Bluetooth link waits, all tested on hardware. Revisit only if RAM runs out.
