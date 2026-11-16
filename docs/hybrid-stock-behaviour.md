**Documentation:** [Install](install.md) • [Backup & restore](backup-restore.md) • [Build](build.md) • [Config](config.md) • [VIA](via.md) • [Hybrid port](hybrid-port.md) • [Hybrid updates](hybrid-update.md) • [Hybrid hardware](hybrid-hardware.md)

# HHKB Professional Hybrid: stock behaviour ECK relies on or follows

From static analysis of the owner's dumps (`backups/hybrid-2026-11-14/`, not in git) and PFU's `HHKB800_FW_A048.hfb`. Addresses: `0x08xxxxxx` = stock STM32 code, `0x2xxxx`/`0x3xxxx` = PFU nRF52 app, `0x7xxxx` = PFU nRF52 bootloader. **C** = confirmed in code, **I** = inferred. PFU's own STM32 <-> nRF52 link protocol is not documented here: ECK replaces both apps and uses its own link (`common/ble_link.h`).

## nRF52 staging and the PFU bootloader (ECK nRF52 updates use it)

| Item | Value | Where | |
| --- | --- | --- | --- |
| Staged image | raw bytes of the `.hfb` nRF section (SoftDevice + app, image of `0x1000`..`0x36000`), at `0x37000` | `0x2a908`, `0x2aa7a` | C |
| Record | 7 words at `0x36000`: `u32 size; u32 0; u8 md5[16]; u32 crc` with crc = CRC-16/MODBUS (init FFFF, reflected 0xA001, table `0x313e6` = `0000 C0C1 C181 0140`) over the first 24 bytes, zero-extended | app `0x2a976`..`0x2a9be`, CRC `0x2b71c` | C |
| Bootloader check `0x7a314` | word0 == FFFFFFFF -> "New Fw is NOT found", boot app. CRC mismatch -> "FW header crc is differnt", erase `0x37000` (61 pages) and `0x36000`. MD5 of `0x37000`+size mismatch -> "MD5 is different", erase both | `0x7a314`..`0x7a3c6` | C |
| Copy `0x7a3e4` | erase `0x1000` x 53 pages (to `0x36000`), copy size/4 words `0x37000` -> `0x1000`, MD5 over `0x1000`; OK -> erase staging and record ("Software writing is success."); fail -> erase `0x1000` region and reset (retries next boot, record kept) | `0x7a3e4`..`0x7a4c4` | C |
| Then | `sd_mbr_command(INIT_SD)` (`0x79794`), vector base `0x26000` (`0x796f8`), jump to `0x26000` (`0x7987c`). No app CRC/validity check; app address fixed | `0x797b0` | C |
| Size limit | the bootloader erases only `0x1000`..`0x36000`; an image over `0x35000` would overwrite the record page (I) | | I |


## Power, battery and LED (stock STM32 app and PFU nRF app; ECK follows them)

Source: stock Bank 2 (STM32 section of `HHKB800_FW_A048.hfb`, base `0x08010000`); `backups/hybrid-2026-11-14/bank2.bin` is an ECK build (no PFU link code, different bytes), so it was not used. Ticks are TIM21 ticks of about 1 ms (stock Bank 2 has the same two TIM21 set-ups, `0x080152a4` ARR 40 and `0x08015304` ARR 160, PSC 99). The 1 ms tick is also what makes the LED timings below match the manual.

### Battery

