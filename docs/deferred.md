# Open items

Everything not done yet, by what it waits on. Delete an item when it's done. Reviewed for every release (up to v0.7.2). HHKB JIS ships as experimental until a JIS keyboard has run it (owner, 2026-11-23; [JIS](models.md#jis)).

## Planned work

- **R3S recovery check before the scheduler starts** (review, 2026-11-21): reading Esc and Caps Lock in `hhkb_matrix_hardware_init` would cover a build that faults after start-up; today the check needs the scheduler, 24 scans and a keyboard-task tick (as the 15 s timeout before it needed the scheduler).

## Checks that need hands, a browser or other hardware

- **Web flasher first install.** Everything else passed on the owner's Hybrid (2026-11-17). Installing over PFU firmware needs a keyboard on stock firmware.
- **Web flasher R3S tab in Chrome.** Its `eck.js` flows passed on the owner's R3S from Node (`tools/web_bench/bench.mjs`, 2026-11-20); the page's buttons, device dialogs and Recover firmware haven't been run in a browser.
- **usevia.app in VIA mode:** the HHKB Settings menu shows, including Reset all settings (`settings_reset_check.py` passes over USB, 2026-11-19), and the R3S's indicator colour and brightness. Without the browser, VIA's definition parser (`@the-via/reader`, npm) could check the `via.json` files; installing it needs the owner's permission.
- **By hand on the Hybrid:** tap dance and combo timing; Rapid Trigger; DIP SW2, SW4 and SW6 (code review and host tests only); a key press waking the Mac with SW6 ON, and typing right after it wakes (the 50 Hz stage can't be watched over a suspended USB link; a USB power meter would show the drop).
- **Bluetooth, v0.7.2, needs other hosts:** Windows Swift Pair's pop-up and whether Windows, Linux or Android grant the 7.5 ms interval (VIA shows it; a Mac keeps 15 ms, latency 22, 2 s, tested 2026-11-23); the 5 orange blinks when a host rejects the keys (needs a host that lost its bond, e.g. Windows after "Remove device" while the keyboard keeps its bond). Reconnect after OFF tested: 0.74 s from USB enumeration (`tools/bench/ble_reconnect.py`).
- **Web flasher JIS** (v0.7.2): the JIS detection and layout check are host-tested (`web/eck.test.mjs`); a JIS keyboard has never run it.
- **JIS first run** (needs a JIS keyboard): every key's position (`ECK_PRODUCT=JIS key_depth.py`, or typing), and rest and noise levels with the fifth row. Scan timing is measured (on the ANSI Hybrid, 2026-11-21).

## Hardware unknowns

- **Hybrid:** what PA10 and PC9 connect to (stock never reads PA10 and holds PC9 low, so probably nothing that matters); [Hybrid hardware](hybrid-hardware.md#7-open-questions).
- **R3S:** the roles of PC5/PC11/PA1, a meter check of the SWD pads (marked from the owner's photo in [R3S hardware](r3s-hardware.md)), and the meaning of Topre's `0x600-0x6FF` EEPROM records (stock copies EEPROM `0x000-0xBFF` into RAM structures at start-up; the rest is mapped in [R3S hardware](r3s-hardware.md)).

## Not planned

- **ANSI Fn + A / S order:** the ANSI keymap keeps A = Volume Up, S = Volume Down; only the JIS keymap follows PFU's manuals (A = Down, S = Up, F = Eject) (owner, 2026-11-21).
- **USB VID/PID:** the test IDs stay (`CAFE:4010` HHKB, `4011` R3S, `4012` JIS; owner, 2026-11-21). The tools find keyboards by product string.
- **nRF52 link version check:** the STM32 never checks `BLE_LINK_VERSION`. The `.hfb` always packs matching firmware, and refusing a mismatched nRF52 would block the update that fixes it.
