# Temporary R3S Bank 2 image that starts ST's DFU loader. Output: _build/dfu_jump.bin
ST := ../../stm32/vendor/st
CC := arm-none-eabi-gcc
CFLAGS := -mcpu=cortex-m0plus -mthumb -Os -std=c11 -Wall -Wextra -Werror -ffreestanding \
  -ffunction-sections -DSTM32L072xx -I../../stm32/hybrid \
  -I$(ST)/cmsis_device_l0/Include -I$(ST)/cmsis_core/Include
LDFLAGS := -nostdlib -Wl,--gc-sections -T dfu_jump.ld

_build/dfu_jump.bin: _build/dfu_jump.elf
	arm-none-eabi-objcopy -O binary $< $@
	arm-none-eabi-size $<

_build/dfu_jump.elf: dfu_jump.c dfu_jump.ld ../../stm32/hybrid/rom_dfu.h | _build
	$(CC) $(CFLAGS) $(LDFLAGS) dfu_jump.c -o $@

_build:
	mkdir -p $@

clean:
	rm -rf _build
.PHONY: clean