| Item | Detail | Where | |
| --- | --- | --- | --- |
| ADC | Not used for the battery. The STM32 ADC is configured once, channel 1 (PA1, key sensing) only (`0x080150b8`, `0x04000002` = `ADC_CHANNEL_1`). The nRF app never touches SAADC (`0x40007000` does not occur; the only peripherals it uses are CLOCK/POWER, SPI0, GPIOTE, TIMER1, TEMP, RTC1, NVMC plus errata addresses). No PVD handler (vector 1 is the default handler) | | C |
| Inputs | Three digital inputs on the STM32, read in `0x0801a598`: **PB9 low -> level 3, else PB8 low -> level 2, else PB7 low -> level 1, else level 0**. Outputs of a three-threshold voltage detector; ECK measured its thresholds at the battery terminals (falling): about 2.11, 2.04 and 1.94 V, see [hybrid-hardware.md](hybrid-hardware.md) | `0x0801a59a`..`0x0801a5d4` | C |
| Percent | `0x2000007b` = 100 / 15 / 5 / 0 for level 0 / 1 / 2 / 3, forced to 100 while USB power is present | `0x0801a63c`..`0x0801a686` | C |
| Filtering | Raw level in `0x2000001f`; latched level `0x20000014`. A worse (higher) level latches at once; a better level must persist for more than 10 samples, and recovering to level 1 latches as 2 | `0x0801a5ee`..`0x0801a63a` | C |
| Interval | `0x080111d4` (reads the inputs, drives the battery LED) runs when the 1 ms counter `0x20000050` passes 99, i.e. every 100 ms | `0x0801a014`..`0x0801a022` | C |
| To nRF | Bank 1 sends a ≤100 value with `AA 80` then `AA v` (start-up step 5); same 100/15/5/0 set is the likely payload (I) | | I |
| At power-on | `0x08011380`: 11 reads 20 ms apart; if level 3 (empty) and on batteries: orange on, refuse to start; then a 2 s hold of the switch (`0x2000007c` ≥ 2000) forces the off routine `0x08011494` | `0x08011380`..`0x08011470` | C |

