[ECK docs](overview.md)

# Configuration

Settings are changed at runtime in [VIA](via.md); build options are only for bench work ([Build](build.md)).

## DIP switches

Read once at power-on, so change them with the keyboard unplugged (and, on the Hybrid, switched off).

| Switch | OFF | ON |
| --- | --- | --- |
| SW1, SW2 | Mode select (below) | Mode select (below) |
| SW3 | Top-right key is Delete; Fn + Delete is Backspace | Top-right key is Backspace; Fn + Delete is Delete |
| SW4 | Left ◇ is GUI | Left ◇ is Fn |
| SW5 | Alt and ◇ in their stock places | Alt and ◇ swapped on both sides |
| SW6 | Classic: normal. Hybrid: auto power-off on batteries | Classic: [Bank 1 recovery](backup-restore.md#sw6-recovery). Hybrid: no auto power-off |

| SW1 | SW2 | Mode |
| --- | --- | --- |
| OFF | OFF | HHK (default) |
| ON | OFF | Win |
| OFF | ON | Mac |
| ON | ON | HHK |

- **HHK mode:** Fn + right ◇ sends Stop.
- **Mac mode:** SW3 has no effect: the top-right key sends Backspace and Fn + Delete sends forward Delete, as on Apple keyboards.
- **Without the switches:** Fn + Ctrl + M selects Mac mode; Fn + Ctrl + W selects Win mode (or HHK when SW1/SW2 select HHK). The change lasts until power-off and isn't sent to the host. The chord goes by key position (the Fn key, or the left ◇ with SW4; left Ctrl; W; M), whatever the keymap holds there.

The rest of the Fn layer (Caps Lock, volume, keypad, Power) is the same in every mode; Fn + Tab is Caps Lock.

The switches change only keys that still hold their default keycodes (the top-right key, F, and the four bottom-row modifiers), so VIA remaps are kept. With SW4 on, the left ◇ selects the Fn layer of the active system (layer 1 for System 1, layer 3 for System 2). With SW4 and SW5 both on, the left ◇ is Fn and the left Alt sends ◇.

## Key sensing

Matrix 4 rows × 15 columns. Per key, ECK selects a multiplexer column, pulses the discharge line, drives the row low, waits for the analog path and converts one ADC sample. The scanner runs from SRAM, paced by TIM2.

| Function | Pins |
| --- | --- |
| Rows | `PB0`, `PB1`, `PB2`, `PB10` |
| Columns | two 4051 muxes: `PC6` selects columns 0–7, `PB15` columns 8–14; address `PB12`–`PB14` |
| Analog path | `PC14` sensor supply, `PC7` op-amp enable, `PC8` discharges C57, `PA1` ADC_IN1 |
| DIP switches | `PA3`–`PA7`, `PC4` (active low) |
| LEDs | `PA8`, `PA9` |

Full Hybrid pin table: [Hybrid hardware](hybrid-hardware.md#pin-table).

**Thresholds.** Each board's factory press thresholds (PFU's tables at EEPROM `0x08081100`/`0x08081200`) are read at every scan, so the same image works on any board. By default (custom calibration and Rapid Trigger off) a key presses above its factory threshold and releases 50 counts below it; a key without a valid threshold uses the default range. Custom calibration stores a rest level and peak per key in ECK's own EEPROM record; with it or Rapid Trigger on, keys use that range and the actuation settings. The factory tables are never written.
