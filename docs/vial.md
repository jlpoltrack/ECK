[ECK docs](overview.md)

# Vial

ECK builds with [Vial](https://get.vial.today/) on every model (HHKB Classic, Type-S, Hybrid and the Realforce R3S): open the Vial app or vial.rocks and the keyboard shows up with its layout, no definition file needed. VIA still works; see [VIA mode](#via-mode).

Build: on by default (CMake option `ECK_VIAL`; the code is under `VIAL_ENABLE`). `-DECK_VIAL=OFF` builds the VIA-only firmware of v0.4.4 and earlier. Bench check, no keypresses: `uv run -q --with hidapi python3 tools/bench/vial_check.py`.

## VIA mode

The Vial app accepts only VIA protocol 9, and at protocol 9 the VIA web app wants an old-format definition without custom menus, so the **HHKB Settings** menu (tap-hold, calibration, Rapid Trigger, LEDs) can't show. The app asks for the protocol version before anything else, so the keyboard can't tell the two apps apart.

**Fn + Ctrl + V** switches to VIA mode: the keyboard reports protocol 12 and usevia.app shows the menu ([VIA](via.md)). Press it again, or restart the keyboard (unplug it; on the Hybrid on batteries, power it off), to go back to Vial. Each switch blinks the notify LED, twice into VIA mode and once back to Vial (the blue LED on the Hybrid, LED 0 on the Classic and Type-S, the Simple Mode LED on the R3S) and drops the keyboard off USB for 100 ms, so the open app reconnects and reads the new mode by itself. The chord goes by key position (the Fn key, or the left ◇ with SW4 on the HHKB; left Ctrl; V), whatever the keymap holds there, and isn't sent to the host.

## Size

Totals per model: [build](build.md#flash-and-ram-usage). Vial adds about 6.3 KB of flash and 1,544 B of RAM to every model: 520 B for its state and 1,024 B for the larger keyboard task stack below. The flash is roughly: features ~3.9 KB, protocol, QMK settings and storage ~1.2 KB, the xz-compressed layout definition ~0.6 KB (built from the board's VIA definition by `tools/vial_def.py`), hooks in existing code ~0.6 KB.

Keyboard task stack: 2,048 B with Vial (1,024 B without); 808 B worst case with one level of feature nesting (`tools/stack_depth.py` from `keyboard_task`). Keys a feature presses nest at most 3 deep (`VIRTUAL_DEPTH_MAX` in `app/keyboard.c`; deeper presses are dropped, so entries that name each other can't overflow the stack); each extra level costs at most ~200 B, about 1.2 KB in all.

## What it has

- Vial protocol 6: keyboard ID (per board: `BOARD_VIAL_UID` in `board_config.h`), definition. Always unlocked, as vial-qmk's `VIAL_INSECURE` builds: no unlock keys, QK_BOOT allowed anywhere, macros writable, and VIA's bootloader jump (which `hybrid_update.py` and the web flasher use) always works.
- 8 each of tap dances, combos (2-4 keys), key overrides and alt repeat keys; Repeat, Alt Repeat, Caps Word, Layer Lock, Grave Escape, one-shot keys (OSM, OSL), combo and key-override on/off keys. Vial macros with 16-bit keycodes (actions 5-7) play.
- Tap dance outcomes follow vial-qmk (interrupted means tap, early finish on release where no later outcome exists); each entry keeps its own state. Keys a feature presses go through the normal press steps (hold-tap resolution, one-shot, Caps Word, Repeat) and are released as pressed, so editing an entry while it is held can't strand a key.
- Layer Lock never locks the selected system's base layer (0, or 2 in System 2).
- Magic swaps apply to real key presses only; feature outputs go out unswapped, as in vial-qmk. Alt Repeat matching (entry keycodes with modifiers, required and allowed mods, best fit, default entry) follows vial-qmk. Key override trigger mods follow QMK's hands rule (one hand needs that hand, both hands accept either).
- A tapped one-shot modifier is released after the key it applies to (without Vial it is released just before, so the key goes out unmodified: a bug in the VIA-only build, left there because no shipped keymap uses one-shot keys).
- QMK settings: Grave Escape override (1), combo term (2), one-shot timeout (6; 0 is none), tapping term (7; 50-500 ms), magic swaps (21), hold on other key press (23), retro tapping (24), quick tap term (25; up to 500 ms). Tapping term, quick tap and setting 23 are ECK's existing settings, shared with VIA's HHKB Settings menu: ECK's "permissive hold" holds when another key goes down, which is QMK's hold on other key press, not QMK's permissive hold (22, not offered).
- Entries are saved in one record at the board's `EEPROM_VIAL_ADDR` (HHKB `0x08081608-0x0808173F`, R3S `0x08080300-0x08080437`; both free in ECK's maps, and blank on a stock R3S). It's a single copy: a power loss mid-save loses the Vial entries (keymap and macros keep their two slots). A failed EEPROM write abandons that save, as the keymap writer does.

## Differences from vial-qmk and side effects

- The USB serial is `vial:f64c2b3c-<chip ID>` (the Vial app finds keyboards by that prefix). Chrome ties WebHID permission to the serial, so after the first update from a VIA-only version (v0.4.4 or earlier) the web flasher and usevia.app ask for permission once more.
- The switch-matrix reply uses protocol 9's layout (rows from byte 2, no row offset) as Vial's matrix tester reads it; VIA mode uses protocol 12's.
- Vial's Reboot (0x7C01) and Debug (0x7C02) keys are ECK's Layer Up and Layer Down: saved keymaps already hold those values, so their meaning can't change.
- Not done: auto shift, mouse keys, chordal hold, flow tap, QMK's built-in alt-repeat pairs, key overrides that start when a modifier goes down after the key, more than 8 entries per kind (the HHKB record's free region holds 12 of each; ECK's other free 256 B at `0x08081400` would add 7).
- Known limits: combos are matched on the key's code before any pending layer-tap resolves, so a combo on a layer-tap's layer can't start with the first key after it; the keyboard task reads entries without a lock, so an edit landing mid-read can produce one wrong key (never a stuck one).
