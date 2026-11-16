# Deferred and open items

Work that is decided but not done yet, or waiting on a decision. Update this file when an item is done (delete it) or added.

## Waiting on the owner

- **Web flasher on hardware.** `web/` is written but has not been run on a keyboard. Serve it locally (`cd web && python3 -m http.server`, then open it in Chrome or Edge) and try first install, update and backup. Agent sandboxes cannot serve localhost.
- **Public home for the web flasher.** A `v*` tag builds a GitHub release with the three `.hfb` files, the boot stub, and `eck-web-flasher-<tag>.zip` (the page with those images bundled, so users need no local files). The page still needs a public host: the repo is private, and GitHub Pages on a private repo needs a paid plan. Decision (2026-11-16): release only for now, pick a host later (a separate public repo with Pages, or making ECK public).

## Deferred

- **USB VID/PID.** The firmware uses the test ID `CAFE:4010`. Replace it with a real ID (pid.codes suggested); the exact ID needs the owner's approval. `hybrid_update.py`, the bench tools and `web/` find the keyboard by "HHKB" in the product string, so they keep working.
- **USB hardware-in-the-loop tests.** Run a test set against the bench keyboard over USB (flash, VIA round trips, EEPROM persistence) in one command, e.g. before tagging a release. Pieces exist in `tools/bench/` (`eeprom_save_test.py`, `via_keymap_check.py`, `key_depth.py scantime`/`noise`).

## Held in reserve (size/performance review, 2026-11-16)

The review's other items are done (STM32 Hybrid now 43,016 B flash, Classic/Type-S ~34.4 KB; nRF52 app 45,756 B). Not adopted, for when flash or scan time gets tight:

- **Link-time optimisation:** about -3.9 KB, but it rewrites the scan loop, so rest levels move: re-measure `noise` and the per-key rest readings (`key_depth.py cal`).
- **Default keymap layers 2-3** repeat layers 0-1 except one key; generating them at start-up saves ~450 B but makes `stm32/board/config.h` harder to read.
- **Scan restructure:** read all keys with interrupts off, decide presses after; cuts the interrupts-off time further but is a larger, riskier change.
