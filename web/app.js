// ECK Flasher UI: connect, detect what the keyboard runs, then offer only the actions that apply.
import * as eck from './eck.js';

const $ = (id) => document.getElementById(id);
const logEl = $('log'), stateEl = $('state'), bar = $('progress'), gate = $('gate');
const controls = ['b-connect', 'b-install', 'b-stm32', 'b-nrf', 'b-backup', 'b-recover', 'b-nrf-recover', 'b-restore', 'eepromfile', 'hfbfile',
    'r-connect', 'r-install', 'r-update', 'r-backup', 'r-restore', 'r-eepromfile', 'r-recover-b'].map($);
const windows = navigator.userAgent.includes('Windows');
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
let manifest = null;
let kb = null; // the connected keyboard: {kind: 'stock' | 'eck' | 'silent' | 'unsupported', ...}
let inDfu = false; // the running action has put the keyboard in ST's update mode

const MODELS = {
    AHUX01: { file: 'hybrid', name: 'HHKB Professional Hybrid Type-S', hybrid: true },
    AHHX01: { file: 'classic', name: 'HHKB Professional Classic', hybrid: false },
    AHHS01: { file: 'type_s', name: 'HHKB Professional Classic Type-S', hybrid: false },
};

function log(msg) {
    logEl.textContent += msg + '\n';
    logEl.scrollTop = logEl.scrollHeight;
}
let tenth = 0; // last 10% step logged in this action
function progress(f) {
    bar.value = f;
    const t = Math.floor(f * 10 + 1e-9);
    if (t > tenth) log(`${(tenth = t) * 10}%`);
}
$('b-clear').onclick = () => (logEl.textContent = '');
const say = (msg) => {
    stateEl.textContent = msg;
    stateEl.className = '';
    log(msg);
};

/** Resolves on a click of the gate button: device dialogs and file pickers need a user gesture. */
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

/** Asks where to save (needs a user gesture) and returns a writer for that file. A cancel throws,
 * so callers ask before changing anything. */
async function pickFile(name) {
    if (!window.showSaveFilePicker) {
        return async (bytes) => {
            const a = document.createElement('a');
            a.href = URL.createObjectURL(new Blob([bytes]));
            a.download = name;
            a.click();
            log(`${name}: check that it is in your downloads.`);
        };
    }
    let handle;
    try {
        handle = await window.showSaveFilePicker({ suggestedName: name });
    } catch (e) {
        if (e.name === 'AbortError') throw new Error(`${name} was not saved: stopped before changing anything`);
        throw e;
    }
    return async (bytes) => {
        const w = await handle.createWritable();
        await w.write(bytes);
        await w.close();
        log(`Saved ${handle.name}.`);
    };
}

/** Saves bytes to a file the user picks after clicking the Save button. */
async function save(name, bytes) {
    say(`Save ${name}`);
    await needGesture(`Save ${name}`);
    await (await pickFile(name))(bytes);
}

let busy = false; // one action at a time: a stale gate click must not start a second one

async function run(title, fn) {
    if (busy) return;
    busy = true;
    gate.classList.remove('on');
    gate.onclick = null;
    controls.forEach((b) => (b.disabled = true));
    stateEl.textContent = title;
    stateEl.className = '';
    bar.value = 0;
    tenth = 0;
    inDfu = false;
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
        if (inDfu)
            log(`The keyboard is still in update mode and won't type: use ${tab === 'r3s' ? 'Recover firmware' : 'Recover keyboard firmware'} (below).`);
    } finally {
        gate.classList.remove('on');
        controls.forEach((b) => (b.disabled = false));
        busy = false;
    }
}

async function fetchBytes(path) {
    const r = await fetch(path);
    if (!r.ok) throw new Error(`${path}: ${r.status}`);
    return new Uint8Array(await r.arrayBuffer());
}

/** Firmware for a model: the local file (Advanced) if chosen, else the bundled release. Checked here. */
async function firmware(model) {
    const f = $('hfbfile').files[0];
    let data;
    if (f) data = new Uint8Array(await f.arrayBuffer());
    else if (manifest) data = await fetchBytes(`firmware/${manifest.files[MODELS[model].file]}`);
    else throw new Error('no bundled release here: choose a local .hfb under Advanced');
    const hfb = eck.parseHfb(data);
    if (hfb.model !== model) throw new Error(`the .hfb is for ${hfb.model}, the keyboard is ${model}`);
    return data;
}

