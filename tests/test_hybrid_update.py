"""Bank 1 boot stub and layout checks in hybrid_update.py (the stm32 command refuses on either)."""
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



class FakeKeyboard:
    def __init__(self, product):
        self.product = product

    def get_product_string(self):
        return self.product

    def close(self):
        pass


class LayoutCheck(unittest.TestCase):
    def check(self, product, sec):
        saved = upd.open_keyboard
        upd.open_keyboard = lambda required=True: None if product is None else FakeKeyboard(product)
        try:
            upd.check_layout(b"Hybrid JIS" in sec)
        finally:
            upd.open_keyboard = saved

    def test_matching_layouts_pass(self):
        self.check("HHKB Professional Hybrid", b"..HHKB Professional Hybrid\0..")
        self.check("HHKB Professional Hybrid JIS", b"..HHKB Professional Hybrid JIS\0..")
        self.check(None, b"..HHKB Professional Hybrid JIS\0..")  # in DFU: nothing to compare

    def test_mismatch_refused(self):
        with self.assertRaisesRegex(SystemExit, "JIS layout"):
            self.check("HHKB Professional Hybrid", b"..HHKB Professional Hybrid JIS\0..")
        with self.assertRaisesRegex(SystemExit, "ANSI layout"):
            self.check("HHKB Professional Hybrid JIS", b"..HHKB Professional Hybrid\0..")

    def test_hfb_model_names_layout(self):
        trailer = lambda model: model + bytes([0x02, 0x00, 0x04, 0x09]) + b"\xff" * 6
        self.assertTrue(upd.hfb_is_jis(b"\0" * 100 + trailer(b"AHJX01")))
        self.assertFalse(upd.hfb_is_jis(b"\0" * 100 + trailer(b"AHUX01")))


if __name__ == '__main__':
    unittest.main()
