[ECK docs](overview.md)

# Hybrid port: design notes and open work

How ECK runs on the HHKB Professional Hybrid, what was found on the way, and what is still open. Board facts (pins, memory maps, power states, SWD access) are in [Hybrid hardware](hybrid-hardware.md); stock firmware analysis in [Hybrid stock behaviour](hybrid-stock-behaviour.md). Test keyboard: Hybrid Type-S PD-KB800BS, USB `04FE:0021`, stock A0.48. Findings come from static analysis of PFU's `HHKB800_FW_A048.hfb` (and the Classic Type-S `HHKB401_FW_A803.hfb`) unless marked tested.

The test keyboard is assembled (no SWD or power analyser), runs ECK on both chips, with ECK's boot stub in Bank 1. A full pre-ECK backup (both chips, EEPROM, option bytes) is in `backups/hybrid-2026-11-14/` (git-ignored). For USB tests it must be on a USB **host**: on a power bank, ECK sees no host and sends everything to Bluetooth.

## Updates

User steps: [Hybrid updates](hybrid-update.md). Round trip ECK → stock (both chips byte-identical to PFU's) → ECK tested.

- **STM32:** ST's USB DFU loader in system memory (`0483:DF11`). Its reset code jumps back into any valid flash bank unless BOOT0 selected it, so `stm32/hybrid/rom_dfu.h` runs the ROM's data init and enters the loader past that check (ROM contents checked first). Interrupts must be enabled on entry, or the loader's USB never answers. The bootloader key does this on USB power only; on batteries or an unknown ROM it does nothing.
- **Bank 1 boot rule (tested):** Bank 1 boots Bank 2 when EEPROM `0x08080360 = 1` and `0x08080370` holds the `.hfb` inner CRC (CRC-16/MCRF4XX of the 64 KB Bank 2, little-endian word). Clearing `0x370` alone keeps it in the updater. ECK's boot stub (`stm32/boot/`, 720 B) applies the same rule, so either Bank 1 boots ECK; it starts the DFU loader instead if the power switch is held with USB power present. After an SWD write to Bank 2, set both words.
- **nRF52:** VIA id 30 → link frames `BLE_LINK_UPDATE` (22 data bytes each) → `nrf52/nrf_update.c` stages the image at `0x37000` with the record at `0x36000`, which PFU's nRF52 bootloader installs at the next reset. The SoftDevice is stopped meanwhile; bonds (FDS, `0x75000-0x77FFF`) lie outside the staging area. The scan drops to 50 Hz during staging: at 1000 Hz it starved the link task (20 ms per chunk instead of 2.2 ms).
- **PFU's updater needs PFU's nRF52 app (observed with `tools/bench/nrf_pc_monitor.py`).** On entry Bank 1 resets the nRF52 through PD2, makes SPI1 a slave, raises PB6 and waits for the nRF52 to clock a transaction as master. PFU's app does and writes the nRF52 section to flash itself; ECK's app is an SPI slave, so the updater enumerates but never answers HID requests. After the final command Bank 1 resets the nRF52 again and PFU's bootloader copies the image (~4 s); `HYBRID_SETTLE_SEC` is 15 s.
- `hhkb_flasher.py --info` works on the Hybrid only in USB mode (Fn + Ctrl + 0). Reinstalling stock over stock took 166 s.
- The `.hfb` is built from ECK's parts plus Nordic's S132 v7.2.0 SoftDevice (stock carries v6.0.0; both put the app at `0x26000`, which PFU's bootloader jumps to); no PFU file is needed. Never run an ECK STM32 build against PFU's nRF52 app: both act as SPI master on the same lines.

## Bluetooth

- nRF52 app: [`nrf52/`](../nrf52/README.md) (nRF5 SDK 17.1.1 on S132 v7.2.0, app at `0x26000`). Link contract: `common/ble_link.h` (`BLE_LINK_VERSION`), 32-byte frames, STM32 SPI1 master. BLE HID keyboard and consumer reports, plus the Battery Service (stock percent steps 100/15/5/0).
- **Keys** (stock, PFU manual P3PC-6641-01EN in `backups/vendor-docs/`): Fn+Ctrl+1..4 select a host slot and Bluetooth, Fn+Ctrl+0 USB; both blink blue like a light press of the power switch (N blinks for slot N, 1 s on USB), in any LED mode. PFU lights blue once; the count is ECK's choice (owner, 2026-11-19). Fn+Ctrl+Q enters pairing standby (LED 2 blinks/s); then Fn+Ctrl+1..4 pairs that slot, Fn+Ctrl+Delete+1..4 forgets its host, Fn+Ctrl+Delete+Z forgets all (Delete must be down before the last key); forgetting blinks orange 3 times and the keyboard stays on (PFU turns it off; owner, 2026-11-19); Fn+Ctrl+X cancels. PFU uses Fn alone for Q, Z and X; ECK puts every chord on Fn+Ctrl so Fn+Q, X and Z stay free for the keymap (owner, 2026-11-19). Chords go by key position (`stm32/hybrid/hybrid_chords.c`), so VIA remaps don't change them, and their presses never reach the host. Slot, mode and Bluetooth scan rate persist at EEPROM `0x080817F8`.
- **Slots:** each has its own static random address (slot 1 the chip's own, slots 2-4 change the lowest byte), so a host bonded in one slot never sees the others. Only the slot's bonded host may connect (whitelist); an empty slot doesn't advertise.
- **Pairing:** deletes the slot's bond and advertises discoverably as `HHKB-Hybrid_n` for 10 min (LED 4 blinks/s). Passkey entry with MITM: the digits and Return go to the nRF52, not the host. LESC offered (SDK's Oberon library). A connection not encrypted within 30 s is dropped. Connection parameters: request 7.5-15 ms, latency 30, 6 s timeout, as ZMK (v0.7.2; was 15-30 ms per Apple's guidelines, where a Mac granted 15 ms, latency 22, 2 s). The host decides; VIA's HHKB Settings, Firmware, shows what it granted. Windows Swift Pair: Microsoft's beacon is in the advert only during pairing, with the name moved to the scan response. A host that rejects the slot's keys (it forgot the keyboard) is disconnected at once, orange blinks 5 times, and advertising pauses 30 s; pairing or a slot change clears it. Bonds survive nRF52 updates (tested). With no host paired and no USB host 3 s after start-up, slot 1 enters pairing, as stock.
- **Start-up** deletes bonds without ECK's slot tag (e.g. PFU's, which made the SoftDevice reject the identity list), so no manual erase is needed after installing.
- **Transport:** reports go to Bluetooth when no USB host is mounted or Bluetooth is selected; with Bluetooth selected but not connected they are dropped. The transport left behind gets an all-keys-released report. Every deliberate disconnect (slot switch, pairing, delete, power-off) first sends all-keys-released and waits up to 300 ms for the host: Windows otherwise kept the Ctrl of Fn+Ctrl+slot held (fixed and confirmed 2026-11-16). A full link queue holds the newest report until it drains rather than dropping it.
- **Scan rate:** 250 Hz while reports go to Bluetooth, otherwise 1000 Hz (both fixed).
- **LED mode 5, Bluetooth status** (Hybrid default for PA8; PA9 off): 1 s on after start-up or a pairing cancel; 4 blinks/s pairing or waiting for a passkey; 2 blinks/s in pairing standby; otherwise off, also while reconnecting (owner's choice). Power indications override both pins; orange PA9 wins over blue PA8, as stock.
- Bench tools over SWD: `hybrid_state.py`, `hybrid_cmd.py` (pair, cancel, select, delete, clear), `watch_inputs.py`, `rtt_dump.py` ([tools/bench](../tools/bench/README.md)).

## Key sensing (measured 2026-11-15 with `key_depth.py`)

- Rest readings 40-520 counts, full press ~2700. PFU's factory press thresholds sit at 1660-2130, about 60-75 % of the swing. ECK's generic uncalibrated scale fits this sensor badly, so factory thresholds are the default.
- **Interrupts during the scan caused the noise.** Each reading depends on the timing since the previous key, so an interrupt inside a scan skewed readings by up to ~200 counts. Holding interrupts off per scan half took spikes from ~5 per 1000 readings to none and typical spread from ~10 to 2.4 counts, and stopped 1000 Hz chattering. Ruled out: the nRF52 radio, its DC/DC, the boost converter.
- The first key read (Left Shift) needs the op-amp to settle after power-up each scan: a 20 µs wait (`HHKB_OPAMP_SETTLE_US`) took its spread from ~65 to 3 and fixed erratic presses.
- Calibration averages 16 scans for the rest level and needs 600 counts of travel per key (one scan and 150 counts let noise pass as a press range).
- Rapid Trigger against recorded noise (`tools/bench/rt_noise_sim.c`, key held at 60 %): no false toggles at the default 25 ‰ (~5/s before the fix).

## Power, battery and LED

`stm32/hybrid/hybrid_power.c` (hardware) and `hybrid_power_logic.c` (pure logic, `tests/test_hybrid_power.c`), run from the platform tick in the keyboard task: every 100 ms, every 10 ms while the switch is down. Currents: [Hybrid hardware](hybrid-hardware.md#5-power).

| Item | ECK behaviour | Source |
| --- | --- | --- |
| Battery level | PB7/PB8/PB9 every 100 ms, stock filter (worse at once, better after >10 samples), 100/15/5/0 %, 100 % on USB power. Sent to the nRF52 on change and after a link restart | stock |
| Low battery | Orange: 1 blink every 33 s at level 1, 2 blinks every 16.5 s at levels 2-3, at once on a new level; on batteries with Bluetooth as transport only | manual p.3, stock timing |
| Empty at power-on | 11 samples (1.1 s); level 3 on batteries: orange 2 s, then OFF | stock |
| Empty while on | Level 3 on batteries for 60 s: orange 2 s, then OFF. Stock checks only at power-on, switch press and wake; ECK stops cells running down to ~1.7 V (aroum/dmk tester, 2026-11-22) | ECK |
| Power switch PC5 | 2-sample debounce. Light press (100 ms-2 s): blue blinks the slot number, or 1 s on USB. 2 s hold: OFF. A press held through boot is ignored until released | manual p.4, p.12 |
| OFF | Orange at least 1 s and until release; `BLE_LINK_CMD_POWER_OFF` (nRF52 disconnects, System OFF after bond writes); USB, ADC, sensor and matrix lines off; STOP with wake on PC5 and PC13 | manual p.4, stock |
| Power-on | From STOP: switch held 1 s lights blue, release resets; USB power appearing resets. Inserting batteries goes quietly to OFF after the battery check, as stock (`RCC->CSR` tells this from ECK's own reset). The link task resets the nRF52 at once after OFF or deep sleep (a marker in RAM that survives the reset, `.noinit`), else if it doesn't answer within 500 ms | manual p.4, p.7 |
| Auto power-off | Batteries and SW6 OFF: 60 min without a key (owner's choice; manual 30), 10 min pairing, 1 min advertising without connecting. All enter deep sleep, woken by any key, the switch or USB power. Only a 2 s hold enters OFF | manual p.3, p.6, p.7 |
| Pairing deletes | Orange 3 blinks, keyboard stays on (manual p.14-15: OFF after the delete; owner's change 2026-11-19) | ECK |

Not copied from stock, or guessed: the 10 s hold on wake (blue off, orange 3 s); the "recovering to level 1 latches as 2" filter quirk; masking the switch on empty batteries (ECK still wakes and shows orange); matrix idle levels and the PB7-PB9/PC13 pulls; the 10 min pairing value (manual, not found in stock code).

## Stock firmware facts

- Command E0 (enter updater) writes EEPROM `0x08080360=0`, `0x08080370=0`, `0x08081350=1`, then resets (Hybrid `0x801abd2`, Classic A8.03 `0x801a326`). Bank 1 clears `0x08081350` on entry; meaning unknown.
- `0x08080348` is not a flag: it lies in the factory info block (`0x310-0x34F`) that command 0x02 returns.
- **Command E5 erases Bank 1 immediately. Never send E4-E7.**
- No watchdog. Bank 2 code can read Bank 1.
- The Classic SW6 path (`hhkb_enter_bank1_updater`) differs from stock: it writes `0x08080348` and erases the Bank 2 vector page, where stock E0 writes the three words above. On the Classic, `platform_bootloader_jump` (VIA 0x0B, `K_BOOTLOADER`) enters the ST ROM without a reset.
- EEPROM: ECK's Classic storage areas were all zero in this keyboard's stock dump and the factory calibration uses the Classic layout, so the Hybrid shares the Classic storage code. Stock keymap saves at `0x08081400 + n*0x100` would overlap ECK's `0x08081500`, which matters only when returning to stock.

## Open

Open items: [deferred.md](deferred.md).
