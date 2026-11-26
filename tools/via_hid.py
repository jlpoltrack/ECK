"""Find an HHKB running ECK by its VIA raw HID interface and exchange 32-byte VIA reports (hidapi).
ECK_PRODUCT (default "HHKB") picks another ECK keyboard by its product string, e.g. ECK_PRODUCT=R3S."""
import os
import sys
import time

PRODUCT = os.environ.get("ECK_PRODUCT", "HHKB")
# ECK_BLUETOOTH=1: the Hybrid's Bluetooth VIA interface instead of USB (bench checks only: it refuses updates)
BLUETOOTH = os.environ.get("ECK_BLUETOOTH") == "1"


def open_keyboard(required=True, wait=0.0):
    """Open the keyboard's VIA interface, retrying for up to wait seconds; None if absent and not required."""
    try:
        import hid
    except ImportError:
        sys.exit("run with: uv run --with hidapi python3 ...")
    deadline = time.monotonic() + wait
    while True:
        # USB unless asked: the Hybrid's Bluetooth VIA interface refuses updates (hidapi bus type 2)
        devs = [d for d in hid.enumerate() if d["usage_page"] == 0xFF60 and d["usage"] == 0x61
                and PRODUCT in (d["product_string"] or "") and (d.get("bus_type") == 2) == BLUETOOTH]
        if devs:
            h = hid.device()
            h.open_path(devs[0]["path"])
            return h
        if time.monotonic() >= deadline:
            if required:
                sys.exit(f"no {PRODUCT} running ECK found on {'Bluetooth' if BLUETOOTH else 'USB'}")
            return None
        time.sleep(0.5)


def send(h, req):
    """Send one VIA request (zero padded); return the reply, empty if none came within 1 s."""
    req = bytes(req)
    h.write(b"\x00" + req + bytes(32 - len(req)))
    return bytes(h.read(32, 1000))


def via(h, req, min_len=1):
    """send(), exiting unless the reply echoes the command byte and has at least min_len bytes."""
    resp = send(h, req)
    if len(resp) < min_len or resp[0] != bytes(req)[0]:
        sys.exit(f"no VIA answer to {bytes(req)[0]:#04x} (got {resp.hex() or 'nothing'})")
    return resp
