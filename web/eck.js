// ECK web flasher: protocols and update flows, no UI. Ports of tools/hhkb_flasher.py (PFU updater),
// tools/hybrid_update.py and tools/r3s/r3s_backup.py (ST ROM DFU, nRF52 over VIA). Chrome/Edge (WebHID, WebUSB).

// ---------- containers ----------

export const CLASSIC_HFB_SIZE = 65570;
export const HYBRID_HFB_SIZE = 282672;
export const RFB_SIZE = 65568; // Realforce R3S: the .hfb layout with a 28-byte trailer
export const SECTION = 0x10000;
const NRF_MD5 = 0x10010, NRF_IMAGE = 0x10020, NRF_SIZE = 0x35000, NRF_BASE = 0x1000, NRF_APP = 0x26000;
export const BANK1 = 0x08000000, BANK2 = 0x08010000, EEPROM = 0x08080000, EEPROM_SIZE = 0x1800;
const TAG_VALID = 0x08080360, TAG_CRC = 0x08080370;
const R3S_TAG = 0x080817e0; // Topre's Bank 2 tag: CRC-16 of the image in bytes 2-3, then a word kept as is

export function crc16mcrf4xx(data) {
    let crc = 0xffff;
    for (const b of data) {
        let cur = (crc ^ b) & 0xff;
        for (let i = 0; i < 8; i++) cur = cur & 1 ? (cur >>> 1) ^ 0x8408 : cur >>> 1;
        crc = (crc >>> 8) ^ cur;
    }
    return crc;
}

export function md5(data) {
    const r = [7, 12, 17, 22, 5, 9, 14, 20, 4, 11, 16, 23, 6, 10, 15, 21];
    const k = Array.from({ length: 64 }, (_, i) => Math.floor(Math.abs(Math.sin(i + 1)) * 2 ** 32) >>> 0);
    const padLen = ((data.length + 8) >>> 6) * 64 + 64;
    const buf = new Uint8Array(padLen);
    buf.set(data);
    buf[data.length] = 0x80;
    const dv = new DataView(buf.buffer);
    dv.setUint32(padLen - 8, (data.length * 8) >>> 0, true);
    dv.setUint32(padLen - 4, Math.floor(data.length / 0x20000000), true);
    const h = [0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476];
    for (let off = 0; off < padLen; off += 64) {
        let [a, b, c, d] = h;
        for (let i = 0; i < 64; i++) {
            let f, g;
            if (i < 16) [f, g] = [(b & c) | (~b & d), i];
            else if (i < 32) [f, g] = [(d & b) | (~d & c), (5 * i + 1) % 16];
            else if (i < 48) [f, g] = [b ^ c ^ d, (3 * i + 5) % 16];
            else [f, g] = [c ^ (b | ~d), (7 * i) % 16];
            const t = d;
            d = c;
            c = b;
            const x = (a + f + k[i] + dv.getUint32(off + 4 * g, true)) >>> 0;
            const s = r[(i >>> 4) * 4 + (i % 4)];
            b = (b + ((x << s) | (x >>> (32 - s)))) >>> 0;
            a = t;
        }
        h[0] = (h[0] + a) >>> 0;
        h[1] = (h[1] + b) >>> 0;
        h[2] = (h[2] + c) >>> 0;
        h[3] = (h[3] + d) >>> 0;
    }
    const out = new Uint8Array(16);
    const ov = new DataView(out.buffer);
    h.forEach((w, i) => ov.setUint32(4 * i, w, true));
    return out;
}

const u32 = (b, o) => (b[o] | (b[o + 1] << 8) | (b[o + 2] << 16) | (b[o + 3] << 24)) >>> 0;
const le32 = (v) => new Uint8Array([v & 0xff, (v >>> 8) & 0xff, (v >>> 16) & 0xff, (v >>> 24) & 0xff]);
const equal = (a, b) => a.length === b.length && a.every((x, i) => x === b[i]);
const hex = (v) => '0x' + (v >>> 0).toString(16).padStart(8, '0');

/** Checks a .hfb (or an R3S .rfb) and returns {model, version, hybrid, stm32 (64 KB section), nrf?, nrfMd5?}. */
export function parseHfb(data) {
    const hybrid = data.length === HYBRID_HFB_SIZE, rfb = data.length === RFB_SIZE;
    if (!hybrid && !rfb && data.length !== CLASSIC_HFB_SIZE) throw new Error(`not an .hfb or .rfb (${data.length} bytes)`);
    if ((data[0] | (data[1] << 8)) !== crc16mcrf4xx(data.subarray(2))) throw new Error('outer CRC does not match');
    const stm32 = data.subarray(4, 4 + SECTION);
    if ((data[2] | (data[3] << 8)) !== crc16mcrf4xx(stm32)) throw new Error('STM32 CRC does not match');
    const trailer = data.subarray(data.length - 16);
    const model = new TextDecoder().decode(trailer.subarray(0, rfb ? 8 : 6));
    const v = trailer.subarray(rfb ? 8 : 6, rfb ? 12 : 10);
    const version = `${v[0] === 0x0b ? 'B' : 'A'}${v[1].toString(16)}.${v[2].toString(16)}${v[3].toString(16)}`.toUpperCase();
    checkVectors(stm32, BANK2, 'STM32');
    const out = { model, version, hybrid, stm32 };
    if (hybrid) {
        const nrf = data.subarray(NRF_IMAGE, NRF_IMAGE + NRF_SIZE);
        const nrfMd5 = data.subarray(NRF_MD5, NRF_IMAGE);
        if (!equal(md5(nrf), nrfMd5)) throw new Error('nRF52 MD5 does not match');
        const app = NRF_APP - NRF_BASE, sp = u32(nrf, app), reset = u32(nrf, app + 4);
        if (sp >>> 24 !== 0x20 || reset < NRF_APP || reset >= NRF_BASE + NRF_SIZE)
            throw new Error(`nRF52 app vectors look wrong (SP ${hex(sp)}, reset ${hex(reset)})`);
        Object.assign(out, { nrf, nrfMd5 });
    }
    return out;
}

