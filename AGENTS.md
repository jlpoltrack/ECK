# Agent guide: ECK

Rules and lessons for AI agents (and people) working in this repo. Layout and build basics are in [README.md](README.md); details in [docs/](docs/). Each rule states its scope and reason; delete a rule when its reason no longer holds.

## Scope and ownership

- **Keep the Classic and Hybrid code paths as alike as possible:** share code and behaviour, and keep model differences to what the hardware forces (e.g. the Classic has no power switch, so SW6 is its recovery). Reason: owner's rule (2026-11-17); the Classic can't be tested, so shared paths are what keeps it working.
- **Hybrid is tested; Classic and Type-S are compile-only** (no hardware). Keep them building without warnings, and don't change their behaviour-only paths (e.g. the SW6 Bank 2 erase in `stm32/board/dip_switch.c`). Shared code may change for all three.
- **Keep `THIRD_PARTY_NOTICES.md` current** when the firmware links new third-party code (e.g. another nRF5 SDK library); CI attaches it to every release. Reason: Nordic's, ST's and SEGGER's licences require their notices with the binaries.
- **Commit as `41841496+jlpoltrack@users.noreply.github.com`** (set in this repo's git config). Reason: the repo is public-facing; the owner's real email was scrubbed from history on 2026-11-16.
- **VIA only**; Vial was removed, don't re-add it. Fresh defaults: factory thresholds, Rapid Trigger off.

## Never rename (persisted or matched by tools)

Reason: renaming orphans data already in keyboards' EEPROM, or breaks tool matching.

- EEPROM magics: `ECK3` keymap slot tag, `CMIT` commit word, `HKCx` settings records, settings words, the nRF52 bond slot tag. To change one, keep reading the old value and rewrite it, as `hhkb_keymap_storage.c` does for `ECK2` and the pre-ECK slot tags.
- "HHKB" in the USB product string: `tools/hybrid_update.py` and the bench tools find the keyboard by it.
- Code identifiers carry no project prefix; the name is ECK everywhere except the aroum/dmk credit.

## Build and test

- `./build.sh hybrid|classic|type_s [-c] [-d DEFS]`; bench build: `./build.sh hybrid -d HHKB_ADC_DIAGNOSTICS,HHKB_SCAN_DIAGNOSTICS`. The Hybrid `.hfb` needs the nRF5 SDK 17.1.1 at `../nRF5_SDK_17.1.1`; CI fetches it (SHA-256 pinned).
- `tests/run_all_tests.sh` runs the C and Python host tests. Host tests compile some modules against mocks in `tests/*_mocks/`: a new `#include` in such a module needs a mock header there.
- Builds must stay warning-free (`-Wall -Wextra`); fix causes, don't suppress.
- **One compiler: Arm GNU Toolchain 15.3.Rel2** on the bench and in CI (`ci.yml` pins its URL and SHA-256). Change both together, then re-measure `noise` and rest readings. Reason: releases come from CI, and compiler changes shift scan timing and so key readings.
- **Refactors must prove binaries unchanged:** `tools/build_hashes.sh <src> <out>` on a `git archive HEAD` copy and on the working tree; every `.bin` hash must match. (`.hfb` lines differ when only one tree has `nrf52/_build`.)
- **Every change to a shipped binary gets a version bump** (a new `vX.Y.Z` release); ask the owner when the bump level is unclear. Reason: owner's rule (2026-11-17), so the version a keyboard reports always identifies its firmware.
- **History: one commit per release.** Work between releases is committed normally; every version bump squashes all commits since the previous tag into that release's commit (subject `ECK vX.Y.Z: <summary>`), tag it, and force-push `main` and the tag. Back up first: `git bundle create backups/git-history/<date>.bundle --all`. Reason: the owner wants a short history. No `Co-Authored-By` trailers (owner's rule).

## Bench (the owner's Hybrid, USB only)

- The test keyboard is assembled: no SWD or PPK2. The owner types on it. Flash and measure over USB without asking; never ask for keypresses (if presses are truly needed, run the recorder ~40 s and tell the owner to start right away).
- Flash: `uv run -q --with hidapi python3 tools/hybrid_update.py stm32 build/hybrid/hhkb_ec_hybrid.bin` (~20-30 s), `nrf <.hfb>` (~40 s + 10 s install). Wrap USB/probe commands in `timeout`, and check any long command within 30 s.
- Before risky flashes (EEPROM layout, storage code): `hybrid_update.py backup backups/<name>/` (git-ignored).
- **Before tagging a release, run `tools/bench/release_check.py --perf --nrf`** (~6 min; lights the orange LED during the nRF52 steps). Tell the owner first not to use the keyboard until it finishes, and wait for their go. Reason: it covers the USB checks below in one run, and typing skews the measurements and failed the EEPROM check once (2026-11-17). Full output: `release_check.log` in the run's backup folder.
- Checks without keypresses: `tools/bench/key_depth.py scantime 5` (longest interrupts-off half, baseline 408 µs; longest whole scan ~910 µs and missed 1 kHz slots, which need `HHKB_SCAN_DIAGNOSTICS` too), `noise 8` (median p-p ~12-13 counts), `tools/bench/eeprom_save_test.py` (all three EEPROM writers persist across resets), `tools/bench/via_keymap_check.py` (VIA keycode round trips and keymap reset; restores the keymap). `scantime`/`noise` need a `-d HHKB_ADC_DIAGNOSTICS` build. The scan baselines are for 1 kHz (USB output): at 500 Hz and below (Bluetooth output defaults to 250 Hz) the oversampling setting applies, which raises the interrupts-off half to ~496 µs; `release_check.py` turns oversampling off while measuring.
- Most other `tools/bench/` tools need SWD (case open): ask first.
- **Never commit raw EEPROM dumps or print the macro area**: the owner's macro may be a password. Use `tools/scrub_eeprom_fixture.py` for fixtures; report only address ranges.