States vs. the manual (C for the code, mapping to the manual's wording is I): the battery LED runs only in the Bluetooth modes (`0x20000044` = 2, 3 or 4) and only on batteries (`0x080111da`..`0x080111f8`).

| Latched level | Pattern (`0x080111fa`..`0x0801135a`) | Matches manual |
| --- | --- | --- |
| 1 (PB7, 15 %) | 1 orange blink at once, then 1 blink when the slow counter passes 15, then every time it passes 30 | "low": once every 30 s |
| 2 or 3 (PB8/PB9, 5 % / 0 %) | 2 orange blinks, repeated every time the slow counter passes 15 | "replace": twice every 15 s |

Timing: the routine runs every 100 ms; blink half-period = 6 runs (0.6 s on, 0.6 s off, counter `0x2000001d`); slow counter `0x20000018` +1 every 11 runs (1.1 s, counter `0x2000001e`). So 15 and 30 counts are about 16.5 s and 33 s. `0x20000016` = remaining LED toggles (2 = one blink, 4 = two).

### Power switch

| Item | Detail | Where | |
| --- | --- | --- | --- |
| Pin | **PC5**, active low (pressed = 0), falling-edge EXTI line 5 (EXTI4_15). Debounce: read, busy-wait 5000 loops (1000 in one mode), read again | e.g. `0x0801a3c6`..`0x0801a3f6` | C |
| Hold timer | `0x2000007c` counts 1 ms ticks only while `0x20000085` (pressed) is set | TIM callback `0x08013fd6`..`0x08013fe4` | C |
| ON -> OFF | in the ON state, pressed for ≥ 2000 ticks (2 s) sets `0x20000084`; the main loop then sets mode `0x20000044` = 0 (OFF) (`0x0801a002`..`0x0801a012`). A release after 100..1999 ticks sets `0x2000000a` (short press: battery re-check `0x08011380` and mode 4, `0x0801a0a0`..`0x0801a0bc`; purpose I) | `0x0801a404`..`0x0801a466` | C |
| OFF state | `0x0801164c` (mode 0): orange on; tell the nRF (see below); wait for release; `0x0801c248` drives the matrix/sensor GPIOs to their idle levels; then, on batteries, **STOP mode** (`0x08011abc` -> `0x08012d84`: PWR_CR LPSDS=1, PDDS=0, SLEEPDEEP, WFI). Wake on any enabled EXTI (PC5 switch, PC13 USB) | `0x0801164c`..`0x080117a6` | C |
| OFF -> ON | after wake: switch pressed (debounced) -> hold timer; at ≥ 1000 ticks (1 s) blue on and `0x20000003`=1; on release (and < 10000 ticks) PC14 high (sensor power), battery check `0x08011380`, blue on, pick the mode `0x0801158c`. Held ≥ 10000 ticks: blue off, orange on for 3000 ms, off (meaning I) | `0x080117ac`..`0x080118da` | C |
| Forced off | `0x08011494` (empty battery): stop link, idle GPIOs, mask EXTI5 (`EXTI_IMR &= ~0x20`), STOP, then `NVIC_SystemReset` | `0x08011494`..`0x080114fe` | C |
| Told to the nRF | `0x080105b4` at OFF entry: PB6 pulse, wait poll `02`, send `AA 40`, wait poll `03`, send `AA 10` (100-tick waits). This is the same exchange as Bank 1 step 3: the nRF stops advertising / disconnects (`0x29fbc`) and answers `83 01` | `0x080105b4`..`0x0801067a` | C |
| nRF OFF | the PFU app never calls System OFF: its only SoftDevice SoC calls are flash (SVC 0x28/0x29), `sd_app_evt_wait` (0x41, idle loop `0x280d0`) and two crypto/rand calls. In OFF it stays in its idle loop with the radio stopped. Stock Bank 2 drives PD2 (nRF reset) only at boot (`0x0801c448` high, `0x0801c0d8` low) | | C (SVC names I) |
| P0.11/P0.12 | configured as inputs with pull-up (`PIN_CNF` = 0xC at `0x28a62`), no other access found; not the power switch | | C |

### USB power detection

**PC13 low = USB power present** (C for the code, VBUS wiring I): `0x0801ad0c` sets `0x200000ed` (0 when PC13 high, else 1/2); `0x0801bd80` returns it. It also chooses the clock set-up (Bank 1 `0x0800d9ae`: PC13 high -> 4 MHz battery clock, low -> HSI16 + HSI48 for USB) and is a STOP wake source (EXTI13).

### Auto power-off (partly found)

The mode timers count 1 ms ticks and time out at 60000 (1 min) (`0x08019980`, `0x08019c3a`, `0x08019d7a`, `0x08019dc8`, `0x08019fba`, `0x0801a19c`, `0x0801a54e`; counters `0x200000a0`, `0x200000a4` (runs while `0x20000095` set), `0x200000a8`, `0x200000c4`, incremented in the TIM21 callback `0x08013f7c`). On a timeout: if USB powered, or `0x20000086[5]` == 1, go to mode 5, else mode 0 (OFF). `0x20000086[0..5]` is a 6-byte switch-state array that the HID info reply also returns (`0x08011544`..`0x08011566`); `[5]` being DIP SW6 is I. `0x20000097` counts retries (≥ 5 ends the sequence, `0x0801a512`). **The 10-minute (pairing) and 30-minute (idle) values from the manual were not located**; they are presumably built from these 1-minute steps, not confirmed.

### LEDs

| Item | Detail | Where | |
| --- | --- | --- | --- |
| API | `0x0801c3b0` on(n), `0x0801c358` off(n), `0x0801c410` toggle(n); state in `0x200000eb[n]`; GPIOA BSRR, active high | | C |
| n = 0 | **PA8** | `0x0801c368`, `0x0801c3c8` | C |
| n = 1 | **PA9**; has priority: while LED 1 is on, LED 0 is forced off and restored when LED 1 goes off (only one lit at a time) | `0x0801c37c`..`0x0801c3a4`, `0x0801c3dc`..`0x0801c406` | C |
| Colours | **PA9 = orange, PA8 = blue** (seen on the board, 2026-11-15) | | C |