function checkVectors(sec, base, what) {
    const sp = u32(sec, 0), reset = u32(sec, 4);
    if (sp >>> 24 !== 0x20 || reset < base || reset >= base + SECTION)
        throw new Error(`${what} vectors look wrong (SP ${hex(sp)}, reset ${hex(reset)})`);
}

/** Start of Bank 1: the ECK boot stub (under 3 KB) has its vectors in its first 4 KB, padded with 0xFF
 * from STUB_MAX; Bluetooth updates stage images from 0x1000, so the rest may hold data. */
const STUB_MAX = 0xc00;
const STAGE_HDR = 0x0800ff80, STAGE_MAGIC = 0x55534345; // stm32/hybrid/bank1_stage.h
const stubInstalls = (b) => {
    for (let i = 0; i + 4 <= Math.min(b.length, STUB_MAX); i += 4) if (u32(b, i) === STAGE_MAGIC) return true;
    return false;
};
export function hasBootStub(head) {
    if (head.length < 0x1000) return false;
    const sp = u32(head, 0), reset = u32(head, 4);
    return sp >>> 24 === 0x20 && reset >= BANK1 && reset < BANK1 + 0x1000 && head.subarray(STUB_MAX, 0x1000).every((b) => b === 0xff);
}

/** Bank 1 of a Realforce R3S (Topre's updater) holds "Topre"; an HHKB's (PFU's updater or ECK's stub) doesn't. */
export const isTopreBank1 = (bank1) => new TextDecoder('latin1').decode(bank1).includes('Topre');

/** Keyboard type number (PFU info) to container model. */
export function deviceModel(type) {
    const n = type.toUpperCase().trim();
    // Whole families, as tools/hhkb_flasher.py MODEL_PATTERNS
    if (/^PD-KB401[BWY]N?SC[0-9]*$/.test(n)) return 'AHHS01';
    if (/^PD-KB401[BW]N?$/.test(n)) return 'AHHX01';
    if (/^PD-KB800[BWY]N?(?:S(?:C[0-9]*)?)?$/.test(n)) return 'AHUX01';
    if (/^PD-KB421[BWY]N?SC[0-9]*$/.test(n)) return 'AHKS01'; // JIS, experimental
    if (/^PD-KB820[BWY]N?(?:S(?:C[0-9]*)?)?$/.test(n)) return 'AHJX01'; // JIS, experimental
    if (n === 'R3SD13') return 'R3SD####'; // Realforce R3S US TKL, under Topre's updater
    return null;
}

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

// ---------- PFU updater (stock firmware or Bank 1), WebHID ----------

export const PFU_FILTER = { vendorId: 0x04fe, usagePage: 0xff00, usage: 0x01 };
export const TOPRE_FILTER = { vendorId: 0x0853, usagePage: 0xff00 }; // Topre's updater: PFU's protocol
export const isUpdater = (d, f = PFU_FILTER) => d.vendorId === f.vendorId && d.collections.some((c) => c.usagePage === 0xff00);

export class Pfu {
    constructor(dev) {
        this.dev = dev;
        this.queue = [];
        dev.addEventListener('inputreport', (e) => this.queue.push(new Uint8Array(e.data.buffer)));
    }
    static async request() {
        const [dev] = await navigator.hid.requestDevice({ filters: [PFU_FILTER] });
        if (!dev) throw new Error('no keyboard selected');
        return Pfu.open(dev);
    }
    static async open(dev) {
        if (!dev.opened) await dev.open();
        return new Pfu(dev);
    }
    async cmd(bytes, timeoutMs) {
        const pkt = new Uint8Array(64);
        pkt.set([0xaa, 0xaa, ...bytes]);
        this.queue.length = 0;
        await this.dev.sendReport(0, pkt);
        const end = Date.now() + timeoutMs;
        while (Date.now() < end) {
            if (this.queue.length) {
                const r = this.queue.shift();
                if (r[0] !== 0x55 || r[1] !== 0x55 || r[2] !== bytes[0])
                    throw new Error(`unexpected answer to ${bytes[0].toString(16)}: ${[...r.subarray(0, 6)].map((x) => x.toString(16)).join(' ')}`);
                return r;
            }
            await sleep(5);
        }
        throw new Error(`no answer to command ${bytes[0].toString(16)}`);
    }
    async info() {
        const r = await this.cmd([0x02, 0, 0, 0], 3000);
        // Up to the first control byte: Topre's updater follows R3SD13 with 0x10, PFU's with 0
        const type = new TextDecoder().decode(r.subarray(6, 26)).split(/[\0-\x1f]/)[0].trim();
        return { type, model: deviceModel(type), bank2Broken: r[62] === 1 };
    }
    async close() {
        try {
            await this.dev.close();
        } catch {}
    }
}