async function bootStub() {
    if (!manifest?.files.boot) throw new Error('no bundled boot stub');
    const stub = await fetchBytes(`firmware/${manifest.files.boot}`);
    eck.checkBootStub(stub);
    return stub;
}

const confirmStub = () => needGesture("I have saved both files: replace PFU's updater");

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

/** The keyboard running ECK: the connected one, a granted one, or one picked in a dialog. */
async function openVia() {
    if (kb?.via) {
        const v = kb.via;
        kb.via = null;
        return v;
    }
    const granted = (await navigator.hid.getDevices()).find(eck.isEck);
    if (granted) return eck.Via.open(granted);
    say('Select the keyboard (HHKB) in the next dialog.');
    return withGesture('Select keyboard', () => eck.Via.request());
}

async function enterDfu(via) {
    if (windows) log('Windows: if the keyboard is not found in update mode, install the WinUSB driver with Zadig ("DFU in FS Mode") and try again.');
    const dfu = await eck.enterDfu({ via, log, needGesture });
    inDfu = true;
    return dfu;
}

/** After PFU's (Topre's) updater re-enumerates: a granted device, or one picked in a dialog after 10 s. */
async function pfuReconnect(filter = eck.PFU_FILTER, name = "PFU's updater (HHKB)") {
    const find = async () => (await navigator.hid.getDevices()).find((d) => eck.isUpdater(d, filter));
    for (const end = Date.now() + 10000; Date.now() < end; await sleep(300)) {
        const d = await find();
        if (d) return d;
    }
    say(`Select ${name} in the next dialog.`);
    let done = false;
    const poll = (async () => {
        while (!done) {
            const d = await find();
            if (d) return d;
            await sleep(300);
        }
    })();
    const ask = needGesture('Select updater').then(async () => {
        const [d] = await navigator.hid.requestDevice({ filters: [filter] });
        if (!d) throw new Error('no updater selected');
        return d;
    });
    try {
        return await Promise.race([poll, ask]);
    } finally {
        done = true;
        gate.classList.remove('on');
    }
}

// ---------- detection ----------

function show(id, on) {
    $(id).hidden = !on;
}

function render() {
    show('kb', !!kb);
    ['act-stock', 'act-hybrid', 'act-classic', 'kb-msg'].forEach((id) => show(id, false));
    if (!kb) return;
    const latest = manifest ? `Latest release: ${manifest.version}.` : '';
    if (kb.kind === 'stock') {
        const m = MODELS[kb.model];
        $('kb-title').textContent = `${m.name} (${kb.type})`;
        $('kb-sub').textContent = `Running PFU's firmware. ${latest}`;
        $('stock-text').textContent = m.hybrid
            ? "Installs ECK through PFU's updater, then sets up updates and recovery (you save two backup files). Takes a few minutes."
            : "Installs ECK through PFU's updater. Takes about a minute; keep the keyboard plugged in.";
        show('act-stock', true);
    } else if (kb.kind === 'eck') {
        $('kb-title').textContent = kb.hybrid ? 'HHKB Professional Hybrid' : 'HHKB Professional Classic or Classic Type-S';
        $('kb-sub').textContent = `Running ECK ${kb.version || '(version unknown)'}. ${latest}`;
        show(kb.hybrid ? 'act-hybrid' : 'act-classic', true);
    } else {
        $('kb-title').textContent = kb.kind === 'silent' ? "PFU's updater (not answering)" : `Unsupported keyboard (${kb.type})`;
        $('kb-sub').textContent = '';
        $('kb-msg').innerHTML =
            kb.kind === 'silent'
                ? 'The keyboard is in PFU\'s updater but does not answer. On a Hybrid that already has ECK\'s Bluetooth firmware, PFU\'s updater cannot install anything: see <a href="https://github.com/jlpoltrack/ECK/blob/main/docs/hybrid-update.md">Hybrid updates</a>.'
                : 'ECK supports the HHKB Professional Hybrid Type-S (PD-KB800BS), Classic and Classic Type-S (US layout).';
        show('kb-msg', true);
    }
}

