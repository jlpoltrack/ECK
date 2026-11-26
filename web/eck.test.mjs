// Node checks of the pure parts against the Python tools: node web/eck.test.mjs [file.hfb ...]
import { readFileSync } from 'node:fs';
import { execFileSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import assert from 'node:assert/strict';
import { crc16mcrf4xx, md5, parseHfb, hasBootStub, deviceModel, isTopreBank1, bank2Layout, eepromLayout, stm32Update, isEck, RFB_SIZE } from './eck.js';

const repo = fileURLToPath(new URL('..', import.meta.url));
const py = (code) => execFileSync('python3', ['-c', code], { cwd: repo }).toString().trim();
for (const len of [0, 1, 55, 56, 63, 64, 65, 1000, 70000]) {
    const data = Uint8Array.from({ length: len }, (_, i) => (i * 131 + 7) & 0xff);
    const src = `bytes((i*131+7)&255 for i in range(${len}))`;
    assert.equal(Buffer.from(md5(data)).toString('hex'), py(`import hashlib;print(hashlib.md5(${src}).hexdigest())`), `md5 ${len}`);
    assert.equal(crc16mcrf4xx(data), Number(py(`import sys;sys.path.insert(0,'tools');from make_hfb import compute_crc16_mcrf4xx as c;print(c(${src}))`)), `crc ${len}`);
}
const stub = new Uint8Array(0x2000).fill(0xff);
stub.set([0x00, 0x40, 0x00, 0x20, 0x75, 0x00, 0x00, 0x08]);
assert.ok(hasBootStub(stub));
stub[0x1500] = 0;
assert.ok(!hasBootStub(stub));
// R3S: an .rfb from tools/make_hfb.py parses with its model and version; the Bank 1 check
const rfb = Buffer.from(py(`import sys;sys.path.insert(0,'tools');from make_hfb import wrap_binary_to_rfb as w;` +
    `print(w(bytes.fromhex('0050002001010108') + bytes(1000)).hex())`), 'hex');
assert.equal(rfb.length, RFB_SIZE);
const r = parseHfb(new Uint8Array(rfb));
assert.deepEqual([r.model, r.version, r.hybrid, r.stm32.length], ['R3SD####', 'A0.11', false, 0x10000]);
rfb[100] ^= 1;
assert.throws(() => parseHfb(new Uint8Array(rfb)), /CRC/);
assert.equal(deviceModel('R3SD13'), 'R3SD####');
for (const t of ['PD-KB800BS', 'PD-KB800B', 'PD-KB800WN', 'PD-KB800YNS', 'PD-KB800WSC1']) assert.equal(deviceModel(t), 'AHUX01');
for (const t of ['PD-KB401W', 'PD-KB401BN']) assert.equal(deviceModel(t), 'AHHX01');
for (const t of ['PD-KB401YSC', 'PD-KB401BNSC']) assert.equal(deviceModel(t), 'AHHS01');
for (const t of ['PD-KB820WS', 'PD-KB820B', 'PD-KB820YSC1']) assert.equal(deviceModel(t), 'AHJX01');
for (const t of ['PD-KB421BSC', 'PD-KB421WNSC']) assert.equal(deviceModel(t), 'AHKS01');
for (const t of ['PD-KB800', 'PD-KB800BSS', 'PD-KB800YSC1X', 'PD-KB421W', 'PD-KB820WSC1X', 'PD-KB401Y']) assert.equal(deviceModel(t), null);
const enc = (s) => { const b = new Uint8Array(0x10000); b.set(new TextEncoder().encode(s), 0x8000); return b; };
assert.equal(bank2Layout(enc('HHKB Professional Hybrid JIS')), true);
assert.equal(bank2Layout(enc('HHKB Professional Hybrid')), false);
assert.equal(bank2Layout(new Uint8Array(0x10000).fill(0xff)), null);
assert.ok(isEck({ vendorId: 0xcafe, productId: 0x4012, productName: 'HHKB Professional Hybrid JIS', collections: [{ usagePage: 0xff60 }] }));
assert.ok(!isEck({ vendorId: 0xcafe, productId: 0x4010, productName: 'HHKB-Hybrid_1', collections: [{ usagePage: 0xff60 }] }));
const eepromWith = (off) => { const e = new Uint8Array(0x1800); e.set(new TextEncoder().encode('CMITVIAL'), off); return e; };
assert.equal(eepromLayout(eepromWith(0x1608)), false);
assert.equal(eepromLayout(eepromWith(0x1644)), true);
assert.equal(eepromLayout(new Uint8Array(0x1800)), null);

// stm32Update's layout check on a fake DFU loader, with the built Hybrid images when present
const built = (p) => { try { return new Uint8Array(readFileSync(new URL(`../${p}`, import.meta.url))); } catch { return null; } };
const ansiHfb = built('build/hybrid/hhkb_ec_hybrid.hfb'), jisHfb = built('build/hybrid_jis/hhkb_ec_hybrid_jis.hfb');
const fakeDfu = (bank2, eeprom) => ({
    writes: 0,
    async read(alt, addr, n) { return alt === 2 ? eeprom.subarray(0, n) : addr === 0x08010000 ? bank2 : stub.subarray(0, 0x2000).slice(); },
    async writeVerified() { this.writes++; },
    async leave() {},
});
if (ansiHfb && jisHfb) {
    stub.fill(0xff); stub.set([0x00, 0x40, 0x00, 0x20, 0x75, 0x00, 0x00, 0x08]); // Bank 1 holds the boot stub
    const opts = { log: () => {}, progress: () => {} };
    const blank = new Uint8Array(0x10000).fill(0xff);
    let d = fakeDfu(enc('HHKB Professional Hybrid'), new Uint8Array(0x1800));
    await assert.rejects(stm32Update(d, jisHfb, opts), /set up for the US layout/);
    assert.equal(d.writes, 0);
    d = fakeDfu(blank, eepromWith(0x1644)); // Bank 2 erased: the EEPROM tells
    await assert.rejects(stm32Update(d, ansiHfb, opts), /set up for the JIS layout/);
    await stm32Update(d, ansiHfb, { ...opts, layoutChange: true }); // confirmed change
    assert.ok(d.writes > 0);
    d = fakeDfu(blank, new Uint8Array(0x1800)); // nothing tells: allowed
    await stm32Update(d, jisHfb, opts);
} else {
    console.log('stm32Update layout check skipped: build hybrid and hybrid_jis first');
}
const bank1 = new Uint8Array(0x10000).fill(0xff);
assert.ok(!isTopreBank1(bank1));
bank1.set(new TextEncoder().encode('Topre Corporation'), 31804);
assert.ok(isTopreBank1(bank1));
for (const f of process.argv.slice(2)) {
    const h = parseHfb(new Uint8Array(readFileSync(f)));
    console.log(f, h.model, h.version, h.hybrid ? 'hybrid' : 'classic');
}
console.log('eck.js checks passed');
