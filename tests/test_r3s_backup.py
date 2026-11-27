"""r3s_backup.restore never writes to a keyboard in ST's DFU loader unless Bank 1 holds Topre's updater."""
import os
import sys
import tempfile
import unittest
from unittest import mock

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools", "r3s"))
sys.modules.setdefault("hid", mock.MagicMock())
import r3s_backup as R  # noqa: E402

SECTION = R.SECTION


def rfb():
    """A valid .rfb: vectors in range, both CRCs right."""
    image = bytearray(b"\xff" * SECTION)
    image[0:8] = (0x20005000).to_bytes(4, "little") + (0x08010101).to_bytes(4, "little")
    body = R.crc16(bytes(image)).to_bytes(2, "little") + bytes(image) + bytes(28)
    return R.crc16(body).to_bytes(2, "little") + body


class TopreCheck(unittest.TestCase):
    def setUp(self):
        f = tempfile.NamedTemporaryFile(suffix=".rfb", delete=False)
        f.write(rfb())
        f.close()
        self.path = f.name

    def tearDown(self):
        os.unlink(self.path)

    def run_restore(self, bank1):
        reads = {0x08000000: bank1, R.TAG_WORD: b"\x01\x02\x03\x04"}
        with mock.patch.object(R, "dfu_present", return_value=True), \
                mock.patch.object(R.dfu, "read", side_effect=lambda alt, addr, n: reads[addr][:n]), \
                mock.patch.object(R.dfu, "write_verified") as wv, mock.patch.object(R.dfu, "leave"):
            R.restore(self.path)
        return wv

    def test_refuses_an_hhkb(self):
        stub = bytes(range(256)) * 12 + b"\xff" * (SECTION - 3072)       # ECK's boot stub: no "Topre"
        with self.assertRaises(SystemExit) as e:
            self.run_restore(stub)
        self.assertIn("not the R3S", str(e.exception))

    def test_writes_the_r3s(self):
        bank1 = b"\x00" * 100 + b"Topre Corporation" + b"\x00" * (SECTION - 117)
        wv = self.run_restore(bank1)
        self.assertEqual([c.args[1] for c in wv.call_args_list], [R.TAG_WORD, 0x08010000, R.TAG_WORD])


if __name__ == "__main__":
    unittest.main()
