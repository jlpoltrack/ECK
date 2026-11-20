#!/usr/bin/env python3
"""One-command USB bench check of the Hybrid before a release (keyboard on USB, no keypresses).

Backs up the keyboard, builds and flashes the firmware, then checks over USB:
  - the reported version is in the flashed image; the USB serial is Vial's prefix and the chip ID, unchanged
  - the nRF52 reports no failed update and is not stuck in recovery mode
  - VIA keymap round trips (via_keymap_check.py) and EEPROM persistence (eeprom_save_test.py)
Options:
  --hfb FILE   flash this Hybrid .hfb instead of building one (e.g. a downloaded release)
  --perf       first build and flash a diagnostics build and measure scan time and rest noise
               (keep hands off the keyboard for ~15 s), then flash the normal build
  --nrf        also flash the nRF52 part and run its recovery-mode round trip
               (the orange LED blinks for up to 30 s, twice)
Run: uv run --with hidapi python3 tools/bench/release_check.py [--hfb FILE] [--perf] [--nrf]
"""
import argparse
import os
import re
import subprocess
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
from via_hid import open_keyboard, via  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
HFB = os.path.join(ROOT, "build/hybrid/hhkb_ec_hybrid.hfb")
BIN = os.path.join(ROOT, "build/hybrid/hhkb_ec_hybrid.bin")  # the STM32 alone: no nRF5 SDK needed
DIAG_DEFS = "HHKB_ADC_DIAGNOSTICS,HHKB_SCAN_DIAGNOSTICS"
VERSION_ID, NRF_ID = 28, 30   # VIA custom values (stm32/via/hhkb_via_controls.c)
UPD_RESTART, UPD_RECOVERY = 4, 5
# Baselines from AGENTS.md (Bench): interrupts-off scan half 408 us, rest noise median p-p 12-13
SCAN_HALF_WARN_US, SCAN_WHOLE_LIMIT_US, NOISE_WARN = 450, 1000, 16

results = []  # (name, "PASS" | "WARN" | "FAIL", detail)
LOG = []      # every command's full output, saved next to the backup


def report(name, status, detail=""):
    results.append((name, status, detail))
    print(f"[{status}] {name}" + (f": {detail}" if detail else ""), flush=True)


def run(cmd, timeout, quiet=False):
    """Run a command from the repo root; return (exit code, output)."""
    print("$ " + " ".join(cmd), flush=True)
    try:
        p = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True, timeout=timeout)
    except subprocess.TimeoutExpired:
        LOG.append(f"$ {' '.join(cmd)}  (timed out after {timeout} s)")
        return 124, f"timed out after {timeout} s"
    out = p.stdout + p.stderr
    LOG.append(f"$ {' '.join(cmd)}  (exit {p.returncode})\n{out}")
    if not quiet:
        print(out.rstrip())
    return p.returncode, out


def py(*args):
    return [sys.executable, *args]


def keyboard_info():
    """(version, serial, nRF failed, nRF recovery), waiting up to 20 s for the keyboard."""
    import hid
    h = open_keyboard(wait=20)
    try:
        ver = via(h, [0x08, 0, VERSION_ID])[3:].split(b"\0")[0].decode(errors="replace")
        nrf = via(h, [0x08, 0, NRF_ID], min_len=11)
        serial = next((d["serial_number"] for d in hid.enumerate(0xCAFE, 0x4010) if d["usage_page"] == 0xFF60), "") or ""
        return ver, serial, nrf[9], nrf[10]
    finally:
        h.close()


def build(defs=None):
    code, out = run(["./build.sh", "hybrid"] + (["-d", defs] if defs else []), 600, quiet=True)
    warnings = len(re.findall(r"warning:", out))
    if code or warnings:
        print(out[-3000:])
    report("build" + (f" ({defs})" if defs else ""), "FAIL" if code or warnings else "PASS",
           f"exit {code}, {warnings} warnings")
    return code == 0


def flash_stm32(hfb, label):
    code, out = run(py("tools/hybrid_update.py", "stm32", hfb), 180)
    report(f"flash STM32 ({label})", "PASS" if code == 0 else "FAIL", out.strip().splitlines()[-1] if out.strip() else "")
    return code == 0


def perf():
    code, out = run(py("tools/bench/key_depth.py", "scantime", "5"), 60)
    halves = [int(x) for x in re.findall(r"interrupts off: (\d+) us", out)]
    wholes = [int(x) for x in re.findall(r"whole scan: (\d+) us", out)]
    missed = [int(x) for x in re.findall(r"missed slots (\d+)", out)]
    if code or not halves or not wholes:
        report("scan time", "FAIL", "no readings (diagnostics build?)")
    else:
        half, whole, miss = max(halves), max(wholes), max(missed, default=0)  # missed: running total
        status = "FAIL" if whole >= SCAN_WHOLE_LIMIT_US else "WARN" if half > SCAN_HALF_WARN_US or miss else "PASS"
        report("scan time", status, f"interrupts-off half {half} us (baseline 408), whole scan {whole} us "
               f"(baseline ~910), missed 1 kHz slots {miss} (baseline 0)")
    code, out = run(py("tools/bench/key_depth.py", "noise", "8"), 60)
    m = re.search(r"median peak-to-peak ([\d.]+)", out)
    if code or not m:
        report("rest noise", "FAIL", "no readings")
    else:
        pp = float(m.group(1))
        pressed = "pressed during the run" in out
        report("rest noise", "WARN" if pp > NOISE_WARN or pressed else "PASS",
               f"median peak-to-peak {pp:g} counts (baseline 12-13)" + ("; keys were pressed" if pressed else ""))


