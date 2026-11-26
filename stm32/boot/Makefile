# Boot stub for the HHKB Hybrid's STM32 Bank 1. Output: _build/hybrid_boot.bin
CMSIS := ../vendor/st
CC := arm-none-eabi-gcc
CFLAGS := -mcpu=cortex-m0plus -mthumb -Os -std=c11 -Wall -Wextra -Werror -ffreestanding \
  -ffunction-sections -DSTM32L072xx -I../hybrid \
  -I$(CMSIS)/cmsis_device_l0/Include -I$(CMSIS)/cmsis_core/Include
LDFLAGS := -nostdlib -Wl,--gc-sections -T boot.ld

_build/hybrid_boot.bin: _build/hybrid_boot.elf
	arm-none-eabi-objcopy -O binary $< $@
	arm-none-eabi-size $<

_build/hybrid_boot.elf: boot.c boot.ld ../hybrid/rom_dfu.h ../hybrid/bank1_stage.h | _build
	$(CC) $(CFLAGS) $(LDFLAGS) boot.c -o $@

_build:
	mkdir -p $@

clean:
	rm -rf _build
.PHONY: clean
