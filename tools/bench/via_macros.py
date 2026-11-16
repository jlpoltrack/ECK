#!/usr/bin/env python3
"""Read or write the VIA macro buffer of an HHKB running ECK over USB.

  get            print the used part of the buffer (hex) and its macros
  set TEXT...    write each TEXT as macro 0, 1, ... (plain characters), rest zeroed
  clear          zero the whole buffer
Run: uv run --with hidapi python3 tools/via_macros.py get
"""
import sys
import time

GET_SIZE, GET_BUF, SET_BUF = 0x0D, 0x0E, 0x0F


def open_keyboard():
    import hid
    devs = [d for d in hid.enumerate() if d["usage_page"] == 0xFF60 and d["usage"] == 0x61
            and "HHKB" in (d["product_string"] or "")]
    if not devs:
        sys.exit("no HHKB running ECK found on USB")
    h = hid.device()
    h.open_path(devs[0]["path"])
    return h


def via(h, req):
    h.write(b"\x00" + req + bytes(32 - len(req)))
    resp = bytes(h.read(32, 1000))
    if not resp or resp[0] != req[0]:
        sys.exit(f"no VIA answer (got {resp.hex()})")
    return resp


def size(h):
    r = via(h, bytes([GET_SIZE]))
    return (r[1] << 8) | r[2]


def read_buf(h):
    n, out = size(h), bytearray()
    for off in range(0, n, 28):
        k = min(28, n - off)
        out += via(h, bytes([GET_BUF, off >> 8, off & 0xFF, k]))[4:4 + k]
    return bytes(out)


def write_buf(h, data):
    for off in range(0, len(data), 28):
        chunk = data[off:off + 28]
        via(h, bytes([SET_BUF, off >> 8, off & 0xFF, len(chunk)]) + chunk)


def main():
    h = open_keyboard()
    cmd = sys.argv[1] if len(sys.argv) > 1 else "get"
    n = size(h)
    if cmd == "get":
        buf = read_buf(h)
        used = buf.rstrip(b"\0")
        print(f"buffer {n} B, used {len(used)} B")
        for i, m in enumerate(buf.split(b"\0")[:16]):
            if m:
                print(f"M{i}: {m!r}")
    elif cmd in ("set", "clear"):
        texts = sys.argv[2:] if cmd == "set" else []
        data = b"".join(t.encode() + b"\0" for t in texts)
        if len(data) > n:
            sys.exit(f"{len(data)} B does not fit the {n} B buffer")
        write_buf(h, data + bytes(n - len(data)))
        time.sleep(0.1)
        print("written; the keyboard saves to EEPROM about 0.5 s after the last write")
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main()