def nrf_round_trip():
    """Recovery mode and back, as hybrid_update.py's nrf --recovery starts it."""
    h = open_keyboard()
    try:
        via(h, [0x07, 0, NRF_ID, UPD_RECOVERY])
        ok = False
        for _ in range(50):
            time.sleep(0.2)
            if via(h, [0x08, 0, NRF_ID], min_len=11)[10]:
                ok = True
                break
        via(h, [0x07, 0, NRF_ID, UPD_RESTART])
        cleared = False
        for _ in range(150):
            time.sleep(0.2)
            if not via(h, [0x08, 0, NRF_ID], min_len=11)[10]:
                cleared = True
                break
    finally:
        h.close()
    report("nRF52 recovery round trip", "PASS" if ok and cleared else "FAIL",
           f"entered recovery: {ok}, left it: {cleared}")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--hfb", help="flash this .hfb instead of building")
    ap.add_argument("--perf", action="store_true", help="measure scan time and noise on a diagnostics build")
    ap.add_argument("--nrf", action="store_true", help="also flash the nRF52 and test recovery mode")
    a = ap.parse_args()
    t0 = time.monotonic()
    try:
        checks(a)
    except SystemExit as e:  # via_hid and keyboard_info exit when the keyboard is missing or silent
        report("keyboard", "FAIL", str(e.code))
    if DIAG_INSTALLED:
        report("diagnostics build still installed", "FAIL", "flash a normal build before using the keyboard")
    return finish(t0)


DIAG_INSTALLED = False


def checks(a):
    global DIAG_INSTALLED
    minutes = 1 + a.perf + a.nrf  # measured: --nrf with --hfb 107 s (2026-11-19)
    print(f"Don't use the keyboard until the summary prints (about {minutes} min): typing during the run "
          "skews the measurements and can fail the EEPROM check.\n", flush=True)

    ver0, serial0, _, _ = keyboard_info()
    print(f"keyboard: ECK {ver0}, USB serial {serial0}")
    global BACKUP
    backup = BACKUP = os.path.join("backups", "release-check-" + time.strftime("%Y%m%d-%H%M%S"))
    code, _ = run(py("tools/hybrid_update.py", "backup", backup), 120)
    report("backup", "PASS" if code == 0 else "FAIL", backup)
    if code:
        return
    keyboard_info()  # the backup restarts the keyboard: wait until it is back

    if a.perf:
        if build(DIAG_DEFS) and flash_stm32(BIN, "diagnostics"):
            DIAG_INSTALLED = True
            keyboard_info()
            print("hands off the keyboard for ~15 s ...", flush=True)
            perf()

    hfb = a.hfb
    if not hfb:
        if not build():
            return
        hfb = HFB
    if not flash_stm32(hfb, os.path.basename(hfb)):
        return
    DIAG_INSTALLED = False
    keyboard_info()
    if a.nrf:
        print("nRF52 update: the orange LED blinks while it installs", flush=True)
        code, out = run(py("tools/hybrid_update.py", "nrf", hfb), 240)
        report("flash nRF52", "PASS" if code == 0 else "FAIL")
        time.sleep(15)  # PFU's bootloader installs the image, then the app reconnects

    ver, serial, failed, recovery = keyboard_info()
    image = open(hfb, "rb").read()
    report("version", "PASS" if ver and ver != "unknown" and ver.encode() in image else "FAIL",
           f"reports {ver!r}" + ("" if ver.encode() in image else ", not found in the image"))
    # Vial's serial prefix, then the chip ID; the ID must survive the flash (serial0 may predate Vial)
    now, before = re.fullmatch(r"vial:f64c2b3c-([0-9A-F]{24})", serial), re.search(r"([0-9A-F]{24})$", serial0)
    report("USB serial", "PASS" if now and (not before or now[1] == before[1]) else "FAIL", f"{serial} (was {serial0})")
    report("nRF52 status", "PASS" if not failed and not recovery else "FAIL", f"failed {failed}, recovery {recovery}")

    code, out = run(py("tools/bench/via_keymap_check.py"), 120)
    report("VIA keymap round trips", "PASS" if code == 0 else "FAIL")
    code, out = run(py("tools/bench/eeprom_save_test.py"), 240)
    report("EEPROM persistence", "PASS" if code == 0 and "PASS" in out else "FAIL")
    if a.nrf:
        print("nRF52 recovery round trip: the orange LED blinks up to 30 s, twice", flush=True)
        nrf_round_trip()


BACKUP = None


def finish(t0):
    if BACKUP and os.path.isdir(os.path.join(ROOT, BACKUP)):
        with open(os.path.join(ROOT, BACKUP, "release_check.log"), "w") as f:
            f.write("\n".join(LOG))
        print(f"\nfull output: {BACKUP}/release_check.log")
    print(f"\n== Summary ({time.monotonic() - t0:.0f} s)")
    for name, status, detail in results:
        print(f"  {status:4}  {name}" + (f": {detail}" if detail else ""))
    bad = any(s == "FAIL" for _, s, _ in results)
    print("RESULT:", "FAIL" if bad else "PASS")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
