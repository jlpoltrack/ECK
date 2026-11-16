"""Bank 1 boot stub check in hybrid_update.py (the stm32 command refuses without the stub)."""
import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location('hybrid_update', Path(__file__).resolve().parents[1] / 'tools/hybrid_update.py')
upd = importlib.util.module_from_spec(spec)
spec.loader.exec_module(upd)


def vectors(sp, reset):
    return sp.to_bytes(4, 'little') + reset.to_bytes(4, 'little')


class BootStubCheck(unittest.TestCase):
    def test_stub_accepted(self):
        stub = vectors(0x20004000, 0x08000075) + b'\x00' * 712
        self.assertTrue(upd.has_boot_stub(stub.ljust(0x2000, b'\xff')))

    def test_full_firmware_refused(self):
        # PFU's Bank 1: its reset vector is also low (0x080000d5), but code fills the bank
        self.assertFalse(upd.has_boot_stub(vectors(0x20005000, 0x080000d5) + b'\x5a' * (0x2000 - 8)))
        self.assertFalse(upd.has_boot_stub(vectors(0x20004000, 0x08000075) + b'\x5a' * (0x2000 - 8)))

    def test_erased_or_short_refused(self):
        self.assertFalse(upd.has_boot_stub(b'\xff' * 0x2000))
        self.assertFalse(upd.has_boot_stub(vectors(0x20004000, 0x08000075).ljust(0x1000, b'\xff')))


if __name__ == '__main__':
    unittest.main()
