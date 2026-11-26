[ECK docs](overview.md)

# Bench tools and measurements

How the owner's bench keyboards are flashed and checked over USB, and the reference numbers. Bench rules (what needs the owner's go, safety) are in AGENTS.md.

## Hybrid: flash and back up

- Flash: `uv run -q --with hidapi python3 tools/hybrid_update.py stm32 build/hybrid/hhkb_ec_hybrid.bin` (~20-30 s), `nrf <.hfb>` (~40 s + 10 s install).
- Back up before risky flashes: `hybrid_update.py backup backups/<name>/` (git-ignored).
- `restore-eeprom` writes changed 128-byte blocks and verifies by readback, because the ROM DFU loader fails dfu-util's status poll on EEPROM writes that still complete. It keeps the chip's boot tag (`0x08080360`/`0x370`): an old tag makes the boot stub refuse the installed Bank 2 and stay in DFU (fix: rewrite Bank 2 with `stm32`).
- Left in ST's DFU loader the keyboard is safe: `dfu-util -l` shows `0483:df11`, and `hybrid_update.py stm32` / `restore-eeprom` work from there.

## Release check

`tools/bench/release_check.py --perf --nrf`: ~2.5 min (150 s on 2026-11-19), lights the orange LED during the nRF52 steps. Without the nRF5 SDK, pass CI's Hybrid `.hfb` with `--hfb`; the diagnostics build is flashed as a `.bin`. Full output: `release_check.log` in the run's backup folder.

## Checks without keypresses

`scantime`, `noise` and `stacks` need a `-d HHKB_ADC_DIAGNOSTICS` build.

| Command | Checks | Baseline |
| --- | --- | --- |
| `tools/bench/key_depth.py scantime 5` | longest interrupts-off half; longest whole scan and missed 1 kHz slots also need `HHKB_SCAN_DIAGNOSTICS` | 389 µs half, ~873 µs whole scan (1 kHz, USB; v0.7.1, was 408/910) |
| `key_depth.py noise 8` | rest noise | median p-p 14-15.5 counts (2026-11-23: v0.7.0 14.0, v0.7.2 15.0-15.5 same day; was 12-13). A key's range is ~2300 counts, so 1 count is ~0.002 mm on the 4 mm scale (0.1 mm setting step ~57 counts) |
| `key_depth.py cal` | per-key rest readings (average several runs) | |
| `tools/bench/eeprom_save_test.py` | settings, Vial and keymap writers persist across resets | |
| `tools/bench/via_keymap_check.py` | VIA keycode round trips and keymap reset; restores the keymap | |
| `tools/bench/settings_reset_check.py BACKUP_DIR` | VIA Reset all settings, then restores that backup's EEPROM (back up just before) | |
| `key_depth.py stacks [SEC]` | each task stack's and the interrupt stack's peak use since reset | below |
| `tools/bench/ble_raw_check.py --cold 12` | VIA over Bluetooth (Hybrid, needs the Mac's Bluetooth link): round trips, then first requests after a pause | median 30 ms; first 85-387 ms on the Mac (latency 22 x 15 ms), Vial allows 500 (2026-11-26) |

EEPROM before/after comparisons must skip the settings record's CRC and calibration arrays (offsets in `eeprom_save_test.py`): every save rewrites live calibration rest levels.

## Stack sizes

Measured peaks (2026-11-17, after VIA checks, an nRF52 reinstall and 3 min of typing): matrix 320/1024, keyboard 304/1536, usbd 460/2048, ble 356/544, idle 80/512, interrupts 424 bytes. Peaks cover only the code paths that ran (hold-taps weren't typed).

For worst cases use `tools/stack_depth.py` (static call-graph depth; usage in its header, pass TinyUSB's class callbacks with `--indirect`) and add ~90 B for the saved context and libgcc/libc leaves. v0.4.4 sizes stacks from these. Static worst cases (2026-11-17): keyboard 688 B (on batteries it runs the wake scan, plus the scan's debug printf if a VIA debug flag is on; 472 B without it), matrix 560, usbd 552, ble 312, idle 64. The USB-only bench never runs the battery wake scan, so measured peaks understated the keyboard task by ~2x.

Interrupt stack (`_Min_Stack_Size`, 1 KB since 2026-11-21, was 2 KB): static worst case ~720 B on every board (reviewed 2026-11-21). The CM0 port never resets it, so `main()` down to the first task (~140 B; ~200 B during start-up) stays on it, under nested lowest-priority (TIM2/SysTick/PendSV, 84 B), USB (300 B), priority-0 (44 B) and HardFault (44 B) handlers, 36 B frame each.

## Scan speed and key readings

Post-read work overruns the 2 µs settle window, so scan code speed moves rest levels. Example: trimming the per-key scan work (435 -> 407 µs) lowered rest readings ~16 counts with noise unchanged. `-O2` globally was evaluated (2026-11-16) and isn't worth it; the hot functions already carry `optimize("O2")` in RAM. ADC 4x oversampling cut rest noise only 2% (sd 2.81 to 2.76 counts) but raised rest readings ~31 counts (2026-11-19, why it was removed).

## nRF52 without pairing

Restart it into recovery mode and back over VIA (`hybrid_update.py`'s `nrf_recovery`, then `UPD_RESTART`); the recovery flag proves the app reaches `main()` and gets SPI interrupts through the SoftDevice. It doesn't prove the BLE stack starts: that needs a pairing (slot 4 on the owner's Mac; pairing deletes that slot's bond). The bench Mac is paired on slot 1 (since 2026-11-26); with Bluetooth selected it types over Bluetooth even while USB is plugged in. A `--recovery` run restarts the nRF52 twice, each blinking the orange LED (2/s) for up to 30 s.

S132 v7.2.0 under PFU's MBR 2.2.2 was tested on 2026-11-16 (boot, recovery mode, LESC pairing, an SDK 15 bond reconnecting).

## R3S

- `tools/r3s/r3s_backup.py flash <.rfb>` works from stock firmware (Topre's updater), ECK (VIA jump to ST's DFU loader) or DFU; `read DIR` backs up EEPROM and both banks from DFU.
- Bench tools take `ECK_PRODUCT=R3S` (`key_depth.py`, `via_keymap_check.py`). Not `eeprom_save_test.py`, which reads EEPROM through `hybrid_update.py`: use `tools/r3s/persist_check.py`.
- `tools/r3s/caps_lock.py on|off` sets the Mac's Caps Lock to check indicators without key presses (turn it off after).
- Web flasher: `tools/web_bench/bench.mjs install|update|backup|restore|guards` runs `web/eck.js` against the R3S through WebHID/WebUSB stand-ins (`npm install` in `tools/web_bench/` once). A browser needs a click per device dialog, so agents can't drive the page; this found Topre's model string bug (2026-11-20).
