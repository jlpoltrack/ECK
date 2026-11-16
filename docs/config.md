**Documentation:** [Install](install.md) • [Backup & restore](backup-restore.md) • [Build](build.md) • [Config](config.md) • [Models](models.md) • [VIA](via.md) • [Debug](debug.md)

# Configuration

## Build presets

`./build.sh hybrid`, `./build.sh classic` or `./build.sh type_s` picks the keyboard; Classic and Type-S differ only in the `.hfb` model ID and version. `-d` adds bench options (`HHKB_ADC_DIAGNOSTICS`, `HHKB_SCAN_DIAGNOSTICS`, `HHKB_HYBRID_TEST_CMD`); see [Build](build.md). Everything else is a runtime [VIA](https://www.usevia.app/) setting: Rapid Trigger, custom calibration, ×4 ADC oversampling (applies at 50–500 Hz; 1000 Hz and Hybrid batteries always use ×1), scan rate, LEDs and hold-tap timing. For the Hybrid see [Hybrid port](hybrid-port.md) and [Hybrid updates](hybrid-update.md).

## Matrix scanning

The Classic matrix has 15 columns and 4 rows. ECK selects a multiplexer column, pulses PC8 to discharge the sampling path, drives one row LOW, waits for the analog path, performs one ADC conversion, releases the row, and repeats for all rows and columns. PC14 powers the sensor circuit, PC7 enables the amplifier, and PA1 is ADC1_IN1.

The timer scanner is enabled by default. [VIA](https://www.usevia.app/) selects 50, 125, 250, 500, or 1000 Hz; the 1000 Hz choice uses a 1000 µs target period. The scanner runs from SRAM and uses TIM2 for analog delays.

## Matrix and peripheral pinout

- **Matrix:** 4 rows × 15 columns.
- **Row drive:** `PB0`, `PB1`, `PB2`, and `PB10` (`GPIOB` masks `0x001`, `0x002`, `0x004`, `0x400`).
- **Column select:** two 4051 analog multiplexers. `PC6` selects U1 for columns 0–7; `PB15` selects U2 for columns 8–14; `PB12`, `PB13`, and `PB14` are the channel-address lines.
- **Analog path:** `PC14` powers the sensor circuit; `PC7` enables the op-amp; `PC8` discharges sampling capacitor C57; `PA1` is ADC1 input `ADC_IN1`.
- **DIP switches:** `PA3`–`PA7` and `PC4`, active-low inputs.
- **LEDs:** `PA8` and `PA9`. [VIA](https://www.usevia.app/) independently assigns Off, Debug heartbeat, Caps Lock, Fn layer, System 2, or Bluetooth status (Hybrid); the defaults are Caps Lock on PA8 and Fn layer on PA9 (Classic), Bluetooth status on PA8 and Off on PA9 (Hybrid).

## Factory data and calibration

Each board's factory press thresholds (PFU's tables in EEPROM at `0x08081100`/`0x08081200`) are read at every scan, so they stay specific to the board when the same ECK HFB is installed elsewhere. With custom calibration and Rapid Trigger off, which is how a keyboard with no saved settings starts, each key is pressed above its factory threshold and released 50 counts below it; a key without a valid factory threshold uses the default range instead. Custom calibration records a baseline and peak per key in a separate EEPROM record; turning it on, or turning on Rapid Trigger, uses that range and the actuation settings. Saved settings keep their own choice. Factory tables are never overwritten.

## DIP switches

SW1–SW5 follow the stock HHKB settings. They are read once at power-on, so change them with the keyboard unplugged (and, on the Hybrid, switched off).

| Switch | OFF | ON |
| --- | --- | --- |
| SW1, SW2 | Mode select (below) | Mode select (below) |
| SW3 | Top-right key is Delete; Fn + Delete is Backspace | Top-right key is Backspace; Fn + Delete is Delete |
| SW4 | Left ◇ is GUI | Left ◇ is Fn |
| SW5 | Alt and ◇ in their stock places | Alt and ◇ swapped on both sides |

| SW1 | SW2 | Mode |
| --- | --- | --- |
| OFF | OFF | HHK (default) |
| ON | OFF | Win |
| OFF | ON | Mac |
| ON | ON | HHK |

- **HHK mode:** Fn + right ◇ sends Stop.
- **Win mode:** no extra keys.
- **Mac mode:** SW3 has no effect: the top-right key sends Backspace (the Mac delete key) and Fn + Delete sends keypad Clear. Fn + F sends Eject.
- **Changing mode without the switches:** Fn + Ctrl + M selects Mac mode. Fn + Ctrl + W selects Win mode, or HHK mode when SW1/SW2 select HHK. These presses are not sent to the host, and the change lasts until power-off. The chord goes by key position (the Fn key, or the left ◇ with SW4; left Ctrl; W; M), so it works whatever the keymap holds there.

The rest of the ECK Fn layer (Caps Lock, volume, keypad, Power) is the same in every mode.

The switches change only keys that still hold their default keycodes: the top-right key (Backspace or Delete on the base and Fn layers), F, and the four bottom-row modifiers. A key remapped in VIA to anything else keeps its VIA keycode. With SW4 on, the left ◇ selects the Fn layer of the active system (layer 1 for System 1, layer 3 for System 2). With SW4 and SW5 both on, the left ◇ key is Fn and the left Alt key sends ◇.

SW6 is assigned to Bank 1 recovery by default on the Classic. On the Hybrid it keeps the stock meaning: OFF enables auto power-off on batteries (60 min without a key press, 10 min in pairing mode, 1 min without reaching the paired host), ON disables it. Auto power-off is a deep sleep: any key wakes the keyboard. Only a 2 s hold of the power switch turns it fully off. Assign the System 1 and System 2 keycodes in VIA to select a base/Fn layer pair. Fn + Tab remains Caps Lock.