/** Install a .hfb (.rfb) through PFU's (Topre's) updater. reconnect(): returns the re-enumerated updater's HIDDevice. */
export async function pfuInstall(pfu, data, { reconnect, log, progress }) {
    const hfb = parseHfb(data);
    const info = await pfu.info();
    log(`Keyboard: ${info.type}`);
    if (!info.model) throw new Error(`unsupported keyboard model ${info.type}`);
    if (info.model !== hfb.model) throw new Error(`firmware is for ${hfb.model}, keyboard is ${info.model}`);
    await pfu.cmd([0xe0, 0, 0, 0], 3000);
    await pfu.close();
    log('Waiting for the updater to reconnect (about 10 s)...');
    await sleep(10000);
    pfu = await Pfu.open(await reconnect());
    const size = le32(data.length);
    await pfu.cmd([0xe1, 0x00, 0x08, ...size, data[0], data[1], 0, 0], hfb.hybrid ? 60000 : 15000);
    await sleep(200);
    const payload = data.subarray(2);
    for (let off = 0, seq = 0; off < payload.length; off += 57, seq++) {
        const chunk = payload.subarray(off, off + 57);
        await pfu.cmd([0xe2, 0x00, chunk.length + 2, seq & 0xff, seq >> 8, ...chunk], hfb.hybrid ? 10000 : 3000);
        progress(Math.min(off + 57, payload.length) / payload.length);
    }
    await sleep(200);
    try {
        await pfu.cmd([0xe3, 0, 0, 0], hfb.hybrid ? 30000 : 5000);
    } catch (e) {
        // Only silence is expected (the keyboard restarts into the new firmware), as in hhkb_flasher.py
        if (!e.message.startsWith('no answer')) throw e;
    }
    if (hfb.hybrid) {
        log('Waiting while the Bluetooth chip installs its part (15 s): keep the keyboard plugged in.');
        await sleep(15000);
    }
    await pfu.close();
}

// ---------- ECK firmware over VIA raw HID ----------

// ECK on the HHKB (stm32/usb/usb_descriptors.c): 0x4010 ANSI, 0x4012 JIS; the product name must also contain "HHKB".
const ECK_PID = 0x4010, ECK_JIS_PID = 0x4012;
export const ECK_FILTER = { vendorId: 0xcafe, productId: ECK_PID, usagePage: 0xff60, usage: 0x61 };
export const ECK_JIS_FILTER = { ...ECK_FILTER, productId: ECK_JIS_PID };
// The Hybrid's Bluetooth VIA interface (nRF52 device name): it refuses updates, so never pick it.
export const isBluetooth = (d) => /^HHKB-Hybrid_\d/.test(d.productName || '');
export const isEck = (d) =>
    d.vendorId === 0xcafe && [ECK_PID, ECK_JIS_PID].includes(d.productId) && (d.productName || '').includes('HHKB') &&
    !isBluetooth(d) && d.collections.some((c) => c.usagePage === 0xff60);
// ECK on the Realforce R3S (stm32/boards/r3s): its own product ID
export const R3S_FILTER = { vendorId: 0xcafe, productId: 0x4011, usagePage: 0xff60, usage: 0x61 };
export const isR3s = (d) => d.vendorId === 0xcafe && d.productId === 0x4011 && d.collections.some((c) => c.usagePage === 0xff60);
const NRF_ID = 30, VERSION_ID = 28;
const UPD = { START: 1, DATA: 2, FINISH: 3, RESTART: 4, RECOVERY: 5 };
const ST = { ERASING: 1, RECEIVING: 2, STAGED: 3 };
const ST_ERRORS = { 0x81: 'image size refused', 0x82: 'data out of order', 0x83: 'MD5 mismatch after staging' };

export class Via {
    constructor(dev) {
        this.dev = dev;
        this.queue = [];
        dev.addEventListener('inputreport', (e) => this.queue.push(new Uint8Array(e.data.buffer)));
    }
    static async request() {
        const devs = await navigator.hid.requestDevice({ filters: [ECK_FILTER, ECK_JIS_FILTER] });
        const dev = devs.find(isEck);
        if (!dev && devs.some(isBluetooth)) throw new Error('that is the Bluetooth connection: choose the HHKB connected by USB');
        if (!dev) throw new Error('no HHKB running ECK selected');
        return Via.open(dev);
    }
    static async open(dev) {
        if (!isEck(dev) && !isR3s(dev)) throw new Error(`${dev.productName || 'this device'} is not a keyboard running ECK`);
        if (!dev.opened) await dev.open();
        return new Via(dev);
    }
    get hybrid() {
        return (this.dev.productName || '').includes('Hybrid');
    }
    get jis() {
        return this.dev.productId === ECK_JIS_PID;
    }
    /** Installed ECK version (VIA setting 28), or null from firmware too old to report it. */
    async version() {
        try {
            const r = await this.send([0x08, 0, VERSION_ID]);
            return new TextDecoder().decode(r.subarray(3)).split('\0')[0].trim() || null;
        } catch {
            return null;
        }
    }
    async send(bytes, timeoutMs = 1000) {
        const pkt = new Uint8Array(32);
        pkt.set(bytes);
        this.queue.length = 0;
        await this.dev.sendReport(0, pkt);
        const end = Date.now() + timeoutMs;
        while (Date.now() < end) {
            if (this.queue.length) {
                const r = this.queue.shift();
                if (r[0] === bytes[0]) return r;
            }
            await sleep(1);
        }
        throw new Error('no VIA answer from the keyboard');
    }
    nrfSet(payload) {
        return this.send([0x07, 0, NRF_ID, ...payload]);
    }
    async nrfStatus() {
        const r = await this.send([0x08, 0, NRF_ID]);
        return { state: r[3], staged: u32(r, 4), pending: r[8], failed: r[9], recovery: r[10] };
    }
    async bootloader() {
        const pkt = new Uint8Array(32);
        pkt[0] = 0x0b; // id_bootloader_jump: the keyboard leaves USB at once
        try {
            await this.dev.sendReport(0, pkt);
        } catch {}
    }
    async close() {
        try {
            await this.dev.close();
        } catch {}
    }
}

async function nrfSubmit(via, payload, what) {
    const end = Date.now() + 5000;
    while (Date.now() < end) {
        if ((await via.nrfSet(payload))[3] === 0) return;
        await sleep(1);
    }
    throw new Error(`${what}: the keyboard stayed busy`);
}

