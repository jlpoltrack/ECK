# ECK overview

Open firmware for the HHKB Professional Hybrid, Classic and Classic Type-S. It runs on the keyboard's own STM32L072 (and the Hybrid's nRF52832 Bluetooth module) and installs over USB: no soldering or disassembly. Tested on the Hybrid Type-S (PD-KB800BS); the Classics build but are untested ([models](models.md)).

**Users:** [Hybrid updates](hybrid-update.md) • [Classic install](install.md) • [VIA](via.md) • [Configuration](config.md) • [Backup & restore](backup-restore.md) • [Debug](debug.md)
**Developers:** [Build](build.md) • [Hybrid port](hybrid-port.md) • [Hybrid hardware](hybrid-hardware.md) • [Hybrid stock behaviour](hybrid-stock-behaviour.md) • [Deferred](deferred.md)

## Features

- [VIA](via.md): keymap, macros and the **HHKB Settings** menu (scan rate, calibration, Rapid Trigger, LEDs, debug). Vial is not supported.
- Scan rate 50–1000 Hz (stock: about 50 Hz); the Hybrid has separate USB and Bluetooth rates (defaults 1000 and 250 Hz). Optional ×4 ADC oversampling at 50–500 Hz on USB.
- PFU's factory thresholds by default, or per-key calibration with adjustable actuation point and hysteresis.
- Rapid Trigger (off by default) with separate press and release sensitivity.
- Mod-Tap and Layer-Tap with tapping term, permissive hold, tap preference and quick tap.
- Two base/Fn layer pairs (System 1 and System 2 keycodes).
- Two LEDs, each assignable in VIA.
- DIP switches SW1–SW5 as stock ([details](config.md#dip-switches)); SW6 is Bank 1 recovery on the Classic ([SW6 recovery](backup-restore.md#sw6-recovery)) and auto power-off on the Hybrid.
- Hybrid: Bluetooth LE with four host slots, the stock key combinations, power switch and battery handling.

The 32 MHz clock and all scan rates are normal MCU operating modes, not overclocking. Classic current from aroum/dmk (not re-measured): stock 7.5 mA, 500 Hz with oversampling 10 mA, 1000 Hz 12 mA; select 50 Hz to return to about stock.

## Hybrid use

ECK follows PFU's manual for the switch, battery and LED:

- **Power switch:** hold 1 s to turn on (blue), 2 s to turn off (orange). A light press blinks blue once per Bluetooth slot number (1-4), or lights 1 s on USB. Plugging in USB also turns it on; inserting batteries does not.
- **Bluetooth:** Fn + Ctrl + 1-4 selects a host slot, Fn + Ctrl + 0 USB. To pair: Fn + Q, then Fn + Ctrl + 1-4. Fn + Q, then Fn + Ctrl + Delete + 1-4 (or Fn + Z + Delete for all) deletes pairings and turns the keyboard off. Details: [Hybrid port](hybrid-port.md#bluetooth).
- **Batteries:** orange blinks once every ~30 s when low, twice every ~15 s when they need replacing (on batteries, in Bluetooth use). Empty batteries: orange, then off. Hosts see 100, 15, 5 or 0 % (the board has level detectors, not a gauge).
- **Auto power-off** (batteries, SW6 OFF): 60 min without a key (the manual says 30), 10 min in pairing mode, or 1 min without reaching the paired host. This is a deep sleep: any key wakes it (that key is not typed). Only a 2 s switch hold turns it fully off.
- **Battery life:** idle but connected 1.50 mA (stock 5.1 mA); deep sleep 0.33 mA, off 0.20 mA. A day of 2 h typing, 6 h idle and 16 h asleep is roughly 2-3 months on two AA cells (estimate). Details: [Hybrid hardware](hybrid-hardware.md#5-power).
- **Recovery:** batteries out, then hold the power switch while plugging in USB ([Hybrid updates](hybrid-update.md#4-recovery)).
