# Deferred and open items

Decided but not done, or waiting on a decision. Delete an item when it's done.

## Waiting on the owner

- **Web flasher on hardware.** Not yet run on a keyboard. Open <https://jlpoltrack.github.io/ECK/> (or serve a release's `eck-web-flasher-<tag>.zip` with `python3 -m http.server`) in Chrome or Edge and try first install, update and backup. Agent sandboxes can't serve localhost.

## Deferred

- **USB VID/PID.** The firmware uses the test ID `CAFE:4010`. Replace it with a real ID (pid.codes suggested); the owner approves the exact ID. The tools find the keyboard by "HHKB" in the product string, so they keep working.
- **USB hardware-in-the-loop tests.** One command that runs the bench checks over USB (flash, VIA round trips, EEPROM persistence), e.g. before a release. The pieces are in `tools/bench/` (`eeprom_save_test.py`, `via_keymap_check.py`, `key_depth.py scantime`/`noise`).

## Hybrid open items

- **Hand tests not done:** Rapid Trigger, a second host in slot 2 (slot 1 must survive), Fn+X cancel, DIP switch modes. LESC passkey pairing works with a Mac (slot 4, 2026-11-16).
- **Bluetooth while on USB:** ECK keeps the connection up so Fn+Ctrl+1 switches at once; dropping it is the owner's call.
- **nRF52 P0.11/P0.12** (stock inputs with pull-ups): not traced. Other unknowns: [Hybrid hardware](hybrid-hardware.md#7-open-questions).
- **HSI48 start fix:** matches ST's HAL; Bank 1 leaves HSI48 running on the Hybrid, so verify on a Classic.
- **Link version:** the STM32 never checks `BLE_LINK_VERSION`. Left so: the `.hfb` always packs matching firmware, and refusing a mismatched nRF52 would block the update that fixes it.

## Held in reserve (size/performance review, 2026-11-16)

For when flash or scan time gets tight. At v0.4.0: 756 B of ECK's EEPROM free; ~3.1 KB RAM free on the Hybrid; nRF52 app 45,124 B. At v0.3.0: longest interrupts-off scan half 408 µs and longest whole scan ~910 µs of the 1,000 µs slot at 1 kHz.

- **Link-time optimisation:** about -3.9 KB, but it rewrites the scan loop, so rest levels move: re-measure `noise` and per-key rest readings (`key_depth.py cal`).
- **Default keymap layers 2-3** repeat layers 0-1 except one key; generating them at start-up saves ~450 B but makes `stm32/board/config.h` harder to read.
- **Scan restructure:** read each half's keys with interrupts off, decide presses after re-enabling them; less interrupts-off time, but a larger, riskier change.

## Considered, not planned

- **Bare metal instead of FreeRTOS** (2026-11-16): saves ~4.3 KB flash and ~3.5 KB RAM, but rewrites the task structure, tickless STOP sleep and Bluetooth link waits, all tested on hardware. Revisit only if RAM runs out.