/** Stage the nRF52 part of a Hybrid .hfb through ECK; recovery: through the nRF52's recovery mode. */
export async function nrfUpdate(via, data, { recovery = false, log, progress }) {
    const { nrf, nrfMd5, hybrid } = parseHfb(data);
    if (!hybrid) throw new Error('not a Hybrid .hfb');
    try {
        if (recovery) {
            const r = await via.nrfSet([UPD.RECOVERY]).catch(() => null);
            if (!r) throw new Error('the keyboard firmware predates Bluetooth recovery mode: update it first');
            let up = false;
            for (let end = Date.now() + 10000; Date.now() < end && !up; ) {
                await sleep(200);
                up = (await via.nrfStatus()).recovery;
            }
            if (!up) throw new Error('the Bluetooth chip did not report recovery mode (its app may predate it)');
            log('Bluetooth chip in recovery mode');
        }
        await nrfSubmit(via, [UPD.START, ...le32(nrf.length), ...nrfMd5], 'start');
        log('Bluetooth stopped, erasing the staging area...');
        for (let end = Date.now() + 20000; ; ) {
            const s = await via.nrfStatus();
            if (s.state === ST.RECEIVING && !s.pending) break;
            if (ST_ERRORS[s.state] || s.failed || Date.now() > end) throw new Error(`the update did not start (state ${s.state})`);
            await sleep(100);
        }
        for (let off = 0; off < nrf.length; off += 22) {
            const chunk = nrf.subarray(off, off + 22);
            await nrfSubmit(via, [UPD.DATA, ...le32(off), chunk.length, ...chunk], `offset ${off}`);
            if (off % (22 * 500) === 0) {
                const s = await via.nrfStatus();
                if (s.state !== ST.RECEIVING || s.failed) throw new Error(`failed at ${off}: ${ST_ERRORS[s.state] || 'link error'}`);
                progress(s.staged / nrf.length);
            }
        }
        await nrfSubmit(via, [UPD.FINISH], 'finish');
        for (let end = Date.now() + 10000; ; ) {
            const s = await via.nrfStatus();
            if (s.state === ST.STAGED) break;
            if (ST_ERRORS[s.state] || s.failed || Date.now() > end)
                throw new Error(`staging failed: ${ST_ERRORS[s.state] || 'state ' + s.state}`);
            await sleep(100);
        }
        progress(1);
    } catch (e) {
        try {
            await via.nrfSet([UPD.RESTART]); // nothing staged: the current app starts again
        } catch {}
        throw e;
    }
    await via.nrfSet([UPD.RESTART]);
    log("Bluetooth chip restarted: PFU's bootloader installs the image (about 10 s).");
    await sleep(15000);
}

// ---------- ST's USB DFU loader (DfuSe), WebUSB ----------

const DFU_FILTER = { vendorId: 0x0483, productId: 0xdf11 };
const REQ = { DNLOAD: 1, UPLOAD: 2, GETSTATUS: 3, CLRSTATUS: 4, ABORT: 6 };
const STATE = { IDLE: 2, DNBUSY: 4, DNLOAD_IDLE: 5, MANIFEST: 7, UPLOAD_IDLE: 9, ERROR: 10 };
export const ALT = { FLASH: 0, EEPROM: 2 };

