[ECK docs](overview.md)

# Backup & restore (Classic)

Back up Flash, EEPROM and option bytes before the first update. EEPROM holds board-specific factory thresholds: never copy it from another keyboard.

On the Hybrid, use `tools/hybrid_update.py backup` / `restore-eeprom` (or the web flasher) over USB instead; see [Hybrid updates](hybrid-update.md).

| Area | Address | Content |
| --- | --- | --- |
| Bank 1 | `0x08000000–0x0800FFFF` | PFU's updater |
| Bank 2 | `0x08010000–0x0801FFFF` | Stock firmware or ECK |
| EEPROM | `0x08080000–0x080817FF` | Factory data and ECK settings |
| Option bytes | `0x1FF80000`, 32 bytes | Protection and boot settings |

## Normal recovery

Use SW6 to return to Bank 1 (below), then install a matching `.hfb` (ECK, or stock from the [HHKB download centre](https://happyhackingkb.com/download/)) with Keymap Tool or `hhkb_flasher.py`.

## SW6 recovery

On the Classic (`HHKB_SW6_REBOOT_TO_BANK1` in `stm32/boards/hhkb/board_config.h`; off on the Hybrid), starting ECK with SW6 ON erases Bank 2's first 128-byte page (its vector table) and resets into Bank 1. Bank 2 then stays unbootable until a full image is written through PFU's updater, DFU or SWD. Keep SW6 OFF in normal use.

## STM32 ROM DFU

ST's DFU loader in system ROM works whatever Bank 1 and Bank 2 hold, and needs no debugger.

1. Open the keyboard and remove the metal shield on the controller side.
2. While connecting USB, short the **right-hand pads of R6 and R85** (as in the photo; check the markings first). This raises BOOT0.
3. `dfu-util -l` should show `0483:df11`; then remove the short.

![R6 and R85 pads for ROM DFU](https://i.imgur.com/oTwQLPg.jpeg)

```bash
# Backup
dfu-util -a 0 -s 0x08000000:131072 -U hhkb-flash.bin       # both banks
dfu-util -a 2 -s 0x08080000:6144 -U hhkb-eeprom.bin
dfu-util -a 1 -s 0x1FF80000:32 -U hhkb-option-bytes.bin
# Restore (same keyboard only)
dfu-util -a 0 -s 0x08000000 -D hhkb-flash.bin
dfu-util -a 2 -s 0x08080000 -D hhkb-eeprom.bin
dfu-util -a 1 -s 0x1FF80000 -D hhkb-option-bytes.bin      # only if needed
```

DFU writes raw `.bin` files, not `.hfb`. Writing an ECK `.bin` to `0x08010000` works, but doesn't update Bank 1's metadata, so Bank 1 may refuse it; a readback proves the flash contents, not that Bank 1 will boot it.

## SWD (J-Link)

Five pads beside CN3:

| Pad | Signal | J-Link |
| --- | --- | --- |
| top row: GND, NRST, VCC | ground, reset, 3.3 V | GND, RESET (optional), VTref |
| bottom row: SWCLK, SWDIO | PA14, PA13 | SWCLK, SWDIO |

Power the board from USB *or* the probe, not both. Backup script (`JLinkExe -device STM32L072RB -if SWD -speed 4000 -autoconnect 1 -CommanderScript backup.jlink`):

```text
r
halt
savebin hhkb_flash_128k.bin 0x08000000 0x00020000
savebin hhkb_eeprom_6k.bin 0x08080000 0x00001800
savebin hhkb_option_bytes.bin 0x1FF80000 0x00000020
q
```

To restore Flash and EEPROM to the same keyboard, use `loadbin` with the same files and addresses, then `r`, `g`, `q`. Restore option bytes only if needed, and only from the same board.
