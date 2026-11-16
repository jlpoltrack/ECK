# hybrid_boot: ECK boot stub for the HHKB Hybrid's STM32 Bank 1

Replaces PFU's updater in Bank 1 (`0x08000000`, under 1 KB). At reset it:

1. starts ST's USB DFU loader (in the chip's ROM) when the power switch (PC5) is held while USB power is present (PC13 low), or when Bank 2 is not valid;
2. otherwise starts ECK in Bank 2 (`0x08010000`).

Bank 2 is valid when its vectors are sane, EEPROM `0x08080360` = 1 and `0x08080370` holds the CRC-16/MCRF4XX of the 64 KB bank (the same tag PFU's Bank 1 checks, so either Bank 1 boots ECK).

ST's loader jumps back into a valid bank unless the BOOT0 pin selected it, so `../hybrid/rom_dfu.h` runs the ROM's data init and enters the loader after that check. It checks the ROM's code first and does nothing on an unknown ROM revision.

Build with `make` (Arm GNU toolchain); install with `tools/hybrid_update.py boot`. Restoring stock (`hybrid_update.py restore-stm32`) writes PFU's Bank 1 back from a backup.
