# ECK overview

Open firmware for the HHKB Professional HYBRID, Classic and Classic Type-S. It runs on the keyboard's own STM32L072 (and, on the Hybrid, its nRF52832 Bluetooth module) and installs over USB: no controller replacement, soldering or disassembly.

**Documentation:** [Install](install.md) • [Supported Models](models.md) • [Backup & restore](backup-restore.md) • [Build](build.md) • [Config](config.md) • [VIA](via.md) • [Debug](debug.md) • [Hybrid updates](hybrid-update.md)

## Supported keyboards

- HHKB Professional HYBRID Type-S (`PD-KB800BS`): tested on hardware.
- HHKB Professional Classic and Classic Type-S: build, untested on hardware.

Details, model numbers and unsupported models: [Supported models](models.md).

## Features

- [VIA](https://www.usevia.app/) support for runtime keymap and macro changes.
- Configurable matrix scan rate: 50, 125, 250, 500, or 1000 Hz (Hybrid: separate rates for USB and Bluetooth, default 1000 and 250 Hz). The stock firmware's matrix loop runs at about 50 Hz. At 50–500 Hz on USB, optional ×4 hardware ADC oversampling reduces measurement noise.
- PFU's factory thresholds by default, or per-key user calibration through VIA.
- Adjustable actuation point and classic release hysteresis.
- Rapid Trigger (off by default), including independent press and release sensitivity.
- Two configurable LEDs; VIA assigns Off, Debug heartbeat, Caps Lock, Fn layer, System 2 or Bluetooth status (Hybrid) to PA8 and PA9.
- Mod-Tap and Layer-Tap, with configurable tapping term, permissive hold, tap preference, and quick tap.
- Two base/Fn layer pairs, selectable with the System 1 and System 2 keycodes in VIA.
- Hybrid: Bluetooth LE keyboard with four host slots, the stock key combinations, power switch and battery handling.

## Scan rate and power

The 32 MHz clock and the supported scan rates are normal operating modes of the MCU, not overclocking.

Classic current consumption, from the [aroum/dmk](https://github.com/aroum/dmk) Classic port (not re-measured with ECK):

- Stock firmware, 50 Hz, 16 MHz: 7.5 mA.
- 500 Hz with ADC oversampling, 32 MHz: 10 mA.
- 1000 Hz without oversampling, 32 MHz: 12 mA.

On USB power this is negligible; select 50 Hz in VIA to return to about the stock level. Hybrid battery figures are below.

## Installation and recovery

- **Hybrid:** [Hybrid updates](hybrid-update.md): first install through PFU's updater, then ECK's boot stub, USB updates and recovery, and going back to stock.
- **Classic, Classic Type-S:** [Install](install.md), through `tools/hhkb_flasher.py` or PFU's Keymap Tool; a Classic already running ECK first returns to PFU's Bank 1 updater with SW6. Read [Backup & restore](backup-restore.md) before flashing.
- **Web flasher:** `web/` does the same from Chrome or Edge (WebHID/WebUSB). A release's `eck-web-flasher-<tag>.zip` bundles that release's images; serve the folder locally (e.g. `python3 -m http.server`). It has not yet been run on a keyboard.

## VIA

[VIA](https://www.usevia.app/) provides the keymap, macros and the **HHKB Settings** menu: Scan Rate, Calibration, Rapid Trigger, LED and Debug (with the firmware version). The custom controls are available only in the VIA web app: open **Settings**, enable **Show Design tab**, then use **Design → Load Draft Definition** to select `stm32/via/via.json`. Vial is not supported. See [VIA](via.md).

## DIP switches

ECK reads all six DIP switches at power-on. SW1–SW5 work as on the stock HHKB:

- **SW1/SW2:** HHK mode (both OFF, default), Win mode (SW1 ON) or Mac mode (SW2 ON). Fn + Ctrl + W and Fn + Ctrl + M switch modes until power-off.
- **SW3:** the top-right key sends Backspace instead of Delete (no effect in Mac mode).
- **SW4:** the left ◇ key acts as Fn.
- **SW5:** Alt and ◇ swap places on both sides.

Mac mode adds Fn + F = Eject and Fn + Delete = Clear; HHK mode adds Fn + right ◇ = Stop. The switches change only keys that still hold their default keycodes, so VIA remaps keep working. See [Configuration](config.md#dip-switches) for details.

SW6: on the Classic, recovery into PFU's Bank 1 updater ([SW6 recovery](backup-restore.md#sw6-recovery)); on the Hybrid, the stock meaning (OFF = auto power-off on batteries).

## Hybrid

ECK runs on both chips (USB and Bluetooth); the power switch, battery level and LED follow PFU's manual:

- **Power switch:** hold 1 s to turn on (blue), 2 s to turn off (orange, then dark). A light press blinks blue once per Bluetooth slot number (1-4), or lights 1 s when on USB. Plugging in a USB cable also turns the keyboard on; inserting batteries does not (as stock, tested).
- **Bluetooth:** Fn + Ctrl + 1-4 selects a host slot, Fn + Ctrl + 0 USB; Fn + Q, then Fn + Ctrl + 1-4 pairs a slot (details: [Hybrid port](hybrid-port.md#bluetooth-eck-native)).
- **Batteries:** orange blinks once every ~30 s when low and twice every ~15 s when the batteries need replacing (only on batteries, in Bluetooth use). With empty batteries the keyboard lights orange and turns off.
- **Auto power-off on batteries (SW6 OFF):** after 60 min without a key press (the manual says 30), 10 min in pairing mode, or 1 min without reaching the paired host. It is a deep sleep: any key wakes the keyboard (1 s blue; that key is not typed, and reconnecting takes a second or two). Only a 2 s switch hold turns it fully off. SW6 ON or USB power never turns it off.
- **Battery level on the host:** 100, 15, 5 or 0 %, as stock: the board has three voltage detectors, not a gauge. It is sent when the level changes and on every reconnection; 100 % on USB power.
- **Battery life:** idle but connected, ECK draws 1.50 mA against stock's 5.1 mA; deep sleep 0.33 mA, off 0.20 mA (both firmwares). A rough day of 2 h typing, 6 h idle and 16 h asleep uses about 20-25 mAh, roughly 2-3 months on two AA cells (estimate, not a measured run). Details: [Hybrid hardware](hybrid-hardware.md#5-power).
- Deleting pairings (Fn + Q, then Fn + Ctrl + Delete + 1-4, or Fn + Z + Delete) shows orange and turns the keyboard off, as stock.
- **Recovery:** hold the power switch while plugging in USB to reach ST's USB DFU loader ([Hybrid updates](hybrid-update.md#4-recovery)).

Repository layout: [README](../README.md#layout).