## Firmware rules

- **Hybrid timing never uses counted loops**; use SysTick delays. Reason: flash prefetch changed loop timing once and broke Bluetooth.
- Key readings shift with scan code speed (post-read work overruns the 2 µs settle window), so compiler-flag or hot-path changes move rest levels; re-measure `noise` and compare per-key rest readings (`key_depth.py cal`, average several runs) after such changes. Example: trimming the per-key scan work (435 -> 407 µs) lowered rest readings ~16 counts with noise unchanged. `-O2` globally was evaluated (2026-11-16) and isn't worth it; the hot functions already carry `optimize("O2")` in RAM.
- **Data EEPROM writes go through `stm32/board/eeprom.c`** (`eeprom_write_word`). Reason: writers in different tasks used to lock the EEPROM under each other. Exception: the Classic SW6 erase path, which runs before the scheduler.
- Unrecoverable errors call `platform_system_reset()` (faults, asserts, start-up failures). Reason: a hung Hybrid on batteries needs the batteries pulled.
- `hybrid_update.py stm32` refuses unless Bank 1 holds the boot stub (`--force` overrides). Reason: without it a failed write has no USB recovery.

## Learnings

- GNU make 4 (CI) passes "Entering directory" lines into the nRF5 SDK's link command; `build.sh` runs the nRF52 make with `--no-print-directory`. macOS make 3.81 doesn't show this, so nRF52 build changes need a CI run to be trusted.
- The settings record rewrites live calibration rest levels on every save, so EEPROM before/after comparisons must skip its CRC and calibration arrays (offsets in `tools/bench/eeprom_save_test.py`).
- **Never read a struct through a `uint32_t *` cast** (STM32 build has strict aliasing on): copy words out with `memcpy`. Reason: GCC dropped a record's byte/halfword stores once the word writes went out of line, and the keyboard saved a corrupted settings record (2026-11-16).
- `hybrid_update.py restore-eeprom` writes changed 128-byte blocks and verifies by readback, because the ROM DFU loader fails dfu-util's status poll on EEPROM writes that still complete. It keeps the chip's boot tag (`0x08080360`/`0x370`): an old tag makes the boot stub refuse the installed Bank 2 and stay in DFU (fix: rewrite Bank 2 with `stm32`).
- If the keyboard is left in ST's DFU loader, it is safe: `dfu-util -l` shows `0483:df11`, and `hybrid_update.py stm32` / `restore-eeprom` work from there.
- Bench scripts must close their HID handle before running `hybrid_update.py backup` (or another tool that opens the keyboard), or that tool fails.
- Agent sandboxes may refuse to build from a freshly downloaded SDK archive; validate such builds in CI instead.
- Recovery-mode testing: a `--recovery` run restarts the nRF52 twice, and each restart blinks the orange LED (2/s, "Bluetooth update") for up to 30 s. Tell the owner before running tests that light LEDs.
- To build while another job uses `build/`, build a `git archive` copy in a sibling folder (the nRF5 SDK must sit at `../`), then delete it. Reason: two builds in one `build/` tree clobber each other.
- **Keymaps in EEPROM hold internal 32-bit keycodes**, so never change what an existing value means: add new encodings in unused values or flag bits (K_NULL = 0x01, hold-tap `HT_MODS_FLAG` bit 23). Reason: saved keymaps would silently change behaviour.
- **Checking the nRF52 without pairing:** restart it into recovery mode and back over VIA (`hybrid_update.py`'s `nrf_recovery`, then `UPD_RESTART`); the recovery flag proves the app reaches `main()` and gets SPI interrupts through the SoftDevice. It doesn't prove the BLE stack starts: that needs a pairing (slot 4 on the owner's Mac; pairing deletes that slot's bond). Reason: the bench Mac isn't paired with the keyboard, so an empty slot stays silent in scans.
- **The `.hfb` never rewrites the nRF52 MBR** (`0x0-0xFFF`): any SoftDevice upgrade runs under PFU's MBR 2.2.2 and must keep the app at `0x26000`. S132 v7.2.0 under it was tested on 2026-11-16 (boot, recovery mode, LESC pairing, an SDK 15 bond reconnecting). Reason: PFU's bootloader copies only `0x1000-0x35FFF` and jumps to `0x26000`.
- **The USB serial number is the chip's unique ID; keep it constant across firmware versions.** Reason: Chrome ties WebHID permission to the serial (and Windows its driver binding), so the version-as-serial made the web flasher lose the keyboard after every update (2026-11-17). VIA setting 28 reports the version.
- **The power-switch recovery gesture needs the batteries out:** the boot stub reads the switch only at a reset, and with batteries in, a running keyboard doesn't reset when USB is plugged in (only OFF and deep sleep do). Reason: found testing the web flasher's recovery card (2026-11-17).
- Deferred and open items live in [docs/deferred.md](docs/deferred.md) only; update it when one is done or added.
- **Docs: one home per fact.** User guides link to the reference docs (`build.md` for the `.hfb` layout and CI, `hybrid-hardware.md` for pins, memory maps, power and SWD) instead of repeating them. Reason: the same facts sat in three or four files, and the docs ran to 19k words (cut to 11k, 2026-11-16).
