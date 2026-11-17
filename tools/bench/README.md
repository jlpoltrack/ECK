# Bench tools

Bench, measurement and reverse-engineering tools used while porting the firmware to the HHKB Professional Hybrid. End users do not need them: installing and updating uses `tools/hybrid_update.py`, `hhkb_flasher.py` and `make_hfb.py`.

Run them from the repository root, mostly with `uv run --with pyocd python3 tools/bench/<tool>.py ...` (SWD) or `uv run --with hidapi ...` (USB); each file's docstring gives its exact command.

Shared helpers: `tools/via_hid.py` (find the keyboard, VIA requests; also used by `hybrid_update.py`), `swd_attach.py` (pyOCD attach session) and `repo_paths.py` (repository and nRF52 app paths). A tool that runs another tool on the keyboard (e.g. `hybrid_update.py backup`) must close its HID handle first.

## Over USB (keyboard assembled)

| Tool | Purpose |
| --- | --- |
| `key_depth.py` | Per-key sensor readings over VIA: live depth, rest noise map, recorded presses, calibration, scan time. Needs a build with `HHKB_ADC_DIAGNOSTICS` (`./build.sh hybrid -d HHKB_ADC_DIAGNOSTICS`). |
| `eeprom_save_test.py` | Changes a setting, the Bluetooth scan rate and a key over VIA, checks they survive resets, restores them (no keypresses; prints address ranges only). |
| `via_keymap_check.py` | Checks KC_NO/KC_TRNS and modifier round trips and the VIA keymap reset (0x06), then restores the keymap. |
| `via_macros.py` | Reads, writes or clears the VIA macro buffer. `get` prints macro contents, which may hold a password: don't run it on a keyboard with real macros. |
| `rt_noise_sim.c` | Feeds recorded rest noise through the firmware's Rapid Trigger code to count false toggles per sensitivity. |

## Bench (case open)

| Tool | Purpose |
| --- | --- |
| `battery_sweep.py` | Steps a PPK2 supply on the battery terminals down and detects the low-battery warning in the current trace (uses `../Tools-PPK2`). |
| `swd_flash.py` | Flashes an ECK Bank 2 image over SWD (STM32CubeProgrammer, NRST wired). |
| `hybrid_state.py` | Bluetooth bridge state on both chips over SWD, without halting. |
| `hybrid_cmd.py` | Sends a bridge command (pair, select, delete, ...) through the STM32 over SWD; needs `./build.sh hybrid -d HHKB_HYBRID_TEST_CMD`. |
| `power_switch.py` | Presses the power switch over SWD and logs the LED pins. |
| `watch_inputs.py` | Prints every input-pin change on both chips, live. |
| `pin_map.py` | Maps STM32 pins to nRF52 GPIOs with two SWD probes. |
| `spi_pin_id.py` | Identifies which DSLogic channels carry the STM32-nRF52 lines. |
| `nrf_reset_probe.py` | Checks what STM32 PD2 does to the nRF52 (it is the reset line). |
| `nrf_pc_monitor.py` | Logs which nRF52 image runs, without halting it. |
| `nrf_ram_watch.py` | Logs changes in an nRF52 RAM window, without halting it. |
| `nrf_spi_trace.py` | Traces PFU's own nRF52 app on the link (stock firmware only). |
| `ram_diff.py` | Finds STM32 RAM that a USB request changes. |
| `rtt_dump.py` | Dumps a SEGGER RTT buffer over SWD (nRF52 app built with `make NRF_LOG=1`). |

SWD notes for this board (pads, the nRF52 vias, never letting pyOCD auto-unlock the nRF52) are in `docs/hybrid-hardware.md` (section 6).

