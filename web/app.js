// ECK Flasher UI: wires the buttons in index.html to the flows in eck.js.
import * as eck from './eck.js';

const $ = (id) => document.getElementById(id);
const logEl = $('log'), stateEl = $('state'), bar = $('progress'), gate = $('gate');
const buttons = ['b-install', 'b-stub', 'b-stm32', 'b-nrf', 'b-backup'].map($);
let manifest = null;

function log(msg) {
    logEl.textContent += msg + '\n';
    logEl.scrollTop = logEl.scrollHeight;
}
const progress = (f) => (bar.value = f);

/** Resolves on a click of the Continue button: WebHID/WebUSB device dialogs need a user gesture. */
function needGesture(label = 'Continue') {
    gate.textContent = label;
    gate.classList.add('on');
    return new Promise((resolve) => {
        gate.onclick = () => {
            gate.classList.remove('on');
            resolve();
        };
    });
}

function download(name, bytes) {
    const a = document.createElement('a');
    a.href = URL.createObjectURL(new Blob([bytes]));
    a.download = name;
    a.click();
    setTimeout(() => URL.revokeObjectURL(a.href), 10000);
}

async function save(name, bytes) {
    await needGesture(`Save ${name}`);
    download(name, bytes);
    log(`Saved ${name} to your downloads.`);
}

async function run(title, fn) {
    buttons.forEach((b) => (b.disabled = true));
    stateEl.textContent = title;
    stateEl.className = '';
    progress(0);
    log(`\n== ${title}`);
    try {
        await fn();
        stateEl.textContent = `${title}: done`;
        stateEl.className = 'ok';
        log('Done.');
    } catch (e) {
        stateEl.textContent = `${title}: failed`;
        stateEl.className = 'warn';
        log(`Error: ${e.message}`);
    } finally {
        gate.classList.remove('on');
        buttons.forEach((b) => (b.disabled = false));
    }
}

async function fetchBytes(path) {
    const r = await fetch(path);
    if (!r.ok) throw new Error(`${path}: ${r.status}`);
    return new Uint8Array(await r.arrayBuffer());
}

/** The selected .hfb: a local file wins over the bundled release. */
async function firmware() {
    const f = $('hfbfile').files[0];
    if (f) return new Uint8Array(await f.arrayBuffer());
    if (!manifest) throw new Error('no bundled firmware: choose a local .hfb');
    return fetchBytes(`firmware/${manifest.files[$('variant').value]}`);
}

/** Try a device dialog now; if the click's user gesture has expired, ask for another click. */
async function withGesture(label, open) {
    try {
        return await open();
    } catch (e) {
        if (e.name !== 'SecurityError') throw e;
        await needGesture(label);
        return open();
    }
}

async function openVia() {
    const granted = (await navigator.hid.getDevices()).find(
        (d) => d.collections.some((c) => c.usagePage === 0xff60) && (d.productName || '').includes('HHKB')
    );
    if (granted) {
        if (!granted.opened) await granted.open();
        return new eck.Via(granted);
    }
    log('Select the keyboard (HHKB ...) in the next dialog.');
    return withGesture('Select keyboard', () => eck.Via.request());
}

/** After PFU's updater re-enumerates: reuse a granted device, else ask for it again. */
async function pfuReconnect() {
    for (let end = Date.now() + 20000; Date.now() < end; ) {
        const dev = (await navigator.hid.getDevices()).find(
            (d) => d.vendorId === 0x04fe && d.collections.some((c) => c.usagePage === 0xff00)
        );
        if (dev) return dev;
        await new Promise((r) => setTimeout(r, 300));
    }
    log('Select the HHKB updater in the next dialog.');
    await needGesture('Select updater');
    const [dev] = await navigator.hid.requestDevice({ filters: [{ vendorId: 0x04fe, usagePage: 0xff00 }] });
    if (!dev) throw new Error('no updater selected');
    return dev;
}

$('b-install').onclick = () =>
    run('First install', async () => {
        const data = await firmware();
        log('Select the keyboard (HHKB, PFU) in the next dialog.');
        const pfu = await withGesture('Select keyboard', () => eck.Pfu.request());
        await eck.pfuInstall(pfu, data, { reconnect: pfuReconnect, log, progress });
    });

$('b-stub').onclick = () =>
    run('Boot stub', async () => {
        if (!manifest?.files.boot) throw new Error('no bundled boot stub');
        const stub = await fetchBytes(`firmware/${manifest.files.boot}`);
        const dfu = await eck.enterDfu({ via: await eck.Dfu.find() ? null : await openVia(), log, needGesture });
        await eck.installBootStub(dfu, stub, { log, save });
    });

$('b-stm32').onclick = () =>
    run('Keyboard firmware update', async () => {
        const data = await firmware();
        eck.parseHfb(data);
        const dfu = await eck.enterDfu({ via: await eck.Dfu.find() ? null : await openVia(), log, needGesture });
        await eck.stm32Update(dfu, data, { log });
    });

$('b-nrf').onclick = () =>
    run('Bluetooth firmware update', async () => {
        const data = await firmware();
        const via = await openVia();
        try {
            await eck.nrfUpdate(via, data, { recovery: $('recovery').checked, log, progress });
        } finally {
            await via.close();
        }
    });

$('b-backup').onclick = () =>
    run('Backup', async () => {
        const dfu = await eck.enterDfu({ via: await eck.Dfu.find() ? null : await openVia(), log, needGesture });
        const { eeprom, bank2 } = await eck.backup(dfu);
        await dfu.leave();
        const stamp = new Date().toISOString().slice(0, 10);
        await save(`eck-eeprom-${stamp}.bin`, eeprom);
        await save(`eck-bank2-${stamp}.bin`, bank2);
    });

$('eepromfile').onchange = () =>
    run('Restore EEPROM', async () => {
        const data = new Uint8Array(await $('eepromfile').files[0].arrayBuffer());
        $('eepromfile').value = '';
        const dfu = await eck.enterDfu({ via: await eck.Dfu.find() ? null : await openVia(), log, needGesture });
        await eck.restoreEeprom(dfu, data, { log });
    });

if (!('hid' in navigator) || !('usb' in navigator)) {
    $('support').textContent = 'This browser lacks WebHID/WebUSB: use Chrome or Edge on a desktop.';
    buttons.forEach((b) => (b.disabled = true));
}

fetch('firmware/manifest.json')
    .then((r) => (r.ok ? r.json() : Promise.reject()))
    .then((m) => {
        manifest = m;
        $('fwinfo').textContent = `Bundled release ${m.version}. Pick your keyboard, or choose a local .hfb.`;
    })
    .catch(() => ($('fwinfo').textContent = 'No bundled release here: choose a local .hfb.'));
