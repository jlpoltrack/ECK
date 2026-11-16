**Documentation:** [Install](install.md) • [Backup & restore](backup-restore.md) • [Build](build.md) • [Config](config.md) • [Models](models.md) • [VIA](via.md) • [Debug](debug.md)

# Debug and HID Console

ECK always provides a USB HID Console interface (the QMK console usage) for diagnostic logs: key sensor readings, calibration progress, scan rates and VIA link counters. It is USB only; nothing is logged over Bluetooth.

By default, all debug logging channels are **disabled** to avoid unnecessary MCU and USB bus overhead.

---

## Enabling Debug Logging

### 1. Enable HID Console in the VIA Web App
1. Open the [VIA web application](https://www.usevia.app/).
2. Click the **Settings** gear icon in the top navigation bar.
3. Toggle on **Show HID Console tab**.
4. A new **HID Console** tab will appear in the top menu bar.

### 2. Enable Logging Channels in Keyboard Settings
1. Navigate to the **Configure** tab.
2. Open **HHKB Settings** → **Debug** submenu.
3. Toggle the desired logging flags:
   - **EC Matrix Stream** — prints key readings as `[Topre] C<col> R<row>: raw=… base=… peak=… travel=…% DN|UP`: raw ADC count, the key's calibrated rest and peak, travel in percent, and pressed (DN) or released (UP).
   - **Calibration Monitor** — real-time progress bar and calibration stream: baseline capture, peak registration per key (`[CAL] Key C3 R2 peak updated: 2650 -> 2890`), completion `[CAL] [====>    ] X% (N/60)`, and a note when all 60 keys have a peak.
     
     > [!NOTE]
     > Switching between the **Configure** tab and **HID Console** does not interrupt or reset calibration — the state remains active in MCU RAM until *Finish and save* is clicked. Console monitoring provides uninterrupted real-time visibility without having to manually refresh the status counter in the calibration menu.
   - **Polling Rate Monitor** — prints the measured matrix scan rate against its target and the USB task loop rate (`[RATE] matrix=1000 Hz (target: 1000 Hz), usb_loop=… Hz`). A `-d HHKB_SCAN_DIAGNOSTICS` build prints `[SCAN]` lines with scan times, missed scans and queue overflows instead.
   - **VIA Telemetry Diag** — prints VIA link counters: requests received, responses submitted, completed and failed, and whether a response is pending (`[VIA_DIAG] rx=… tx_sub=… tx_cpl=… tx_fail=… pend=…`).

> [!TIP]
> You can save logs to a text file or clear the output area directly using the buttons in the upper-right corner of the **HID Console** tab in VIA.

---

## Non-linear Topre Capacitive Sensor Response

The Topre capacitive switch relies on a conical spring compressing against PCB sensing pads. The resulting capacitance change is non-linear in key travel. The table is an approximate illustration, not measured with ECK:

| Physical Key Travel | Sensor Value (% / ‰) | Mechanism Behavior & Physics |
|---|---|---|
| **0 mm** | **0%** (0 ‰) | Key at rest (baseline level); conical spring fully retracted. |
| **1 mm** | **15%** (150 ‰) | Initial rubber dome compression; slow initial capacitance growth. |
| **2 mm** | **50%** (500 ‰) | Peak tactile resistance (tactile bump). |
| **3 mm** | **80%** (800 ‰) | Tactile drop-off; spring rapidly compresses toward the PCB pads. |
| **4 mm** | **99%** (990–1000 ‰) | Bottom-out; maximum measurable capacitance (peak). |

### ECK Defaults:
- **Factory thresholds:** with custom calibration and Rapid Trigger off (the defaults), each key uses PFU's factory press threshold and releases 50 counts below it. The two settings below apply only with custom calibration or Rapid Trigger on.
- **Actuation Point:** 75 % of the calibrated signal range (750 ‰, shown as 3.0 mm in VIA).
- **Release Hysteresis:** 10 % (100 ‰, shown as 0.4 mm).
- **Rapid Trigger:** Disabled by default. Can be enabled on demand under **HHKB Settings → Rapid Trigger**.
