// ECK web flasher: protocols and update flows, no UI. Ports of tools/hhkb_flasher.py (PFU updater)
// and tools/hybrid_update.py (ST ROM DFU, nRF52 over VIA). Runs in Chrome/Edge (WebHID, WebUSB).

// ---------- containers ----------

export const CLASSIC_HFB_SIZE = 65570;
export const HYBRID_HFB_SIZE = 282672;
export const SECTION = 0x10000;
const NRF_MD5 = 0x10010, NRF_IMAGE = 0x10020, NRF_SIZE = 0x35000, NRF_BASE = 0x1000, NRF_APP = 0x26000;
export const BANK1 = 0x08000000, BANK2 = 0x08010000, EEPROM = 0x08080000, EEPROM_SIZE = 0x1800;
const TAG_VALID = 0x08080360, TAG_CRC = 0x08080370;

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

/** Checks a .hfb and returns {model, version, hybrid, stm32 (64 KB section), nrf?, nrfMd5?}. */
export function parseHfb(data) {
    const hybrid = data.length === HYBRID_HFB_SIZE;
    if (!hybrid && data.length !== CLASSIC_HFB_SIZE) throw new Error(`not an HHKB .hfb (${data.length} bytes)`);
    if ((data[0] | (data[1] << 8)) !== crc16mcrf4xx(data.subarray(2))) throw new Error('outer CRC does not match');
    const stm32 = data.subarray(4, 4 + SECTION);
    if ((data[2] | (data[3] << 8)) !== crc16mcrf4xx(stm32)) throw new Error('STM32 CRC does not match');
    const trailer = data.subarray(data.length - 16);
    const model = new TextDecoder().decode(trailer.subarray(0, 6));
    const v = trailer.subarray(6, 10);
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

/** First 8 KB of Bank 1: the ECK boot stub's vectors point into its first 4 KB and the rest is erased. */
export function hasBootStub(head) {
    if (head.length < 0x2000) return false;
    const sp = u32(head, 0), reset = u32(head, 4);
    return sp >>> 24 === 0x20 && reset >= BANK1 && reset < BANK1 + 0x1000 && head.subarray(0x1000).every((b) => b === 0xff);
}

/** Keyboard type number (PFU info) to container model. */
export function deviceModel(type) {
    const n = type.toUpperCase().trim();
    if (['PD-KB401WSC', 'PD-KB401BSC', 'PD-KB401YSC'].includes(n)) return 'AHHS01';
    if (['PD-KB401W', 'PD-KB401B', 'PD-KB401WN', 'PD-KB401BN'].includes(n)) return 'AHHX01';
    if (n === 'PD-KB800BS') return 'AHUX01';
    return null;
}

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

// ---------- PFU updater (stock firmware or Bank 1), WebHID ----------

const PFU_FILTER = { vendorId: 0x04fe, usagePage: 0xff00, usage: 0x01 };

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
        const type = new TextDecoder().decode(r.subarray(6, 26)).split('\0')[0].trim();
        return { type, model: deviceModel(type), bank2Broken: r[62] === 1 };
    }
    async close() {
        try {
            await this.dev.close();
        } catch {}
    }
}

/** Install a .hfb through PFU's updater. reconnect(): returns the re-enumerated updater's HIDDevice. */
export async function pfuInstall(pfu, data, { reconnect, log, progress }) {
    const hfb = parseHfb(data);
    const info = await pfu.info();
    log(`Keyboard: ${info.type}`);
    if (!info.model) throw new Error(`unsupported keyboard model ${info.type}`);
    if (info.model !== hfb.model) throw new Error(`firmware is for ${hfb.model}, keyboard is ${info.model}`);
    await pfu.cmd([0xe0, 0, 0, 0], 3000);
    await pfu.close();
    log('Waiting for the PFU updater to reconnect (about 10 s)...');
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
        log(`Final step: ${e.message} (expected when the keyboard restarts into the new firmware)`);
    }
    if (hfb.hybrid) {
        log('Waiting while the Bluetooth chip installs its part (15 s): keep the keyboard plugged in.');
        await sleep(15000);
    }
    await pfu.close();
}

// ---------- ECK firmware over VIA raw HID ----------

