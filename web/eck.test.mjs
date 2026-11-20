// Node checks of the pure parts against the Python tools: node web/eck.test.mjs [file.hfb ...]
import { readFileSync } from 'node:fs';
import { execFileSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import assert from 'node:assert/strict';
import { crc16mcrf4xx, md5, parseHfb, hasBootStub, deviceModel, isTopreBank1, RFB_SIZE } from './eck.js';

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
assert.equal(deviceModel('PD-KB800BS'), 'AHUX01');
const bank1 = new Uint8Array(0x10000).fill(0xff);
assert.ok(!isTopreBank1(bank1));
bank1.set(new TextEncoder().encode('Topre Corporation'), 31804);
assert.ok(isTopreBank1(bank1));
for (const f of process.argv.slice(2)) {
    const h = parseHfb(new Uint8Array(readFileSync(f)));
    console.log(f, h.model, h.version, h.hybrid ? 'hybrid' : 'classic');
}
console.log('eck.js checks passed');