async function detect(dev) {
    if (dev.vendorId === 0x04fe) {
        const pfu = await eck.Pfu.open(dev);
        try {
            const info = await pfu.info();
            kb = info.model ? { kind: 'stock', pfu, ...info } : { kind: 'unsupported', type: info.type };
        } catch {
            kb = { kind: 'silent' };
        }
    } else if (eck.isEck(dev)) {
        const via = await eck.Via.open(dev);
        kb = { kind: 'eck', via, hybrid: via.hybrid, version: await via.version() };
    } else {
        throw new Error(`${dev.productName || 'this device'} is not an HHKB`);
    }
    log(`Connected: ${dev.productName || 'HHKB'}`);
    render();
}

const pick = (devs) => devs.find((d) => d.vendorId === 0x04fe && d.collections.some((c) => c.usagePage === 0xff00)) || devs.find(eck.isEck);

/** A keyboard granted earlier and plugged in: detected without a click. */
async function autoDetect() {
    const dev = pick(await navigator.hid.getDevices());
    if (dev) await detect(dev).catch(() => {});
}

/** After an action the keyboard restarts as a new device: forget it and look again. */
async function redetect() {
    for (const k of [kb?.via, kb?.pfu]) if (k) await k.close();
    const shown = !!kb;
    kb = null;
    if (shown) $('kb-sub').textContent = 'Reconnecting…'; // the card stays until the keyboard is back
    controls.forEach((b) => (b.disabled = true));
    for (let i = 0; i < 20 && !kb && !inDfu; i++) {
        await sleep(1000);
        await autoDetect();
    }
    controls.forEach((b) => (b.disabled = false));
    if (kb) return;
    render();
    if (inDfu || (await eck.Dfu.find())) {
        show('recover', true);
        say("The keyboard is in update mode: click Recover keyboard firmware.");
        return;
    }
    // Firmware with a new USB serial number needs the browser's permission again
    say('The keyboard did not come back by itself: click Select keyboard and pick it.');
    await needGesture('Select keyboard');
    await connect();
}

// ---------- actions ----------

/** Connect through the browser's device dialog (needs a click). */
const connect = () =>
    run('Connect', async () => {
        for (const k of [kb?.via, kb?.pfu]) if (k) await k.close();
        kb = null;
        render();
        const dev = pick(await navigator.hid.requestDevice({ filters: [eck.PFU_FILTER, eck.ECK_FILTER] }));
        if (!dev) throw new Error('no HHKB selected');
        await detect(dev);
    });
$('b-connect').onclick = connect;

$('b-install').onclick = async () => {
    await run('Install ECK', async () => {
        const { pfu, model } = kb;
        const data = await firmware(model);
        const hybrid = MODELS[model].hybrid;
        const stub = hybrid ? await bootStub() : null;
        kb.pfu = null;
        if (hybrid) log('Stage 1 of 2: install ECK on both chips.');
        await eck.pfuInstall(pfu, data, { reconnect: () => pfuReconnect(), log, progress });
        if (!hybrid) return;
        say('Stage 2 of 2: set up updates and recovery. Click Continue, then pick the keyboard again (it now runs ECK).');
        await needGesture('Continue');
        const via = await openVia();
        const dfu = await enterDfu(via);
        await eck.installBootStub(dfu, stub, { log, save, confirm: confirmStub });
        inDfu = false;
    });
    if (manifest?.files.via) log('Next: load via.json in VIA to change the keymap and settings (Download via.json on this page).');
    await redetect();
};

$('b-stm32').onclick = async () => {
    await run('Update keyboard firmware', async () => {
        const data = await firmware('AHUX01');
        const stub = await bootStub();
        const dfu = await enterDfu(await openVia());
        await eck.stm32Update(dfu, data, { log, progress, stub, save, confirm: confirmStub });
        inDfu = false;
    });
    await redetect();
};