export class Dfu {
    constructor(dev) {
        this.dev = dev;
        this.alt = -1;
        this.xfer = 2048;
    }
    static async find() {
        return (await navigator.usb.getDevices()).find((d) => d.vendorId === 0x0483 && d.productId === 0xdf11);
    }
    static async request() {
        return navigator.usb.requestDevice({ filters: [DFU_FILTER] });
    }
    static async open(dev) {
        const d = new Dfu(dev);
        await dev.open();
        if (dev.configuration === null) await dev.selectConfiguration(1);
        await dev.claimInterface(0);
        d.xfer = await d.transferSize();
        return d;
    }
    /** wTransferSize from the DFU functional descriptor (type 0x21) in the configuration descriptor. */
    async transferSize() {
        const get = (len) =>
            this.dev.controlTransferIn({ requestType: 'standard', recipient: 'device', request: 6, value: 0x0200, index: 0 }, len);
        const head = new Uint8Array((await get(9)).data.buffer);
        const all = new Uint8Array((await get(head[2] | (head[3] << 8))).data.buffer);
        for (let i = 0; i + 1 < all.length && all[i]; i += all[i]) {
            if (all[i + 1] === 0x21 && all[i] >= 7) return all[i + 5] | (all[i + 6] << 8);
        }
        return 1024;
    }
    async select(alt) {
        if (alt === this.alt) return;
        await this.dev.selectAlternateInterface(0, alt);
        this.alt = alt;
    }
    async ctrlOut(request, value, data = new Uint8Array(0)) {
        const r = await this.dev.controlTransferOut({ requestType: 'class', recipient: 'interface', request, value, index: 0 }, data);
        if (r.status !== 'ok') throw new Error(`DFU request ${request} ${r.status}`);
    }
    async ctrlIn(request, value, length) {
        const r = await this.dev.controlTransferIn({ requestType: 'class', recipient: 'interface', request, value, index: 0 }, length);
        if (r.status !== 'ok') throw new Error(`DFU request ${request} ${r.status}`);
        return new Uint8Array(r.data.buffer);
    }
    async status() {
        const s = await this.ctrlIn(REQ.GETSTATUS, 0, 6);
        return { status: s[0], poll: s[1] | (s[2] << 8) | (s[3] << 16), state: s[4] };
    }
    /** Wait out a download; EEPROM writes can make a status request fail while still completing. */
    async settle() {
        for (let tries = 0; tries < 200; tries++) {
            let s;
            try {
                s = await this.status();
            } catch {
                await sleep(20);
                continue;
            }
            if (s.state === STATE.ERROR) {
                await this.ctrlOut(REQ.CLRSTATUS, 0);
                throw new Error(`DFU error status ${s.status}`);
            }
            if (s.state !== STATE.DNBUSY) return s;
            await sleep(Math.max(s.poll, 5));
        }
        throw new Error('DFU loader stayed busy');
    }
    async idle() {
        const s = await this.status().catch(() => null);
        if (s && s.state === STATE.ERROR) await this.ctrlOut(REQ.CLRSTATUS, 0);
        else if (s && s.state !== STATE.IDLE) await this.ctrlOut(REQ.ABORT, 0);
    }
    async command(bytes) {
        await this.ctrlOut(REQ.DNLOAD, 0, new Uint8Array(bytes));
        await this.settle();
    }
    async setAddress(addr) {
        await this.command([0x21, ...le32(addr)]);
    }
    /** As dfu-util: set the address once, then block n (from 2) reads address + (n - 2) * xfer. */
    async read(alt, addr, n, progress = () => {}) {
        await this.select(alt);
        await this.idle();
        await this.setAddress(addr);
        await this.ctrlOut(REQ.ABORT, 0); // back to dfuIDLE: uploads cannot start from dfuDNLOAD-IDLE
        const out = new Uint8Array(n);
        for (let off = 0, block = 2; off < n; off += this.xfer, block++) {
            const len = Math.min(this.xfer, n - off);
            const part = await this.ctrlIn(REQ.UPLOAD, block, len);
            if (part.length !== len) throw new Error(`short DFU read at ${hex(addr + off)}`);
            out.set(part, off);
            progress((off + len) / n);
        }
        await this.ctrlOut(REQ.ABORT, 0);
        return out;
    }
    /** Write data at addr; flash pages (128 B) are erased first, the data EEPROM needs no erase. */
    /** progress(f): erasing is the first third of the work for flash. */
    async write(alt, addr, data, blockSize = this.xfer, progress = () => {}) {
        await this.select(alt);
        await this.idle();
        const flash = alt === ALT.FLASH;
        if (flash)
            for (let a = addr; a < addr + data.length; a += 128) {
                await this.command([0x41, ...le32(a)]);
                progress(((a - addr + 128) / data.length) / 3);
            }
        for (let off = 0; off < data.length; off += blockSize) {
            await this.setAddress(addr + off);
            await this.ctrlOut(REQ.DNLOAD, 2, data.subarray(off, off + blockSize));
            await this.settle();
            const f = Math.min(off + blockSize, data.length) / data.length;
            progress(flash ? 1 / 3 + (2 / 3) * f : f);
        }
        await this.ctrlOut(REQ.ABORT, 0);
    }
    /** progress(f): writing is 80% of the work, the verify read the rest. */
    async writeVerified(alt, addr, data, what, log, progress = () => {}) {
        await this.write(alt, addr, data, alt === ALT.EEPROM ? 128 : this.xfer, (f) => progress(0.8 * f));
        if (!equal(await this.read(alt, addr, data.length, (f) => progress(0.8 + 0.2 * f)), data)) throw new Error(`${what}: verify failed (still in DFU mode: try again)`);
        log(`${what}: written and verified`);
    }
    /** Leave the loader: it jumps to Bank 1, whose boot stub then starts the firmware. */
    async leave() {
        await this.select(ALT.FLASH);
        await this.idle();
        await this.setAddress(BANK1);
        try {
            await this.ctrlOut(REQ.DNLOAD, 0);
            await this.status();
        } catch {} // the device is gone
        try {
            await this.dev.close();
        } catch {}
    }
}

/** Put the keyboard into ST's DFU loader (from ECK over VIA) and open it. needGesture(): resolves on a click. */
export async function enterDfu({ via, log, needGesture }) {
    let dev = await Dfu.find();
    if (!dev && via) {
        log('Starting update mode...');
        await via.bootloader();
        await via.close();
        // A device granted before shows up within ~2 s; otherwise ask while the click still counts
        for (let i = 0; i < 15 && !dev; i++) {
            await sleep(200);
            dev = await Dfu.find();
        }
    }
    if (!dev) {
        log('Select "DFU in FS Mode" (STMicroelectronics) in the dialog; it may take a moment to appear there.');
        try {
            dev = await Dfu.request(); // Chrome's picker lists the device once it enumerates
        } catch (e) {
            if (e.name !== 'SecurityError') throw e;
            await needGesture('Select DFU device'); // the click's permission expired
            dev = await Dfu.request();
        }
    }
    return Dfu.open(dev);
}

async function writeBank2(dfu, sec, log, progress = () => {}) {
    await dfu.writeVerified(ALT.EEPROM, TAG_CRC, new Uint8Array(4), 'boot tag cleared', log); // a half-written Bank 2 never boots
    await dfu.writeVerified(ALT.FLASH, BANK2, sec, 'firmware (Bank 2)', log, progress);
    await dfu.writeVerified(ALT.EEPROM, TAG_VALID, le32(1), 'boot flag', log);
    await dfu.writeVerified(ALT.EEPROM, TAG_CRC, le32(crc16mcrf4xx(sec)), 'boot tag', log);
}

/** Layout of the ECK Hybrid in Bank 2 from its product string: true JIS, false US, null none found. */
export function bank2Layout(bank2) {
    const s = new TextDecoder('latin1').decode(bank2);
    if (s.includes('HHKB Professional Hybrid JIS')) return true;
    return s.includes('HHKB Professional Hybrid') ? false : null;
}

/** Layout from the data EEPROM: ECK's Vial record ('CMIT', 'VIAL') sits at 0x1608 (US) or 0x1644 (JIS),
 * after a settings record sized for the layout (board_config.h). null when neither or both. */
