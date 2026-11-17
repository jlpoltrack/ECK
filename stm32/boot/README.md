# hybrid_boot: ECK boot stub for the Hybrid's Bank 1

Replaces PFU's updater in Bank 1 (`0x08000000`, under 1 KB). At reset it starts:

1. ST's USB DFU loader (chip ROM) if the power switch (PC5) is held while USB power is present (PC13 low), or if Bank 2 is not valid;
2. otherwise ECK in Bank 2 (`0x08010000`).

Bank 2 is valid when its vectors are sane, EEPROM `0x08080360` = 1 and `0x08080370` holds the CRC-16/MCRF4XX of the 64 KB bank: the same tag PFU's Bank 1 checks, so either Bank 1 boots ECK.

ST's loader jumps back into a valid bank unless BOOT0 selected it, so `../hybrid/rom_dfu.h` runs the ROM's data init and enters the loader past that check. It does nothing on an unknown ROM revision.

Build with `make`; install with `tools/hybrid_update.py boot`; `hybrid_update.py restore-stm32` writes PFU's Bank 1 back from a backup ([Hybrid updates](../../docs/hybrid-update.md)).
