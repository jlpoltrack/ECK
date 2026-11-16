# HHKB Classic ECK Firmware — Uses the Original Controller, No Hardware Mods Required

Alternative firmware for the HHKB Professional Classic. It runs directly on the original STM32L072 controller: no controller replacement, soldering, or keyboard disassembly is required.

**Documentation:** [Install](install.md) • [Supported Models](models.md) • [Backup & restore](backup-restore.md) • [Build](build.md) • [Config](config.md) • [VIA](via.md)

## Supported keyboards

- HHKB Professional Classic (`PD-KB401W`, `PD-KB401B`, `PD-KB401WN`, `PD-KB401BN`)
- HHKB Professional Classic Type-S (`PD-KB401WSC`, `PD-KB401BSC`, `PD-KB401YSC`)

See the detailed [Supported Models Guide](models.md) for part numbers, color variants, and container IDs.

## Features

- [VIA](https://www.usevia.app/) support for runtime keymap and macro changes.
- Configurable matrix scan rate: 50, 125, 250, 500, or 1000 Hz. The stock firmware's matrix loop runs at about 50 Hz. At 50–500 Hz, optional ×4 hardware ADC oversampling improves measurement precision and reduces noise.
- Factory thresholds, or per-key user calibration through [VIA](https://www.usevia.app/).
- Adjustable actuation point and classic release hysteresis.
- Rapid Trigger, including independent press and release sensitivity.
- Optional hardware ADC oversampling to reduce measurement noise.
- Two configurable LEDs; [VIA](https://www.usevia.app/) assigns Off, Debug heartbeat, Caps Lock, Fn layer, or System 2 to PA8 and PA9.
- Mod-Tap and Layer-Tap, with configurable tapping term, permissive hold, tap preference, and quick tap.
- Two base/Fn layer pairs, selectable with the System 1 and System 2 keycodes in VIA.

## Scan rate and power

Running the controller at a higher scan rate is within the MCU's normal operating specifications. The 32 MHz clock and the supported scan rates are standard operating modes, not overclocking; the chip remains cool.

Measured current consumption:

- Stock firmware, 50 Hz, 16 MHz: 7.5 mA.
- ECK, 500 Hz with ADC oversampling, 32 MHz: 10 mA.
- ECK, 1000 Hz without oversampling, 32 MHz: 12 mA.

At 1000 Hz the increase is only 4.5 mA, which is negligible for a USB-powered keyboard. If power consumption matters, select 50 Hz in [VIA](https://www.usevia.app/) to return to the stock 7.5 mA level.

PFU's 50 Hz choice may reflect the Classic's shared design lineage with the battery-powered Bluetooth Hybrid. The Classic is effectively the wired-only version, and the scan rate apparently drew little attention for years.

## Installation and recovery

The firmware is installed on the original controller without opening the keyboard. The supported paths are the Python flasher or the official HHKB Keymap Tool after returning to the stock Bank 1 updater with SW6. DFU and SWD are also available for backup, recovery, and development.

The original firmware can be restored using the documented Bank 1/SW6 recovery path, or by writing a saved DFU backup. Read [Installation, backup, and recovery](backup-restore.md) before flashing.

## VIA

[VIA](https://www.usevia.app/) provides the keymap, macros and the full **HHKB Settings** menu: Scan Rate, Calibration, Rapid Trigger, and LED, as well as a **Debug** menu displaying the dynamic **Firmware Version** read directly from controller memory. The custom controls are available only in the VIA web app. To load them, open **Settings**, enable **Show Design tab**, then use **Design → Load Draft Definition** to select `via.json`. Vial is not supported.

## DIP switches

ECK reads all six DIP switches at power-on. SW1–SW5 work as on the stock HHKB:

- **SW1/SW2:** HHK mode (both OFF, default), Win mode (SW1 ON) or Mac mode (SW2 ON). Fn + Ctrl + W and Fn + Ctrl + M switch modes until power-off.
- **SW3:** the top-right key sends Backspace instead of Delete (no effect in Mac mode).
- **SW4:** the left ◇ key acts as Fn.
- **SW5:** Alt and ◇ swap places on both sides.

Mac mode adds Fn + F = Eject and Fn + Delete = Clear; HHK mode adds Fn + right ◇ = Stop. The switches change only keys that still hold their default keycodes, so [VIA](https://www.usevia.app/) remaps keep working. See [Configuration](config.md#dip-switches) for details.

SW6 currently provides the recovery transition to the stock Bank 1 updater.

## Other HHKB models

### HHKB Professional Hybrid / Hybrid JIS

Port in progress: see [Hybrid port status and ToDo](hybrid-port.md). ECK runs on both chips (USB and Bluetooth); the power switch, battery level and LED follow PFU's manual:

- **Power switch:** hold 1 s to turn on (blue), 2 s to turn off (orange, then dark). A light press blinks blue once per Bluetooth slot number (1-4), or lights 1 s when on USB. Plugging in a USB cable also turns the keyboard on; inserting batteries does not (as stock, tested).
- **Batteries:** orange blinks once every ~30 s when low and twice every ~15 s when the batteries need replacing (only on batteries, in Bluetooth use). With empty batteries the keyboard lights orange and turns off.
- **Auto power-off on batteries (SW6 OFF):** after 60 min without a key press (the manual says 30), 10 min in pairing mode, or 1 min without reaching the paired host. It is a deep sleep: any key wakes the keyboard (1 s blue; that key is not typed, and reconnecting takes a second or two). Only a 2 s switch hold turns it fully off. SW6 ON or USB power never turns it off.
- **Battery level on the host:** 100, 15, 5 or 0 %, as stock: the board has three voltage detectors, not a gauge. It is sent when the level changes and on every reconnection; 100 % on USB power.
- **Battery life:** idle but connected, ECK draws 1.50 mA against stock's 5.1 mA; deep sleep 0.33 mA, off 0.20 mA (both firmwares). A rough day of 2 h typing, 6 h idle and 16 h asleep uses about 20-25 mAh, roughly 2-3 months on two AA cells (estimate, not a measured run). Details: [Hybrid hardware](hybrid-hardware.md#5-power).
- Deleting pairings (Fn + Q, then Fn + Ctrl + Delete + 1-4, or Fn + Z + Delete) shows orange and turns the keyboard off, as stock.
- **Hardware:** [Hybrid hardware architecture](hybrid-hardware.md).
- **Installing, updating, going back to stock, recovery:** [Hybrid updates](hybrid-update.md). Hold the power switch while plugging in USB to reach the recovery loader.

### HHKB Professional Classic JIS

PFU does not provide a separate public firmware file for this model, and board photos are scarce. It may share the regular Classic hardware, but this still needs confirmation.

### HHKB Professional 2 and other models

Firmware development is possible, but installation is different. Pro2 has no built-in USB firmware update path, so it must be opened and flashed through SWD with a programmer.

### Realforce R3 and newer

Realforce R3 and newer models use a similar firmware-update container format, so firmware for the original controller should be possible. I do not have a compatible keyboard for hardware testing or for reverse-engineering the controller pinout, so this is not supported yet.

## Directory structure

- `stm32/app/` — startup, keyboard task, board event dispatch (`events.c`), memory, formatting.
- `stm32/keys/` — layers, hold-tap, one-shot, macros.
- `stm32/ec/` — matrix scanning, ADC, calibration, and Rapid Trigger.
- `stm32/board/` — board config, clocks and start-up (`platform.c`), DIP switch input, stock SW1–SW5 behaviour and physical Fn/Ctrl tracking (`dip_modes.c`), key positions (`hhkb_positions.h`), LEDs and the two-system layer selector (`indicators_system.c`), linker script, FreeRTOS port.
- `stm32/via/` — VIA protocol (`via.c`), HHKB settings menu commands, keymap/macro EEPROM storage, `via.json` (VIA v3 definition with custom controls).
- `stm32/usb/` — TinyUSB glue and USB descriptors.
- `stm32/hybrid/` — Hybrid power, sleep, Bluetooth chords and the SPI link to the nRF52; `stm32/boot/` — Hybrid Bank 1 boot stub.
- `common/` — `ble_link.h`, shared by the STM32 and nRF52 builds.
- `docs/` — documentation and research notes.