export function eepromLayout(eeprom) {
    const rec = (o) => eeprom.length >= o + 8 && u32(eeprom, o) === 0x54494d43 && u32(eeprom, o + 4) === 0x4c414956;
    const us = rec(0x1608), jis = rec(0x1644);
    return us === jis ? null : jis;
}

/** The installed layout, in ST's DFU loader: Bank 2's ECK image, else the EEPROM; null if neither tells. */
export async function detectLayout(dfu) {
    const fromBank2 = bank2Layout(await dfu.read(ALT.FLASH, BANK2, SECTION));
    return fromBank2 !== null ? fromBank2 : eepromLayout(await dfu.read(ALT.EEPROM, EEPROM, EEPROM_SIZE));
}

/** Checks a boot stub image before anything is written. */
export function checkBootStub(stub) {
    if (stub.length > STUB_MAX) throw new Error('boot stub larger than 3 KB');
    checkVectors(stub, BANK1, 'boot stub');
}

/** STM32 update from a Hybrid .hfb. Without ECK's boot stub in Bank 1 (no USB recovery) it refuses,
 * unless a stub is given: then the stub goes in first (see writeBootStub). */
export async function stm32Update(dfu, data, { log, progress, stub = null, save, confirm, layoutChange = false }) {
    const { stm32, hybrid, model } = parseHfb(data);
    if (!hybrid) throw new Error('this updater handles the Hybrid; install Classic firmware through PFU’s updater');
    const installed = await detectLayout(dfu);
    const jis = model === 'AHJX01';
    if (installed !== null && installed !== jis && !layoutChange)
        throw new Error(`the keyboard is set up for the ${installed ? 'JIS' : 'US'} layout, the file is for ${jis ? 'JIS' : 'US'}`);
    const bank1 = await dfu.read(ALT.FLASH, BANK1, SECTION);
    if (!hasBootStub(bank1)) {
        if (!stub) throw new Error("ECK's boot stub is not installed: finish setup first");
        await writeBootStub(dfu, stub, bank1, { log, save, confirm });
    }
    // An ECK stub without Bluetooth installs (no STAGE_MAGIC) gets the bundled one, its 4 KB page only; never the reverse.
    if (stub && hasBootStub(bank1) && !stubInstalls(bank1) && stubInstalls(stub)) {
        checkBootStub(stub);
        const page = new Uint8Array(0x1000).fill(0xff);
        page.set(stub);
        await dfu.writeVerified(ALT.FLASH, BANK1, page, 'boot stub updated', log);
    }
    // A Bluetooth install marker the boot stub didn't act on (recovery): this USB write must win.
    if (bank1.subarray(STAGE_HDR - BANK1, STAGE_HDR - BANK1 + 16).some((b) => b !== 0))
        await dfu.writeVerified(ALT.FLASH, STAGE_HDR, new Uint8Array(128), 'Bluetooth install marker cleared', log);
    await writeBank2(dfu, stm32, log, progress);
    await dfu.leave();
}

/** Replace PFU's Bank 1 updater with the boot stub, after saving the original Bank 1 and EEPROM.
 * save(name, bytes) resolves once the file is written; confirm() resolves when the user agrees. */
async function writeBootStub(dfu, stub, bank1, { log, save, confirm }) {
    checkBootStub(stub);
    if (isTopreBank1(bank1)) throw new Error('this is a Realforce R3S, not an HHKB: use the Realforce R3S tab');
    if (!hasBootStub(bank1)) {
        const eeprom = await dfu.read(ALT.EEPROM, EEPROM, EEPROM_SIZE);
        log("Saving PFU's updater and your settings as two files: keep both, going back to stock needs them.");
        await save('hybrid-bank1-backup.bin', bank1);
        await save('hybrid-eeprom-backup.bin', eeprom);
        await confirm();
    }
    const image = new Uint8Array(SECTION).fill(0xff);
    image.set(stub);
    await dfu.writeVerified(ALT.FLASH, BANK1, image, 'boot stub', log);
}

export async function installBootStub(dfu, stub, opts) {
    await writeBootStub(dfu, stub, await dfu.read(ALT.FLASH, BANK1, SECTION), opts);
    await dfu.leave();
}

/** The data EEPROM: keymap, macros, calibration and settings. */
export async function backup(dfu) {
    return dfu.read(ALT.EEPROM, EEPROM, EEPROM_SIZE);
}

/** Restore a data EEPROM backup, keeping the chip's boot tag (it belongs to the installed Bank 2). */
export async function restoreEeprom(dfu, data, { log, r3s = false }) {
    if (data.length !== EEPROM_SIZE) throw new Error(`expected a ${EEPROM_SIZE}-byte data EEPROM backup`);
    if (isTopreBank1(await dfu.read(ALT.FLASH, BANK1, SECTION)) !== r3s)
        throw new Error(`this keyboard is ${r3s ? 'not a Realforce R3S' : 'a Realforce R3S'}: use the other tab`);
    const cur = await dfu.read(ALT.EEPROM, EEPROM, EEPROM_SIZE);
    const want = new Uint8Array(data);
    for (const tag of r3s ? [R3S_TAG, R3S_TAG + 4] : [TAG_VALID, TAG_CRC])
        want.set(cur.subarray(tag - EEPROM, tag - EEPROM + 4), tag - EEPROM);
    let blocks = 0;
    for (let off = 0; off < EEPROM_SIZE; off += 128) {
        const w = want.subarray(off, off + 128);
        if (equal(cur.subarray(off, off + 128), w)) continue;
        await dfu.writeVerified(ALT.EEPROM, EEPROM + off, w, `EEPROM block ${off.toString(16)}`, () => {});
        blocks++;
    }
    log(`Data EEPROM restored (${blocks} blocks written, boot tag kept)`);
    await dfu.leave();
}