$('b-nrf').onclick = async () => {
    await run('Update Bluetooth firmware', async () => {
        const data = await firmware('AHUX01');
        const via = await openVia();
        try {
            await eck.nrfUpdate(via, data, { log, progress });
        } finally {
            await via.close();
        }
    });
    await redetect();
};

$('b-backup').onclick = async () => {
    await run('Back up settings', async () => {
        // Ask where to save first, while the click counts: a cancel then changes nothing
        const write = await pickFile(`eck-settings-${new Date().toISOString().slice(0, 10)}.bin`);
        const dfu = await enterDfu(await openVia());
        const eeprom = await eck.backup(dfu);
        await dfu.leave();
        inDfu = false;
        await write(eeprom);
    });
    await redetect();
};

$('b-restore').onclick = () => $('eepromfile').click();

$('eepromfile').onchange = async () => {
    const file = $('eepromfile').files[0];
    $('eepromfile').value = '';
    if (!file) return;
    await run('Restore settings', async () => {
        const data = new Uint8Array(await file.arrayBuffer());
        if (data.length !== eck.EEPROM_SIZE) throw new Error(`${file.name} is not a settings backup (${data.length} bytes)`);
        if (!confirm(`Restore ${file.name}? It replaces this keyboard's keymap, macros, calibration and settings.`))
            throw new Error('cancelled: nothing changed');
        const dfu = await enterDfu(await openVia());
        await eck.restoreEeprom(dfu, data, { log });
        inDfu = false;
    });
    await redetect();
};

$('l-recover').onclick = (e) => {
    e.preventDefault();
    show('recover', true);
    $('recover').scrollIntoView({ behavior: 'smooth' });
};

$('b-recover').onclick = async () => {
    await run('Recover keyboard firmware', async () => {
        const data = await firmware('AHUX01');
        const stub = await bootStub();
        let dev = await eck.Dfu.find();
        if (!dev && (await navigator.hid.getDevices()).some(eck.isEck))
            throw new Error('the keyboard is running ECK, not in update mode: use Update keyboard firmware, or follow the steps above');
        if (!dev) {
            say('Select "DFU in FS Mode" (STMicroelectronics) in the next dialog.');
            dev = await withGesture('Select update mode device', () => eck.Dfu.request());
        }
        const dfu = await eck.Dfu.open(dev);
        inDfu = true;
        await eck.stm32Update(dfu, data, { log, progress, stub, save, confirm: confirmStub });
        inDfu = false;
    });
    await redetect();
};

$('b-nrf-recover').onclick = async () => {
    await run('Recover Bluetooth firmware', async () => {
        const data = await firmware('AHUX01');
        const via = await openVia();
        if (!via.hybrid) throw new Error('Bluetooth recovery is for the Hybrid');
        try {
            await eck.nrfUpdate(via, data, { recovery: true, log, progress });
        } finally {
            await via.close();
        }
    });
    await redetect();
};

// ---------- Realforce R3S ----------

let rk = null; // the connected R3S: {kind: 'stock' | 'eck' | 'unsupported', ...}
const isTopre = (d) => eck.isUpdater(d, eck.TOPRE_FILTER);

/** R3S firmware: the local file (Advanced) if chosen, else the bundled release. Checked here. */
async function r3sFirmware() {
    const f = $('hfbfile').files[0];
    let data;
    if (f) data = new Uint8Array(await f.arrayBuffer());
    else if (manifest?.files.r3s) data = await fetchBytes(`firmware/${manifest.files.r3s}`);
    else throw new Error('no bundled R3S release here: choose a local .rfb under Advanced');
    const { model } = eck.parseHfb(data);
    if (model !== 'R3SD####') throw new Error(`the file is for ${model}, not the Realforce R3S`);
    return data;
}