const VIA_FILTER = { usagePage: 0xff60, usage: 0x61 };
const NRF_ID = 30;
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
        const devs = await navigator.hid.requestDevice({ filters: [VIA_FILTER] });
        const dev = devs.find((d) => (d.productName || '').includes('HHKB')) || devs[0];
        if (!dev) throw new Error('no keyboard selected');
        if (!dev.opened) await dev.open();
        return new Via(dev);
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
    async read(alt, addr, n) {
        await this.select(alt);
        await this.idle();
        const out = new Uint8Array(n);
        for (let off = 0; off < n; off += this.xfer) {
            await this.setAddress(addr + off); // block 2 reads from the address pointer itself
            await this.ctrlOut(REQ.ABORT, 0);
            const len = Math.min(this.xfer, n - off);
            const part = await this.ctrlIn(REQ.UPLOAD, 2, len);
            if (part.length !== len) throw new Error(`short DFU read at ${hex(addr + off)}`);
            out.set(part, off);
        }
        return out;
    }
    /** Write data at addr; flash pages (128 B) are erased first, the data EEPROM needs no erase. */
    async write(alt, addr, data, blockSize = this.xfer) {
        await this.select(alt);
        await this.idle();
        if (alt === ALT.FLASH) for (let a = addr; a < addr + data.length; a += 128) await this.command([0x41, ...le32(a)]);
        for (let off = 0; off < data.length; off += blockSize) {
            await this.setAddress(addr + off);
            await this.ctrlOut(REQ.DNLOAD, 2, data.subarray(off, off + blockSize));
            await this.settle();
        }
        await this.ctrlOut(REQ.ABORT, 0);
    }
    async writeVerified(alt, addr, data, what, log) {
        await this.write(alt, addr, data, alt === ALT.EEPROM ? 128 : this.xfer);
        if (!equal(await this.read(alt, addr, data.length), data)) throw new Error(`${what}: verify failed (still in DFU mode: try again)`);
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
        log('Starting the DFU loader...');
        await via.bootloader();
        await via.close();
        for (let i = 0; i < 50 && !dev; i++) {
            await sleep(200);
            dev = await Dfu.find();
        }
    }
    if (!dev) {
        log('Select "DFU in FS Mode" (STMicroelectronics) in the next dialog.');
        await needGesture('Select DFU device'); // the loader appears seconds after the click
        dev = await Dfu.request();
    }
    return Dfu.open(dev);
}

async function writeBank2(dfu, sec, log) {
    await dfu.writeVerified(ALT.EEPROM, TAG_CRC, new Uint8Array(4), 'boot tag cleared', log); // a half-written Bank 2 never boots
    await dfu.writeVerified(ALT.FLASH, BANK2, sec, 'firmware (Bank 2)', log);
    await dfu.writeVerified(ALT.EEPROM, TAG_VALID, le32(1), 'boot flag', log);
    await dfu.writeVerified(ALT.EEPROM, TAG_CRC, le32(crc16mcrf4xx(sec)), 'boot tag', log);
}

/** STM32 update from a Hybrid .hfb: refuses unless Bank 1 holds the boot stub (no USB recovery otherwise). */
export async function stm32Update(dfu, data, { log, force = false }) {
    const { stm32, hybrid } = parseHfb(data);
    if (!hybrid) throw new Error('this updater handles the Hybrid; install Classic firmware through PFU’s updater');
    if (!force && !hasBootStub(await dfu.read(ALT.FLASH, BANK1, 0x2000))) {
        throw new Error("Bank 1 does not hold ECK's boot stub: install it first (Boot stub step)");
    }
    await writeBank2(dfu, stm32, log);
    await dfu.leave();
}

/** Replace PFU's Bank 1 updater with the boot stub. save(name, bytes): offers a download; resolves once saved. */
export async function installBootStub(dfu, stub, { log, save }) {
    if (stub.length > 0x1000) throw new Error('boot stub larger than 4 KB');
    checkVectors(stub, BANK1, 'boot stub');
    const bank1 = await dfu.read(ALT.FLASH, BANK1, SECTION);
    if (!hasBootStub(bank1)) {
        const eeprom = await dfu.read(ALT.EEPROM, EEPROM, EEPROM_SIZE);
        log('Saving your original Bank 1 and data EEPROM: keep both files, going back to stock needs them.');
        await save('hybrid-bank1-backup.bin', bank1);
        await save('hybrid-eeprom-backup.bin', eeprom);
    }
    const image = new Uint8Array(SECTION).fill(0xff);
    image.set(stub);
    await dfu.writeVerified(ALT.FLASH, BANK1, image, 'boot stub (Bank 1)', log);
    await dfu.leave();
}

export async function backup(dfu) {
    const eeprom = await dfu.read(ALT.EEPROM, EEPROM, EEPROM_SIZE);
    const bank2 = await dfu.read(ALT.FLASH, BANK2, SECTION);
    return { eeprom, bank2 };
}

/** Restore a data EEPROM backup, keeping the chip's boot tag (it belongs to the installed Bank 2). */
export async function restoreEeprom(dfu, data, { log }) {
    if (data.length !== EEPROM_SIZE) throw new Error(`expected a ${EEPROM_SIZE}-byte data EEPROM backup`);
    const cur = await dfu.read(ALT.EEPROM, EEPROM, EEPROM_SIZE);
    const want = new Uint8Array(data);
    for (const tag of [TAG_VALID, TAG_CRC]) want.set(cur.subarray(tag - EEPROM, tag - EEPROM + 4), tag - EEPROM);
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