/** R3S: write an .rfb's image to Bank 2 under Topre's tag, as tools/r3s/r3s_backup.py restore does;
 * Topre's Bank 1 starts it. Also takes Topre's own .rfb (back to stock). */
export async function r3sUpdate(dfu, data, { log, progress }) {
    const { model, stm32 } = parseHfb(data);
    if (model !== 'R3SD####') throw new Error(`the file is for ${model}, not the Realforce R3S`);
    if (!isTopreBank1(await dfu.read(ALT.FLASH, BANK1, SECTION)))
        throw new Error("this keyboard has no Topre updater, so it is not a Realforce R3S: use the HHKB tab");
    const word = await dfu.read(ALT.EEPROM, R3S_TAG, 4);
    const tag = (crc) => Uint8Array.of(word[0], word[1], crc & 0xff, crc >>> 8);
    await dfu.writeVerified(ALT.EEPROM, R3S_TAG, tag(0), 'boot tag cleared', log); // a half-written Bank 2 never boots
    await dfu.writeVerified(ALT.FLASH, BANK2, stm32, 'firmware (Bank 2)', log, progress);
    await dfu.writeVerified(ALT.EEPROM, R3S_TAG, tag(crc16mcrf4xx(stm32)), 'boot tag', log);
    await dfu.leave();
}

// ---------- Hybrid updates over Bluetooth (nrf52/main.c upd_cmd; tools/hybrid_update.py --bluetooth) ----------

// The 244-byte update service (hosts paired since v0.9.0), else the 32-byte VIA service; both carry protocol 0xE6.
export const BLE_UPD_FILTERS = [
    { vendorId: 0xcafe, usagePage: 0xff60, usage: 0x62 },
    { vendorId: 0xcafe, usagePage: 0xff60, usage: 0x61 },
];
const BU = { CMD: 0xe6, STATUS: 1, BEGIN: 2, CRC: 3, DATA: 4, COMMIT: 5, FINISH: 6, INSTALL: 7, ABORT: 8, COPY: 9 };
const BS = { READY: 2, STAGED: 5, ERROR: 6, STM_DONE: 8 };
const BU_ERRORS = { 1: 'image size refused', 2: 'erase failed', 3: 'write failed', 4: 'MD5 mismatch', 5: 'out of order',
    6: 'busy', 7: 'the keyboard rejected the image',
    8: "the boot stub is too old to install it: update the keyboard firmware over USB once (it updates the boot stub)",
    9: 'not an ECK Bluetooth image', 10: 'the file is for another model or layout',
    11: 'not allowed yet: press Fn + Ctrl + U on the keyboard (allows updates for 5 minutes), then click again' };
const BU_PAGE = 0x1000, STM_STAGE_MAX = 0xef80;

export function crc32(data) {
    let c = ~0;
    for (const b of data) {
        c ^= b;
        for (let k = 0; k < 8; k++) c = c & 1 ? (c >>> 1) ^ 0xedb88320 : c >>> 1;
    }
    return ~c >>> 0;
}

export class BleUpdate {
    constructor(dev) {
        this.dev = dev;
        this.size = dev.collections.some((c) => c.usagePage === 0xff60 && c.usage === 0x62) ? 244 : 32;
        this.jis = dev.productId === ECK_JIS_PID;
        this.queue = [];
        dev.addEventListener('inputreport', (e) => this.queue.push(new Uint8Array(e.data.buffer)));
    }
    static async request() {
        const devs = (await navigator.hid.requestDevice({ filters: BLE_UPD_FILTERS })).filter(isBluetooth);
        if (!devs.length) throw new Error("pick the HHKB's Bluetooth connection (HHKB-Hybrid_n)");
        const has = (d, u) => d.collections.some((c) => c.usagePage === 0xff60 && c.usage === u);
        // The 244-byte update service first; a host that can't write its reports (ATT MTU) gets VIA's.
        for (const dev of [...devs.filter((d) => has(d, 0x62)), ...devs.filter((d) => !has(d, 0x62))]) {
            if (!dev.opened) await dev.open();
            const ble = new BleUpdate(dev);
            if (await ble.ask(BU.STATUS, [], 2000).then(() => true, () => false)) return ble;
            await ble.close();
        }
        throw new Error('the keyboard did not answer over Bluetooth (needs ECK v0.9.0 or later)');
    }
    /** The running STM32's Bank 2 CRC, as it told the nRF52 at start-up, or null. */
    async stm32Crc() {
        const r = await this.ask(BU.STATUS);
        return r.length >= 15 && r[12] ? r[13] | (r[14] << 8) : null;
    }
    async close() {
        if (this.dev.opened) await this.dev.close();
    }
    async send(op, payload = []) {
        const r = new Uint8Array(this.size);
        r.set([BU.CMD, op, ...payload]);
        await this.dev.sendReport(0, r);
    }
    async ask(op, payload = [], timeout = 3000) {
        this.queue = [];
        await this.send(op, payload);
        for (const end = Date.now() + timeout; Date.now() < end; ) {
            const r = this.queue.find((q) => q[0] === BU.CMD && q[1] === op);
            if (r) return r;
            await sleep(10);
        }
        throw new Error(`no reply over Bluetooth (update command ${op})`);
    }
    async wait(want, { done = null, timeout = 60000, poll = 20 } = {}) {
        for (const end = Date.now() + timeout; ; ) {
            const r = await this.ask(BU.STATUS);
            const state = r[2], err = r[3], d = r[4] | (r[5] << 8);
            if (state === BS.ERROR) throw new Error(`update failed: ${BU_ERRORS[err] || err} (page ${d})`);
            if (state === want && (done === null || d >= done)) return;
            if (Date.now() > end) throw new Error(`update timed out (state ${state}, page ${d})`);
            await sleep(poll);
        }
    }
    /** Sends pages in order; unchanged ones (same: a Set) as COPY runs from the live image. */
    async sendPages(image, same, progress) {
        const pages = Math.ceil(image.length / BU_PAGE), step = this.size - 6;
        for (let p = 0; p < pages; ) {
            let n = 1;
            if (same.has(p)) {
                while (same.has(p + n)) n++;
                await this.send(BU.COPY, [p & 0xff, p >> 8, n & 0xff, n >> 8]);
                await this.wait(BS.READY, { done: p + n, timeout: 10000 + 1000 * n, poll: 100 });
            } else {
                const end = Math.min((p + 1) * BU_PAGE, image.length);
                for (let off = p * BU_PAGE; off < end; off += step) {
                    const chunk = image.subarray(off, Math.min(off + step, end));
                    await this.send(BU.DATA, [off & 0xff, (off >> 8) & 0xff, off >> 16, chunk.length, ...chunk]);
                }
                await this.send(BU.COMMIT, [p & 0xff, p >> 8, 0]);
                await this.wait(BS.READY, { done: p + 1, timeout: 10000, poll: 50 });
            }
            p += n;
            progress(p / pages);
        }
    }
}

