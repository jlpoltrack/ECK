"""Hybrid (multi-image) container handling in make_hfb.py and hhkb_flasher.py."""
import importlib.util
from pathlib import Path
import sys
import tempfile
import hashlib
import types
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.modules.setdefault('hid', types.ModuleType('hid'))


def load(name):
    spec = importlib.util.spec_from_file_location(name, ROOT / f'tools/{name}.py')
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


flasher = load('hhkb_flasher')
make_hfb = load('make_hfb')
crc = flasher.compute_crc16_mcrf4xx


def fake_hybrid(stm32=b'\x11' * 100, nrf_fill=b'\x22', model=b'AHUX01'):
    """Stock layout: CRCs, 64 KB STM32 section, 12 x FF, nRF MD5, nRF image, 16-byte trailer."""
    section = stm32.ljust(65536, b'\xff')
    nrf = nrf_fill * (flasher.HYBRID_HFB_SIZE - 4 - 65536 - 28 - 16)
    tail = b'\xff' * 12 + hashlib.md5(nrf).digest() + nrf + model + bytes([0x0A, 0, 4, 8]) + b'\xff' * 6
    body = crc(section).to_bytes(2, 'little') + section + tail
    return crc(body).to_bytes(2, 'little') + body


class HybridContainerTest(unittest.TestCase):
    def test_model_mapping(self):
        self.assertEqual(flasher.device_model('PD-KB800BS'), 'AHUX01')
        self.assertIsNone(flasher.device_model('PD-KB800B'))

    def test_validate_reads_trailer_model(self):
        self.assertEqual(flasher.validate_container(fake_hybrid(), 'AHUX01'), 'AHUX01')
        with self.assertRaises(ValueError):
            flasher.validate_container(fake_hybrid(), 'AHHS01')

    def test_hybrid_size_and_md5_enforced(self):
        good = fake_hybrid()
        with self.assertRaisesRegex(ValueError, 'exactly'):
            flasher.validate_container(good[:-1], 'AHUX01', check_crc=False)
        bad = bytearray(good)
        bad[4 + 65536 + 100] ^= 1
        bad[2:4] = crc(bad[4:4 + 65536]).to_bytes(2, 'little')
        bad[0:2] = crc(bad[2:]).to_bytes(2, 'little')
        with self.assertRaisesRegex(ValueError, 'MD5'):
            flasher.validate_container(bytes(bad), 'AHUX01')
        classic = flasher.wrap_binary_to_hfb(b'\x00\x50\x00\x20\x01\x00\x01\x08')
        with self.assertRaises(ValueError):
            flasher.validate_container(classic, 'AHUX01')

    def write_hex(self, path, image, base=0x1000):
        """Intel HEX of image at base, with extended-address records and 32-byte data lines."""
        lines, upper = [], None
        def rec(addr, kind, data):
            body = bytes([len(data)]) + addr.to_bytes(2, 'big') + bytes([kind]) + data
            lines.append(':' + (body + bytes([-sum(body) & 0xFF])).hex().upper())
        for off in range(0, len(image), 32):
            a = base + off
            if a >> 16 != upper:
                upper = a >> 16
                rec(0, 4, upper.to_bytes(2, 'big'))
            rec(a & 0xFFFF, 0, image[off:off + 32])
        rec(0, 1, b'')
        path.write_text('\n'.join(lines) + '\n')

    def softdevice(self):
        sd = bytearray(b'\x55' * 0x20000) + b'\xff' * 0x5000  # 0x1000..0x26000
        sd[0x2004:0x2008] = (0x51B1E5DB).to_bytes(4, 'little')
        return bytes(sd)

    def test_hybrid_layout(self):
        sd = self.softdevice()
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            self.write_hex(tmp / 'sd.hex', sd[:0x20000])  # trailing 0xFF left out, as in Nordic's file
            (tmp / 'eck.bin').write_bytes(b'\x33' * 300)
            (tmp / 'app.bin').write_bytes(b'\x44' * 500)
            make_hfb.make_hybrid_hfb(str(tmp / 'eck.bin'), str(tmp / 'app.bin'), str(tmp / 'sd.hex'),
                                     str(tmp / 'out.hfb'))
            out = (tmp / 'out.hfb').read_bytes()
        nrf = sd + b'\x44' * 500 + b'\xff' * (0x10000 - 500)
        section = b'\x33' * 300 + b'\xff' * (65536 - 300)
        body = (crc(section).to_bytes(2, 'little') + section + b'\xff' * 12 + hashlib.md5(nrf).digest() + nrf +
                b'AHUX01' + bytes([0x0A, 0, 4, 8]) + b'\xff' * 6)
        self.assertEqual(out, crc(body).to_bytes(2, 'little') + body)
        self.assertEqual(flasher.validate_container(out, 'AHUX01'), 'AHUX01')

    def test_hybrid_jis_model(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            self.write_hex(tmp / 'sd.hex', self.softdevice()[:0x20000])
            (tmp / 'eck.bin').write_bytes(b'\x33' * 300)
            (tmp / 'app.bin').write_bytes(b'\x44' * 500)
            make_hfb.make_hybrid_hfb(str(tmp / 'eck.bin'), str(tmp / 'app.bin'), str(tmp / 'sd.hex'),
                                     str(tmp / 'out.hfb'), 'A2.49', 'AHJX01')
            out = (tmp / 'out.hfb').read_bytes()
        self.assertEqual(out[-16:], b'AHJX01' + bytes([0x0A, 2, 4, 9]) + b'\xff' * 6)
        self.assertEqual(flasher.validate_container(out, 'AHJX01'), 'AHJX01')
        with self.assertRaises(ValueError):
            flasher.validate_container(out, 'AHUX01')

    def test_hybrid_rejects_bad_inputs(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            (tmp / 'eck.bin').write_bytes(b'\x33')
            (tmp / 'app.bin').write_bytes(b'\x44')
            (tmp / 'big.bin').write_bytes(b'\x44' * 0x10001)
            self.write_hex(tmp / 'sd.hex', self.softdevice())
            self.write_hex(tmp / 'nosd.hex', b'\x55' * 0x4000)
            args = [str(tmp / 'eck.bin'), str(tmp / 'app.bin'), str(tmp / 'sd.hex'), str(tmp / 'x.hfb')]
            with self.assertRaisesRegex(ValueError, 'exceeds'):
                make_hfb.make_hybrid_hfb(args[0], str(tmp / 'big.bin'), *args[2:])
            with self.assertRaisesRegex(ValueError, 'magic'):
                make_hfb.make_hybrid_hfb(args[0], args[1], str(tmp / 'nosd.hex'), args[3])
            text = (tmp / 'sd.hex').read_text().splitlines()
            text[3] = text[3][:-2] + ('00' if text[3][-2:] != '00' else '01')
            (tmp / 'sd.hex').write_text('\n'.join(text))
            with self.assertRaisesRegex(ValueError, 'checksum'):
                make_hfb.make_hybrid_hfb(*args)

if __name__ == '__main__':
    unittest.main()
