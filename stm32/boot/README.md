# hybrid_boot: ECK boot stub for the Hybrid's Bank 1

Replaces PFU's updater in Bank 1 (`0x08000000`, 1.4 KB). At reset it starts:

1. ST's USB DFU loader (chip ROM) if the power switch (PC5) is held while USB power is present (PC13 low), without writing flash;
2. a Bluetooth-staged image ([`bank1_stage.h`](../hybrid/bank1_stage.h)), when its header and CRC check out: copied from Bank 1 (`0x08001000`) to Bank 2, then the boot tags set and the header erased (a power cut repeats the copy; a bad image is dropped);
3. ST's loader if Bank 2 is not valid, otherwise ECK in Bank 2 (`0x08010000`).

The stub stays under 3 KB: tools recognise it by `0xFF` at `0xC00-0xFFF`, as Bank 1 above `0x1000` may hold staged data. `tools/bench/boot_stub_emu.py` runs it against a flash-controller model.

Bank 2 is valid when its vectors are sane, EEPROM `0x08080360` = 1 and `0x08080370` holds the CRC-16/MCRF4XX of the 64 KB bank: the same tag PFU's Bank 1 checks, so either Bank 1 boots ECK.

ST's loader jumps back into a valid bank unless BOOT0 selected it, so `../hybrid/rom_dfu.h` runs the ROM's data init and enters the loader past that check. It does nothing on an unknown ROM revision.

Build with `make`; install with `tools/hybrid_update.py boot`; `hybrid_update.py restore-stm32` writes PFU's Bank 1 back from a backup ([Hybrid updates](../../docs/hybrid-update.md)).