/** Keyboard (STM32) firmware over Bluetooth: staged on the nRF52, pulled and checked by the STM32, which
 * restarts; its boot stub installs it (solid orange LED). Bluetooth stays connected. */
export async function stm32UpdateBle(ble, data, { log, progress }) {
    const { stm32, model } = parseHfb(data);
    if ((model === 'AHJX01') !== ble.jis) throw new Error(`the keyboard is ${ble.jis ? 'JIS' : 'US'}, the file is not`);
    let end = stm32.length;
    while (end > 0 && stm32[end - 1] === 0xff) end--;
    const image = new Uint8Array((end + 3) & ~3).fill(0xff);
    image.set(stm32.subarray(0, end));
    if (image.length > STM_STAGE_MAX) throw new Error(`the keyboard firmware is ${image.length} bytes; Bluetooth updates hold ${STM_STAGE_MAX}`);
    const crc = crc16mcrf4xx(stm32);
    try {
        log(`Keyboard firmware: ${image.length} bytes over Bluetooth (${ble.size}-byte reports); typing pauses.`);
        await ble.ask(BU.BEGIN, [...le32(image.length), ...md5(image), 1, crc & 0xff, crc >> 8]);
        await ble.wait(BS.READY, { timeout: 120000, poll: 300 });
        await ble.sendPages(image, new Set(), progress);
        log('Sent; the keyboard checks it...');
        await ble.ask(BU.FINISH);
        await ble.wait(BS.STM_DONE, { timeout: 60000, poll: 200 });
        await ble.send(BU.INSTALL).catch(() => {}); // it may restart before acknowledging
        log('Checked. The keyboard restarts and installs it (~6 s, orange LED on)...');
        await sleep(5000);
        let got = null;
        for (const end = Date.now() + 40000; Date.now() < end && got !== crc; ) {
            got = await ble.stm32Crc().catch(() => null);
            if (got !== crc) await sleep(1000);
        }
        if (got !== crc) throw new Error('the keyboard does not run the new firmware: the install did not happen (USB still updates it)');
        log('Installed: the keyboard runs the new firmware.');
    } catch (e) {
        try {
            await ble.send(BU.ABORT);
        } catch {}
        throw e;
    }
}

/** Bluetooth (nRF52) firmware over Bluetooth: only changed 4 KB pages are sent; false when nothing changed.
 * PFU's bootloader installs it (orange LED blinks); Bluetooth drops for ~10 s. */
export async function nrfUpdateBle(ble, data, { log, progress }) {
    const { nrf, nrfMd5, model } = parseHfb(data);
    if ((model === 'AHJX01') !== ble.jis) throw new Error(`the keyboard is ${ble.jis ? 'JIS' : 'US'}, the file is not`);
    const pages = Math.ceil(nrf.length / BU_PAGE);
    const st = await ble.ask(BU.STATUS);
    if (st[6] !== 1) throw new Error(`the keyboard's update protocol is ${st[6]}; this page speaks 1`);
    const same = new Set();
    for (let first = 0; first < pages; first += 6) {
        const n = Math.min(6, pages - first);
        const r = await ble.ask(BU.CRC, [first, n]);
        for (let i = 0; i < n; i++) {
            const p = first + i, live = (r[8 + 4 * i] | (r[9 + 4 * i] << 8) | (r[10 + 4 * i] << 16) | (r[11 + 4 * i] << 24)) >>> 0;
            if ((p + 1) * BU_PAGE <= nrf.length && live === crc32(nrf.subarray(p * BU_PAGE, (p + 1) * BU_PAGE))) same.add(p);
        }
    }
    if (same.size === pages) {
        log('Bluetooth firmware: already up to date.');
        return false;
    }
    try {
        log(`Bluetooth firmware: ${pages - same.size} of ${pages} pages changed; typing pauses.`);
        await ble.ask(BU.BEGIN, [...le32(nrf.length), ...nrfMd5, 0, 0, 0]);
        await ble.wait(BS.READY, { timeout: 120000, poll: 300 });
        await ble.sendPages(nrf, same, progress);
        await ble.ask(BU.FINISH);
        await ble.wait(BS.STAGED, { timeout: 10000 });
        await ble.send(BU.INSTALL).catch(() => {}); // it restarts within ~1.5 s, often before acknowledging
    } catch (e) {
        try {
            await ble.send(BU.ABORT);
        } catch {}
        throw e;
    }
    log('Checked. The Bluetooth chip restarts and installs it (orange LED blinks); Bluetooth is back in ~10 s.');
    return true;
}
