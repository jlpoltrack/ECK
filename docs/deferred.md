# Deferred and open items

Work that is decided but not done yet, or waiting on a decision. Update this file when an item is done (delete it) or added.

## Waiting on the owner

- **Web flasher on hardware.** `web/` is written but has not been run on a keyboard. Serve it locally (`cd web && python3 -m http.server`, then open it in Chrome or Edge) and try first install, update and backup. Agent sandboxes cannot serve localhost.
- **Public home for the web flasher.** A `v*` tag builds a GitHub release with the three `.hfb` files, the boot stub, and `eck-web-flasher-<tag>.zip` (the page with those images bundled, so users need no local files). The page still needs a public host: the repo is private, and GitHub Pages on a private repo needs a paid plan. Decision (2026-11-16): release only for now, pick a host later (a separate public repo with Pages, or making ECK public).

## Deferred

- **USB VID/PID.** The firmware uses the test ID `CAFE:4010`. Replace it with a real ID (pid.codes suggested); the exact ID needs the owner's approval. `hybrid_update.py`, the bench tools and `web/` find the keyboard by "HHKB" in the product string, so they keep working.
- **USB hardware-in-the-loop tests.** Run a test set against the bench keyboard over USB (flash, VIA round trips, EEPROM persistence) in one command, e.g. before tagging a release. Pieces exist in `tools/bench/` (`eeprom_save_test.py`, `via_keymap_check.py`, `key_depth.py scantime`/`noise`).

## Size and performance review (2026-11-16), not yet adopted

Measured on the 2026-11-16 build: STM32 Hybrid 43,988 B flash (67%), ~17.9 KB of 20 KB RAM; Classic/Type-S 35,380 B; nRF52 app 54,980 B of its 64 KB slot (84%).

- **nRF52 release build:** `build.sh` builds the nRF52 app with RTT logging (unreachable on an assembled keyboard). `RELEASE=1` (`nrf52/Makefile`) takes it to 45,756 B (-9.2 KB) and -2.1 KB RAM. Needs an nRF52 flash and recovery check.
- **Settings load:** `stm32/ec/ec_settings.c` repeats the Rapid Trigger checks and the baseline/peak loop three times; one helper saves ~368 B and ~23 lines. Check with `eeprom_save_test.py` after a backup.
- **Dead code:** about 150 lines of uncalled functions and `#ifdef`s on always-set options (`MATRIX_REPORT_BATCH`, `HHKB_SCAN_1KHZ`, `DEBUG_CONSOLE_ENABLE`, the TinyUSB high-speed block). Prove with `tools/build_hashes.sh` (binaries unchanged).
- **FreeRTOS:** mutexes, queue registry and dynamic allocation are unused; turning them off drops `heap_4.c` and its test (~750 lines, -224 B).
- **Tools:** `tools/bench/swd_flash.py` imports from a missing `tools/hhkb` folder (broken); CRC/container code duplicated between `make_hfb.py` and `hhkb_flasher.py`; the VIA open/send helper exists four times; `web/eck.test.mjs` is not run by the test script or CI.
- **Scan time (needs a `noise` re-measure):** inlining `rt_key_reset` in the RAM scan loop and dropping the dead counted-loop fallback in `classic_matrix.c`'s delay helpers would save ~100 µs per scan (estimated). Link-time optimisation saves ~3.9 KB but rewrites the scan loop; keep in reserve.