function r3sRender() {
    show('rkb', !!rk);
    ['r-act-stock', 'r-act-eck', 'rkb-msg'].forEach((id) => show(id, false));
    if (!rk) return;
    const latest = manifest ? `Latest release: ${manifest.version}.` : '';
    if (rk.kind === 'stock') {
        $('rkb-title').textContent = `Realforce R3S (${rk.type})`;
        $('rkb-sub').textContent = `Running Topre's firmware. ${latest}`;
        show('r-act-stock', true);
    } else if (rk.kind === 'eck') {
        $('rkb-title').textContent = 'Realforce R3S';
        $('rkb-sub').textContent = `Running ECK ${rk.version || '(version unknown)'}. ${latest}`;
        show('r-act-eck', true);
    } else {
        $('rkb-title').textContent = `Unsupported keyboard (${rk.type})`;
        $('rkb-sub').textContent = '';
        $('rkb-msg').textContent = 'ECK supports the Realforce R3S US TKL (R3SD13).';
        show('rkb-msg', true);
    }
}

async function r3sDetect(dev) {
    if (isTopre(dev)) {
        const pfu = await eck.Pfu.open(dev);
        const info = await pfu.info();
        rk = info.model === 'R3SD####' ? { kind: 'stock', pfu, ...info } : { kind: 'unsupported', type: info.type };
    } else if (eck.isR3s(dev)) {
        const via = await eck.Via.open(dev);
        rk = { kind: 'eck', via, version: await via.version() };
    } else {
        throw new Error(`${dev.productName || 'this device'} is not a Realforce R3S`);
    }
    log(`Connected: ${dev.productName || 'Realforce R3S'}`);
    r3sRender();
}

const r3sPick = (devs) => devs.find(isTopre) || devs.find(eck.isR3s);

async function r3sAutoDetect() {
    const dev = r3sPick(await navigator.hid.getDevices());
    if (dev) await r3sDetect(dev).catch(() => {});
    return !!rk;
}

async function r3sClose() {
    for (const k of [rk?.via, rk?.pfu]) if (k) await k.close();
    rk = null;
}

/** After an action the R3S restarts as a new device: look again; ST's loader means Recover. */
async function r3sRedetect() {
    const shown = !!rk;
    await r3sClose();
    if (shown) $('rkb-sub').textContent = 'Reconnecting…';
    controls.forEach((b) => (b.disabled = true));
    for (let i = 0; i < 20 && !rk && !inDfu; i++) {
        await sleep(1000);
        await r3sAutoDetect();
    }
    controls.forEach((b) => (b.disabled = false));
    if (rk) return;
    r3sRender();
    if (inDfu || (await eck.Dfu.find())) {
        show('r-recover', true);
        say('The keyboard is in update mode: click Recover firmware.');
        return;
    }
    say('The keyboard did not come back by itself: click Select keyboard and pick it.');
    await needGesture('Select keyboard');
    await r3sConnect();
}

/** ECK on the R3S: the connected one, a granted one, or one picked in a dialog. */
async function r3sVia() {
    if (rk?.via) {
        const v = rk.via;
        rk.via = null;
        return v;
    }
    const granted = (await navigator.hid.getDevices()).find(eck.isR3s);
    if (granted) return eck.Via.open(granted);
    say('Select the keyboard (ECK Realforce R3S) in the next dialog.');
    return withGesture('Select keyboard', async () => {
        const dev = (await navigator.hid.requestDevice({ filters: [eck.R3S_FILTER] })).find(eck.isR3s);
        if (!dev) throw new Error('no R3S running ECK selected');
        return eck.Via.open(dev);
    });
}

const r3sConnect = () =>
    run('Connect', async () => {
        await r3sClose();
        r3sRender();
        const dev = r3sPick(await navigator.hid.requestDevice({ filters: [eck.TOPRE_FILTER, eck.R3S_FILTER] }));
        if (!dev) throw new Error('no Realforce R3S selected');
        await r3sDetect(dev);
    });
$('r-connect').onclick = r3sConnect;

$('r-install').onclick = async () => {
    await run('Install ECK', async () => {
        const data = await r3sFirmware();
        const { pfu } = rk;
        rk.pfu = null;
        const reconnect = () => pfuReconnect(eck.TOPRE_FILTER, "Topre's updater (REALFORCE R3S)");
        await eck.pfuInstall(pfu, data, { reconnect, log, progress });
    });
    if (manifest?.files.r3s_via) log('Next: load the R3S via.json in VIA to change the keymap and settings (Download via.json on this page).');
    await r3sRedetect();
};

