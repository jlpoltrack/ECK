[ECK docs](overview.md)

# Debug and HID Console

ECK always has a USB HID Console interface (QMK's console usage) for diagnostic logs. It is USB only, and every channel is off by default.

1. In the [VIA web app](https://www.usevia.app/), open **Settings** and enable **Show HID Console tab**.
2. In **Configure → HHKB Settings → Debug**, turn on the channels you want:
   - **EC Matrix Stream:** `[Topre] C<col> R<row>: raw=… base=… peak=… travel=…% DN|UP` (raw ADC count, calibrated rest and peak, travel, pressed or released).
   - **Calibration Monitor:** calibration progress, e.g. `[CAL] Key C3 R2 peak updated: 2650 -> 2890` and `[CAL] [====>    ] X% (N/60)`.
   - **Polling Rate Monitor:** `[RATE] matrix=1000 Hz (target: 1000 Hz), usb_loop=… Hz`. A `-d HHKB_SCAN_DIAGNOSTICS` build prints `[SCAN]` lines (scan times, missed scans, queue overflows) instead.
   - **VIA Telemetry Diag:** VIA link counters, `[VIA_DIAG] rx=… tx_sub=… tx_cpl=… tx_fail=… pend=…`.
3. Watch the **HID Console** tab; its buttons save or clear the log.

The Debug menu also shows the firmware version.
