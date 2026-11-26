# Agent guide: ECK

Rules and lessons for AI agents (and people) working in this repo. Layout and build basics: [README.md](README.md); details: [docs/](docs/) (bench commands and numbers: [docs/bench.md](docs/bench.md)). Each rule states its scope and reason; delete a rule when its reason no longer holds.

## Scope and ownership

- **Keep Classic and Hybrid code paths as alike as possible;** differ only where hardware forces it (Classic has no power switch, so SW6 is its recovery). Reason: owner (2026-11-17); the Classic can't be tested, so shared paths keep it working.
- **Hybrid is tested; Classic, Type-S and JIS (`hybrid_jis`, `type_s_jis`, under `HHKB_JIS`) are compile-only.** Keep them warning-free and don't change their behaviour-only paths (e.g. SW6 Bank 2 erase in `stm32/boards/hhkb/dip_switch.c`). Shared code may change for all. JIS ships as experimental until a JIS keyboard runs it (owner, 2026-11-23).
- **Keep `THIRD_PARTY_NOTICES.md` current** when firmware links new third-party code. Reason: Nordic, ST and SEGGER licences require it; CI ships it with releases.
- **Commit as `41841496+jlpoltrack@users.noreply.github.com`** (`git config core.hooksPath tools/git_hooks`: `pre-push` refuses other emails). Reason: public-facing; real email scrubbed from history 2026-11-16.
- **Vial and VIA:** releases build with Vial on every board (`ECK_VIAL` default ON; `VIAL_ENABLE` kept so VIA-only builds work). Reports VIA protocol 9 for Vial; Fn+Ctrl+V (VIA mode until restart) reports 12 for usevia.app's HHKB Settings. Reason: owner (2026-11-18); Vial accepts only 9. Fresh defaults: factory thresholds, Rapid Trigger off.
- **Fixed behaviour beats settings when one value suits nearly everyone:** fixed scan rates (USB 1000 Hz, 500 Hz where a scan exceeds 1 ms; Bluetooth 250 Hz; 50 Hz while the host sleeps); ADC oversampling and tap-preferred removed. Retired VIA IDs 13, 16, 18, 29 stay unused, their record fields keep their offsets. Reason: owner's settings review (2026-11-19).

## Never rename (persisted or matched by tools)

Reason: renaming orphans data in keyboards' EEPROM or breaks tool matching.

- EEPROM magics: `VIAL` record tag (`EEPROM_VIAL_ADDR`), `ECK3` keymap slot tag, `CMIT`, `HKCx` settings records, settings words, nRF52 bond slot tag. To change one, keep reading the old value and rewrite it (as `hhkb_keymap_storage.c` does for `ECK2`).
- Vial keyboard IDs (`BOARD_VIAL_UID`): `.vil` files record it.
- "HHKB" in the USB product string: `hybrid_update.py` and bench tools match it.
- **USB serial = `vial:f64c2b3c-` + chip unique ID, constant across versions.** Reason: Chrome ties WebHID permission (Windows its driver) to the serial; a version serial lost the keyboard after every web update (2026-11-17). VIA setting 28 reports the version.
- **EEPROM keymaps hold internal 32-bit keycodes:** never change an existing value's meaning; use unused values or flag bits (K_NULL = 0x01, `HT_MODS_FLAG` bit 23). Reason: saved keymaps would silently change.
- No project prefix on code identifiers; the name is ECK everywhere except the aroum/dmk credit.

## Build and test

- `./build.sh hybrid|classic|type_s [-c] [-d DEFS]`; bench build `-d HHKB_ADC_DIAGNOSTICS,HHKB_SCAN_DIAGNOSTICS`. Hybrid `.hfb` needs nRF5 SDK 17.1.1: `$NRF5_SDK_ROOT`, else `../../SDKs/nRF5_SDK_17.1.1`, else `../nRF5_SDK_17.1.1` (CI).
- `tests/run_all_tests.sh`. Modules built against `tests/*_mocks/`: a new `#include` needs a mock header.
- Warning-free (`-Wall -Wextra`); fix causes.
- **One compiler: Arm GNU Toolchain 15.3.Rel2** on bench and CI (`ci.yml` pins it). Change both together, then re-measure `noise` and rest readings. Reason: releases come from CI; compilers shift scan timing.
- **Refactors prove binaries unchanged:** `tools/build_hashes.sh` on a `git archive HEAD` copy and the working tree; every `.bin` hash matches (`.hfb` lines differ when only one tree has `nrf52/_build`).
- **Shared-code changes for a new board leave HHKB binaries hash-identical** (no HHKB release needed): keep HHKB code under `#if` where a generic form compiles differently; macros, not inline functions, for HHKB hardware writes. Reason: R3S work (2026-11-17).
- **Board code in `stm32/boards/<board>/`**; shared code reads board facts only via `board_config.h`. Keep `target_sources` order in `stm32/CMakeLists.txt`: it sets link order and so every binary.
- **Every change to a shipped binary gets a version bump**; ask the owner when the level is unclear. Reason: owner (2026-11-17), so the reported version identifies the firmware.
- **History: one commit per release.** Each bump squashes commits since the last tag into `ECK vX.Y.Z: <summary>`, tags, force-pushes `main` and the tag; first `git bundle create backups/git-history/<date>.bundle --all`. Reason: owner wants a short history. No `Co-Authored-By` trailers (owner).
- nRF52 build changes need a CI run: GNU make 4 leaks "Entering directory" into the SDK link (`build.sh` passes `--no-print-directory`), macOS make 3.81 doesn't; sandboxes may refuse fresh SDK archives.
- While another job uses `build/`, build a `git archive` copy in a sibling folder (set `NRF5_SDK_ROOT`), then delete it. Reason: shared `build/` clobbers.

