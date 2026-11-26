# Open items

Everything not done yet, by what it waits on. Delete an item when it's done. Reviewed for every release (up to v0.9.0). HHKB JIS ships as experimental until a JIS keyboard has run it (owner, 2026-11-23; [JIS](models.md#jis)).

## Planned work

- **R3S recovery check before the scheduler starts** (review, 2026-11-21): reading Esc and Caps Lock in `hhkb_matrix_hardware_init` would cover a build that faults after start-up; today the check needs the scheduler, 24 scans and a keyboard-task tick (as the 15 s timeout before it needed the scheduler).
- **Vial desktop over Bluetooth** (owner's call): it finds keyboards by the `vial:f64c2b3c-` serial, which reads empty over Bluetooth. A Device Information Service serial might reach hidapi (untested), but inserting it moves the GATT handles, so every paired host would have to re-pair once ([VIA over Bluetooth](hybrid-port.md#bluetooth)).
- **A bond's stored GATT data that no longer applies is silent** (review, 2026-11-26): `nrf52/main.c` ignores `PM_EVT_LOCAL_DB_CACHE_APPLY_FAILED`, so a host left unsubscribed after a GATT change looks connected but gets no keys. Showing it (e.g. the host-rejected blinks) would make that failure visible. Enabling Service Changed stopped Bluetooth typing on the Mac even after re-pairing (cause unproven).

## Checks that need hands, a browser or other hardware

- **Web flasher first install.** Everything else passed on the owner's Hybrid (2026-11-17). Installing over PFU firmware needs a keyboard on stock firmware.
- **Web flasher R3S tab in Chrome.** Its `eck.js` flows passed on the owner's R3S from Node (`tools/web_bench/bench.mjs`, 2026-11-20); the page's buttons, device dialogs and Recover firmware haven't been run in a browser.
- **usevia.app in VIA mode:** the HHKB Settings menu shows, including Reset all settings (`settings_reset_check.py` passes over USB, 2026-11-19), and the R3S's indicator colour and brightness. Without the browser, VIA's definition parser (`@the-via/reader`, npm) could check the `via.json` files; installing it needs the owner's permission.
- **By hand on the Hybrid:** tap dance and combo timing; Rapid Trigger; DIP SW2, SW4 and SW6 (code review and host tests only); a key press waking the Mac with SW6 ON, and typing right after it wakes (the 50 Hz stage can't be watched over a suspended USB link; a USB power meter would show the drop).
- **Bluetooth, v0.7.2, needs other hosts:** Windows Swift Pair's pop-up; whether Windows, Linux or Android grant the 7.5 ms interval, and whether an iPhone or iPad accepts the request or keeps its first timing (7.5 ms is below Apple's 15 ms guideline; owner kept it, 2026-11-23) (VIA shows it; a Mac keeps 15 ms, latency 22, 2 s, tested 2026-11-23); the 5 orange blinks when a host rejects the keys (needs a host that lost its bond, e.g. Windows after "Remove device" while the keyboard keeps its bond). Reconnect after OFF tested: 0.74 s from USB enumeration (`tools/bench/ble_reconnect.py`).
- **VIA over Bluetooth on other hosts** (vial.rocks and usevia.app worked on the owner's Mac, 2026-11-26): Windows and Linux. Its battery cost (STM32 polls the nRF52 every 50 ms while a host is connected; estimated 10-30 µA on the 1.5 mA idle stage) needs a PPK2. Vial's 500 ms limit on a host that grants latency 30 at 15 ms (a first request could reply late, then shift later replies by one).
- **Bluetooth updates, untested paths** (v0.9.0): a host bonded without the passkey being refused VIA and updates; the fallback to the VIA channel (a host that can't write 244-byte reports); an abort mid-pull ending the STM32's pull; the low-battery refusal of Fn+Ctrl+U (needs batteries at level 2-3); Windows and Linux hosts (Windows' `hid_write` may use unacknowledged writes).
- **Empty battery while on** (v0.7.4): the 60 s shutdown at level 3 is host-tested only; checking it needs a supply lowered below ~1.94 V at the battery terminals, without USB.
- **Web flasher JIS** (v0.7.2): the JIS detection and layout check are host-tested (`web/eck.test.mjs`); a JIS keyboard has never run it.
- **JIS first run** (needs a JIS keyboard): every key's position (`ECK_PRODUCT=JIS key_depth.py`, or typing), and rest and noise levels with the fifth row. Scan timing is measured (on the ANSI Hybrid, 2026-11-21).

## Hardware unknowns

- **Hybrid:** what PA10 and PC9 connect to (stock never reads PA10 and holds PC9 low, so probably nothing that matters); [Hybrid hardware](hybrid-hardware.md#7-open-questions).
- **R3S:** the roles of PC5/PC11/PA1, a meter check of the SWD pads (marked from the owner's photo in [R3S hardware](r3s-hardware.md)), and the meaning of Topre's `0x600-0x6FF` EEPROM records (stock copies EEPROM `0x000-0xBFF` into RAM structures at start-up; the rest is mapped in [R3S hardware](r3s-hardware.md)).

## Not planned

- **ANSI Fn + A / S order:** the ANSI keymap keeps A = Volume Up, S = Volume Down; only the JIS keymap follows PFU's manuals (A = Down, S = Up, F = Eject) (owner, 2026-11-21).
- **USB VID/PID:** the test IDs stay (`CAFE:4010` HHKB, `4011` R3S, `4012` JIS; owner, 2026-11-21). The tools find keyboards by product string.
- **nRF52 link version check:** the STM32 never checks `BLE_LINK_VERSION`. The `.hfb` always packs matching firmware, and refusing a mismatched nRF52 would block the update that fixes it.
