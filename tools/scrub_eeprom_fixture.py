#!/usr/bin/env python3
"""Replace the VIA macro text in a Hybrid data-EEPROM dump with dummy text, keeping slots valid.

Usage: scrub_eeprom_fixture.py <in.bin> <out.bin>. Layout: stm32/via/hhkb_keymap_storage.c (DMK2 slots).
"""
import struct
import sys

SLOT_OFFSET = 0x600
KEYMAP_WORDS, RESERVED_WORDS, MACRO_WORDS = 240, 20, 56
PAYLOAD_WORDS = KEYMAP_WORDS + RESERVED_WORDS + MACRO_WORDS
SLOT_WORDS = 4 + PAYLOAD_WORDS
MAGIC_V2, COMMIT = 0x324B4D44, 0x54494D43
DUMMY = b"example"


def hash_word(h, w):
    for _ in range(4):
        h = ((h ^ (w & 0xFF)) * 16777619) & 0xFFFFFFFF
        w >>= 8
    return h


def slot_hash(words):
    h = hash_word(2166136261, words[1])
    for w in words[4:4 + PAYLOAD_WORDS]:
        h = hash_word(h, w)
    return h


def main():
    data = bytearray(open(sys.argv[1], "rb").read())
    secrets = []
    for n in range(2):
        base = SLOT_OFFSET + n * SLOT_WORDS * 4
        words = list(struct.unpack_from(f"<{SLOT_WORDS}I", data, base))
        if words[0] != MAGIC_V2 or words[3] != COMMIT or words[2] != slot_hash(words):
            print(f"slot {n}: not a valid DMK2 slot, left alone")
            continue
        start = base + (4 + KEYMAP_WORDS + RESERVED_WORDS) * 4
        macro = data[start:start + MACRO_WORDS * 4]
        secrets += [run for run in bytes(macro).split(b"\0") if len(run) >= 4]
        k, i = 0, 0
        while i < len(macro):
            if macro[i] == 0x01:  # action prefix: tap/down/up + keycode, or delay digits up to '|'
                i += 2
                if macro[i - 1] == 0x04:
                    while i < len(macro) and macro[i] != ord("|"):
                        i += 1
                i += 1
                continue
            if 0x20 <= macro[i] <= 0x7E:
                macro[i] = DUMMY[k % len(DUMMY)]
                k += 1
            i += 1
        data[start:start + len(macro)] = macro
        words = list(struct.unpack_from(f"<{SLOT_WORDS}I", data, base))
        words[2] = slot_hash(words)
        struct.pack_into("<I", data, base + 8, words[2])
        print(f"slot {n}: {k} macro text bytes replaced, checksum updated")
    for s in secrets:
        text = bytes(c for c in s if 0x20 <= c <= 0x7E)
        if len(text) >= 4 and text in data:
            sys.exit("original macro text still present elsewhere in the dump")
    open(sys.argv[2], "wb").write(data)


if __name__ == "__main__":
    main()