$('r-update').onclick = async () => {
    await run('Update firmware', async () => {
        const data = await r3sFirmware();
        const dfu = await enterDfu(await r3sVia());
        await eck.r3sUpdate(dfu, data, { log, progress });
        inDfu = false;
    });
    await r3sRedetect();
};

$('r-backup').onclick = async () => {
    await run('Back up settings', async () => {
        const write = await pickFile(`eck-r3s-settings-${new Date().toISOString().slice(0, 10)}.bin`);
        const dfu = await enterDfu(await r3sVia());
        const eeprom = await eck.backup(dfu);
        await dfu.leave();
        inDfu = false;
        await write(eeprom);
    });
    await r3sRedetect();
};

$('r-restore').onclick = () => $('r-eepromfile').click();

$('r-eepromfile').onchange = async () => {
    const file = $('r-eepromfile').files[0];
    $('r-eepromfile').value = '';
    if (!file) return;
    await run('Restore settings', async () => {
        const data = new Uint8Array(await file.arrayBuffer());
        if (data.length !== eck.EEPROM_SIZE) throw new Error(`${file.name} is not a settings backup (${data.length} bytes)`);
        if (!confirm(`Restore ${file.name}? It replaces this keyboard's keymap, macros and settings.`))
            throw new Error('cancelled: nothing changed');
        const dfu = await enterDfu(await r3sVia());
        await eck.restoreEeprom(dfu, data, { log, r3s: true });
        inDfu = false;
    });
    await r3sRedetect();
};

$('l-r-recover').onclick = (e) => {
    e.preventDefault();
    show('r-recover', true);
    $('r-recover').scrollIntoView({ behavior: 'smooth' });
};

$('r-recover-b').onclick = async () => {
    await run('Recover firmware', async () => {
        const data = await r3sFirmware();
        let dev = await eck.Dfu.find();
        const hid = await navigator.hid.getDevices();
        if (!dev && hid.some(eck.isR3s)) throw new Error('the keyboard is running ECK, not in update mode: use Update firmware');
        if (!dev && hid.some(isTopre)) throw new Error("the keyboard is in Topre's updater: connect it above and click Install ECK");
        if (!dev) {
            say('Select "DFU in FS Mode" (STMicroelectronics) in the next dialog.');
            dev = await withGesture('Select update mode device', () => eck.Dfu.request());
        }
        const dfu = await eck.Dfu.open(dev);
        inDfu = true;
        await eck.r3sUpdate(dfu, data, { log, progress });
        inDfu = false;
    });
    await r3sRedetect();
};

// ---------- tabs and start ----------

let tab = 'hhkb';
function setTab(name) {
    tab = name;
    for (const t of ['hhkb', 'r3s']) {
        show(`tab-${t}`, t === name);
        $(`t-${t}`).setAttribute('aria-selected', String(t === name));
    }
    history.replaceState(null, '', name === 'r3s' ? '#r3s' : location.pathname + location.search);
}
$('t-hhkb').onclick = () => setTab('hhkb');
$('t-r3s').onclick = () => setTab('r3s');
setTab(location.hash === '#r3s' ? 'r3s' : 'hhkb');

$('win-note').hidden = !windows;
if (!('hid' in navigator) || !('usb' in navigator)) {
    $('support').hidden = false; /* also shown by the page's inline check */
    controls.forEach((b) => (b.disabled = true));
} else {
    // A granted R3S (and no HHKB) opens on the R3S tab
    Promise.all([autoDetect(), r3sAutoDetect()]).then(([, r3s]) => {
        if (r3s && !kb) setTab('r3s');
    });
}

fetch('firmware/manifest.json')
    .then((r) => (r.ok ? r.json() : Promise.reject()))
    .then((m) => {
        manifest = m;
        $('fwinfo').textContent = `Bundled release: ${m.version}.`;
        if (m.files.via) {
            $('vialink').href = `firmware/${m.files.via}`;
            $('via').hidden = false;
        }
        if (m.files.r3s_via) {
            $('r-vialink').href = `firmware/${m.files.r3s_via}`;
            $('r-via').hidden = false;
        }
        render();
        r3sRender();
    })
    .catch(() => ($('fwinfo').textContent = 'No bundled release here: choose a local .hfb.'));
