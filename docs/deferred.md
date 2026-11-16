# Deferred and open items

Work that is decided but not done yet, or waiting on a decision. Update this file when an item is done (delete it) or added.

## Waiting on the owner

- **Web flasher on hardware.** `web/` has not been run on a keyboard yet. Unzip `eck-web-flasher-v0.2.0.zip` from the latest release (it bundles that release's images), run `python3 -m http.server` in that folder, open it in Chrome or Edge, and try first install, update and backup. Agent sandboxes cannot serve localhost.
- **Public home for the web flasher.** A `v*` tag builds a GitHub release with the three `.hfb` files, the boot stub, and `eck-web-flasher-<tag>.zip` (the page with those images bundled, so users need no local files). The page still needs a public host: the repo is private, and GitHub Pages on a private repo needs a paid plan. Decision (2026-11-16): release only for now, pick a host later (a separate public repo with Pages, or making ECK public).

## Deferred

- **USB VID/PID.** The firmware uses the test ID `CAFE:4010`. Replace it with a real ID (pid.codes suggested); the exact ID needs the owner's approval. `hybrid_update.py`, the bench tools and `web/` find the keyboard by "HHKB" in the product string, so they keep working.
- **Measure the full scan time.** `key_depth.py scantime` reports only the longer interrupts-off half (columns 0-7, 32 keys: 407 µs since v0.2.0); the full scan adds the 28-key half and the work between, roughly 770-800 µs (estimated, not measured). A `-d HHKB_SCAN_DIAGNOSTICS` build records the whole scan in `matrix.c`; add a VIA readout for it to `key_depth.py`.
- **USB hardware-in-the-loop tests.** Run a test set against the bench keyboard over USB (flash, VIA round trips, EEPROM persistence) in one command, e.g. before tagging a release. Pieces exist in `tools/bench/` (`eeprom_save_test.py`, `via_keymap_check.py`, `key_depth.py scantime`/`noise`).

## Held in reserve (size/performance review, 2026-11-16)

The review's other items are done (STM32 Hybrid `.bin` now 43,132 B, ~3.1 KB RAM free; Classic/Type-S 34,556 B; nRF52 app 45,756 B). Not adopted, for when flash or scan time gets tight. (The EEPROM analysis's 16-bit keymap slots are done: macros are 512 B since 2026-11-16, 756 B of ECK's EEPROM is still free.)

- **Link-time optimisation:** about -3.9 KB, but it rewrites the scan loop, so rest levels move: re-measure `noise` and the per-key rest readings (`key_depth.py cal`).
- **Default keymap layers 2-3** repeat layers 0-1 except one key; generating them at start-up saves ~450 B but makes `stm32/board/config.h` harder to read.
- **Scan restructure:** read each half's keys with interrupts off but decide presses (Rapid Trigger, thresholds) after re-enabling them; cuts the interrupts-off time further but is a larger, riskier change.

## Considered, not planned

- **Bare metal instead of FreeRTOS** (2026-11-16). It would save about 4.3 KB of flash (kernel code) and about 3.5 KB of RAM (five task stacks, ~5.6 KB, become one). Neither is short: flash is at 66% and RAM has ~3.1 KB free. It would mean rewriting the task structure, the Hybrid's tickless STOP sleep, and the Bluetooth link waits, all of which are tested on hardware. Revisit only if RAM runs out.
