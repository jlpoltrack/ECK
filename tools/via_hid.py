"""Find an HHKB running ECK by its VIA raw HID interface and exchange 32-byte VIA reports (hidapi)."""
import sys
import time


def open_keyboard(required=True, wait=0.0):
    """Open the keyboard's VIA interface, retrying for up to wait seconds; None if absent and not required."""
    try:
        import hid
    except ImportError:
        sys.exit("run with: uv run --with hidapi python3 ...")
    deadline = time.monotonic() + wait
    while True:
        devs = [d for d in hid.enumerate() if d["usage_page"] == 0xFF60 and d["usage"] == 0x61
                and "HHKB" in (d["product_string"] or "")]
        if devs:
            h = hid.device()
            h.open_path(devs[0]["path"])
            return h
        if time.monotonic() >= deadline:
            if required:
                sys.exit("no HHKB running ECK found on USB")
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
