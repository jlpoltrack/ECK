// Runs the web flasher's R3S flows (web/eck.js) from Node against a keyboard on the bench, through
// WebHID and WebUSB stand-ins. Setup: npm install (here). Usage: node tools/web_bench/bench.mjs CMD ...
//   install FILE.rfb   from stock firmware, through Topre's updater
//   update FILE.rfb    from ECK (VIA jump to ST's DFU loader) or from the loader
//   backup OUT.bin     data EEPROM through the loader;  restore IN.bin: write it back, boot tag kept
//   guards HYBRID.hfb  the HHKB flows must refuse the R3S in the loader before writing anything
import { readFileSync, writeFileSync } from 'node:fs';
import { createRequire } from 'node:module';
import * as eck from '../../web/eck.js';

const require = createRequire(import.meta.url);
const HID = require('node-hid');
const { WebUSB } = require('usb');
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const log = (m) => console.log(m);
let tenth = 0;
const progress = (f) => {
    const t = Math.floor(f * 10 + 1e-9);
    if (t > tenth) console.log(`${(tenth = t) * 10}%`);
};

/** A node-hid interface with the parts of WebHID's HIDDevice that eck.js uses. */
class HidShim extends EventTarget {
    constructor(info) {
        super();
        Object.assign(this, { info, vendorId: info.vendorId, productId: info.productId, productName: info.product });
        this.collections = [{ usagePage: info.usagePage, usage: info.usage }];
        this.h = null;
    }
    get opened() {
        return !!this.h;
    }
    async open() {
        this.h = new HID.HID(this.info.path);
        this.h.on('data', (buf) => {
            const e = new Event('inputreport');
            e.data = new DataView(Uint8Array.from(buf).buffer);
            this.dispatchEvent(e);
        });
        this.h.on('error', () => {});
    }
    async sendReport(id, data) {
        this.h.write([id, ...data]);
    }
    async close() {
        if (this.h) this.h.close();
        this.h = null;
    }
}

const hidDevices = async () => HID.devices().map((d) => new HidShim(d));
Object.defineProperty(globalThis, 'navigator', {
    value: { hid: { getDevices: hidDevices }, usb: new WebUSB({ allowAllDevices: true }) },
    configurable: true,
});

async function waitFor(what, find, ms) {
    for (const end = Date.now() + ms; Date.now() < end; await sleep(200)) {
        const d = await find();
        if (d) return d;
    }
    throw new Error(`${what} did not appear`);
}
const topre = async () => (await hidDevices()).find((d) => eck.isUpdater(d, eck.TOPRE_FILTER));
const r3sEck = async () => (await hidDevices()).find(eck.isR3s);

async function dfu() {
    const dev = await eck.Dfu.find();
    if (dev) return eck.Dfu.open(dev);
    const via = await eck.Via.open(await waitFor('ECK on the R3S', r3sEck, 3000));
    log(`ECK ${await via.version()}`);
    return eck.enterDfu({ via, log, needGesture: () => Promise.reject(new Error('no DFU device')) });
}

async function back() {
    const via = await eck.Via.open(await waitFor('ECK on the R3S', r3sEck, 20000));
    log(`back: ECK ${await via.version()}`);
    await via.close();
}

process.on('unhandledRejection', (e) => {
    console.error(`Error: ${e.message}`);
    process.exit(1); // open HID handles would keep Node running
});
const [cmd, file] = process.argv.slice(2);
if (cmd === 'install') {
    const pfu = await eck.Pfu.open(await waitFor("Topre's updater", topre, 3000));
    log(`stock: ${JSON.stringify(await pfu.info())}`);
    await eck.pfuInstall(pfu, new Uint8Array(readFileSync(file)), {
        reconnect: () => waitFor("Topre's updater (again)", topre, 15000), log, progress,
    });
    await back();
} else if (cmd === 'update') {
    await eck.r3sUpdate(await dfu(), new Uint8Array(readFileSync(file)), { log, progress });
    log('left the DFU loader');
} else if (cmd === 'backup') {
    const d = await dfu();
    writeFileSync(file, await eck.backup(d));
    await d.leave();
    log(`saved ${file}`);
    await back();
} else if (cmd === 'restore') {
    await eck.restoreEeprom(await dfu(), new Uint8Array(readFileSync(file)), { log, r3s: true });
    await back();
} else if (cmd === 'guards') {
    const d = await dfu();
    const refuses = async (what, p) => {
        const e = await p.then(() => null, (e) => e);
        if (!e) throw new Error(`${what} went ahead on the R3S`);
        log(`${what} refused: ${e.message}`);
    };
    const opts = { log, progress, stub: new Uint8Array(readFileSync(`${import.meta.dirname}/../../stm32/boot/_build/hybrid_boot.bin`)) };
    await refuses('HHKB update', eck.stm32Update(d, new Uint8Array(readFileSync(file)), opts));
    await refuses('HHKB settings restore', eck.restoreEeprom(d, await eck.backup(d), { log }));
    await d.leave();
    await back();
} else {
    console.log('usage: node tools/web_bench/bench.mjs install|update|backup|restore|guards FILE');
    process.exit(2);
}
process.exit(0);
