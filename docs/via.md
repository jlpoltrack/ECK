[ECK docs](overview.md)

# VIA

ECK speaks the [VIA](https://www.usevia.app/) protocol (v3: keymap, macros, custom menus). Vial is not supported. The **HHKB Settings** menu works only in the VIA web app.

## Load the definition

1. Open the [VIA web app](https://www.usevia.app/), go to **Settings** and enable **Show Design tab**.
2. In **Design**, choose **Load Draft Definition** and select `stm32/via/via.json` (the same file for all three keyboards).
3. Connect the keyboard and open **HHKB Settings**.

If Layer 0 shows blank keys, click Layer 1 and back (a VIA rendering race).

**Linux:** if Chromium sees the keyboard (`cafe:4010`, a test ID) but VIA says it isn't responding, give the session access to its hidraw device, then replug, reload VIA and authorize the device:

```bash
sudo tee /etc/udev/rules.d/70-hhkb-via.rules >/dev/null <<'EOF'
SUBSYSTEM=="hidraw", KERNEL=="hidraw*", ATTRS{idVendor}=="cafe", ATTRS{idProduct}=="4010", TAG+="uaccess"
EOF
sudo udevadm control --reload-rules
```

If it still fails, check `chrome://device-log` for `HID` or `hidraw` entries.

## Settings

A keyboard with no saved settings uses PFU's factory thresholds with custom calibration and Rapid Trigger off.

- **Scan rate:** 50, 125, 250, 500 or 1000 Hz (default 1000). The Hybrid has a separate Bluetooth rate (default 250 Hz).
- **ADC oversampling ×4:** averages four conversions per key at 50–500 Hz on USB; 1000 Hz and Hybrid batteries always use ×1. The choice is kept for when the rate drops again.
- **Tapping term:** 50–500 ms (default 220). **Quick tap:** 0–500 ms (default 150; 0 disables).
- **Actuation, hysteresis, Rapid Trigger press/release sensitivity:** 0.1–4.0 mm. **RT top dead zone:** 0.0–0.6 mm. Defaults: actuation 3.0 mm, hysteresis 0.4 mm. Millimetres map the calibrated signal range onto a nominal 4 mm travel; they are a scale, not a measurement (the sensor response is non-linear).
- **LEDs PA8 and PA9:** Off, Debug heartbeat, Caps Lock, Fn layer, System 2, or Bluetooth status (Hybrid). Defaults: Caps Lock and Fn layer (Classic); Bluetooth status and Off (Hybrid).

**Systems:** assign **System 1** (`0x7E00`) or **System 2** (`0x7E01`) to switch between layers 0/1 and 2/3. Put each selector where the other system can reach it.

## Calibration

1. Turn on **Use custom calibration** with all keys released.
2. Click **Start calibration** (records rest levels).
3. Fully press every key you want calibrated. To see which are done, open VIA's **Key Tester** with **Test Matrix** on; calibration keeps running across tabs.
4. Click **Finish and save** (only keys with a valid range are updated).

The status counter doesn't update by itself: click **Refresh status**. For live progress, turn on **Debug → Calibration Monitor** and watch the [HID Console](debug.md).

## Storage

Settings and calibration are at EEPROM `0x08081500`. The keymap (16-bit VIA keycodes) and macros (512 bytes for all 16) share two alternating, checksummed slots at `0x08080600`, saved about 0.5 s after the last edit. Slots from earlier firmware are read and converted. `KC_NO` blocks a key; `KC_TRNS` falls through to the layer below. Full EEPROM map: [Hybrid hardware](hybrid-hardware.md#memory-map).