## Bench (owner's Hybrid and R3S, USB only)

- Assembled keyboard, no SWD or PPK2; the owner types on it. Flash and measure over USB without asking; never ask for keypresses (if truly needed, run the recorder ~40 s and tell the owner to start right away). Tools needing SWD (case open): ask first.
- Wrap USB/probe commands in `timeout`; check long commands within 30 s. After any flash, confirm the keyboard is back on USB.
- Back up before risky flashes (EEPROM layout, storage code): `hybrid_update.py backup backups/<name>/`.
- **Before tagging a release, run `tools/bench/release_check.py --perf --nrf`** (~2.5 min); first tell the owner not to use the keyboard and wait for their go. Reason: typing skews measurements and failed the EEPROM check once (2026-11-17).
- Tell the owner before tests that light LEDs (`--recovery`).
- **Never commit raw EEPROM dumps or print the macro area**: the owner's macro may be a password. Use `tools/scrub_eeprom_fixture.py`; report only address ranges.
- Bench scripts close their HID handle before running another tool that opens the keyboard, or it fails.
- **Never use `hybrid_update.py` on the R3S:** Topre's Bank 2 tag is at EEPROM `0x17E0`, and `enter_dfu` opens the first "HHKB" (the Hybrid). Use `tools/r3s/`.
- After changing an R3S or shared flow in `web/eck.js`, run `tools/web_bench/bench.mjs` (back up EEPROM first). Reason: agents can't click browser device dialogs (2026-11-20).
- **In ST's DFU loader HHKB and R3S look alike** (`0483:df11`): every DFU flow keeps `eck.js`'s `isTopreBank1` check before writing. Reason: Recover could find the other keyboard and replace Topre's updater (2026-11-20).

## Firmware rules

- **Hybrid timing never uses counted loops**; SysTick delays. Reason: flash prefetch changed loop timing and broke Bluetooth.
- After compiler-flag or hot-path changes, re-measure `noise` and per-key rest readings: scan speed moves them.
- **Data EEPROM writes go through `eeprom_write_word`** (`stm32/board/eeprom.c`). Reason: tasks locked the EEPROM under each other. Exception: Classic SW6 erase, before the scheduler.
- **Task stack depth is `sizeof(stack) / sizeof(StackType_t)`, never a literal.** Reason: a stale `512` reset-looped v0.4.4 until DFU flash (2026-11-17).
- **Never read a struct through a `uint32_t *` cast** (strict aliasing): `memcpy`. Reason: GCC dropped stores, corrupting a saved settings record (2026-11-16).
- Unrecoverable errors call `platform_system_reset()`. Reason: a hung Hybrid on batteries needs them pulled.
- `hybrid_update.py stm32` refuses unless Bank 1 holds the boot stub (`--force`). Reason: otherwise a failed write has no USB recovery.
- **nRF52 GATT changes only append services; Service Changed stays off.** Reason: appending kept bonded hosts typing; enabling `NRF_SDH_BLE_SERVICE_CHANGED` shifted handles and stopped Bluetooth typing on the Mac, even after re-pairing (2026-11-26).
- **The `.hfb` never rewrites the nRF52 MBR** (`0x0-0xFFF`); app stays at `0x26000`. Reason: PFU's bootloader copies only `0x1000-0x35FFF` and jumps there.
- **Power-switch recovery needs the batteries out:** the stub reads the switch only at reset, and plugging USB doesn't reset a battery-powered keyboard (2026-11-17).
- **JIS positions reach 68:** per-position bits in a `uint64_t` (`dip_modes.c`) need two words under `HHKB_JIS`. JIS facts come from PFU's JIS images and manuals ([JIS](docs/models.md#jis)). Reason: JIS port (2026-11-19).

## Docs

- **Rerun `tools/gen_layer_svg.py` after changing the HHKB default keymap, a chord, DIP switch behaviour or an LED pattern; `tools/gen_r3s_led_svg.py` after R3S LED driver changes; `tools/make_jis_via_json.py` after changing `stm32/via/via.json`.** Reason: the `docs/*.svg` (and `-jis` versions) are the user-facing pictures, `via_jis.json` is generated (2026-11-19).
- Open items live only in [docs/deferred.md](docs/deferred.md); **review the whole file every release** (drop done items, move shipped facts to their reference doc, bump "up to vX.Y.Z"). Reason: stale across v0.5.0-v0.6.1 (2026-11-20).
- **One home per fact:** guides link to reference docs (`build.md`, `hybrid-hardware.md`, `bench.md`). Reason: docs had grown to 19k words of repeats (cut to 11k, 2026-11-16).

## Learnings

Record each lesson in its section above, or in the doc that owns the fact, with scope and reason; delete those whose reason is gone.
