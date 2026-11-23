PROJECT_NAME     := hhkb_hybrid_ble
TARGETS          := nrf52832_xxaa
OUTPUT_DIRECTORY := _build
# JIS=1: the Hybrid JIS app (HID country code 15, product ID 0x4012), built in its own folder.
ifeq ($(JIS),1)
OUTPUT_DIRECTORY := _build_jis
CFLAGS += -DECK_JIS
endif

# nRF5 SDK 17.1.1 (S132 v7.2.0), unpacked next to this repo. Layout follows the SDK's
# ble_app_hids_keyboard example; sources and include folders cut to what the app uses.
SDK_ROOT ?= ../../nRF5_SDK_17.1.1
# Use the arm-none-eabi-gcc on PATH instead of the SDK's hard-coded GCC location.
GNU_INSTALL_ROOT ?= $(dir $(shell which arm-none-eabi-gcc))
PROJ_DIR := .

$(OUTPUT_DIRECTORY)/nrf52832_xxaa.out: \
  LINKER_SCRIPT  := hybrid_ble_gcc_nrf52.ld

SRC_FILES += \
  $(SDK_ROOT)/modules/nrfx/mdk/gcc_startup_nrf52.S \
  $(SDK_ROOT)/modules/nrfx/mdk/system_nrf52.c \
  $(PROJ_DIR)/main.c \
  $(PROJ_DIR)/nrf_update.c \
  $(SDK_ROOT)/components/libraries/log/src/nrf_log_backend_rtt.c \
  $(SDK_ROOT)/components/libraries/log/src/nrf_log_backend_serial.c \
  $(SDK_ROOT)/components/libraries/log/src/nrf_log_default_backends.c \
  $(SDK_ROOT)/components/libraries/log/src/nrf_log_frontend.c \
  $(SDK_ROOT)/components/libraries/log/src/nrf_log_str_formatter.c \
  $(SDK_ROOT)/components/libraries/util/app_error.c \
  $(SDK_ROOT)/components/libraries/util/app_error_handler_gcc.c \
  $(SDK_ROOT)/components/libraries/util/app_error_weak.c \
  $(SDK_ROOT)/components/libraries/util/app_util_platform.c \
  $(SDK_ROOT)/components/libraries/util/nrf_assert.c \
  $(SDK_ROOT)/components/libraries/scheduler/app_scheduler.c \
  $(SDK_ROOT)/components/libraries/timer/app_timer2.c \
  $(SDK_ROOT)/components/libraries/timer/drv_rtc.c \
  $(SDK_ROOT)/components/libraries/sortlist/nrf_sortlist.c \
  $(SDK_ROOT)/components/libraries/crc16/crc16.c \
  $(SDK_ROOT)/components/libraries/fds/fds.c \
  $(SDK_ROOT)/components/libraries/hardfault/hardfault_implementation.c \
  $(SDK_ROOT)/components/libraries/atomic_fifo/nrf_atfifo.c \
  $(SDK_ROOT)/components/libraries/atomic_flags/nrf_atflags.c \
  $(SDK_ROOT)/components/libraries/atomic/nrf_atomic.c \
  $(SDK_ROOT)/components/libraries/balloc/nrf_balloc.c \
  $(SDK_ROOT)/components/libraries/fstorage/nrf_fstorage.c \
  $(SDK_ROOT)/components/libraries/fstorage/nrf_fstorage_sd.c \
  $(SDK_ROOT)/components/libraries/memobj/nrf_memobj.c \
  $(SDK_ROOT)/components/libraries/ringbuf/nrf_ringbuf.c \
  $(SDK_ROOT)/components/libraries/pwr_mgmt/nrf_pwr_mgmt.c \
  $(SDK_ROOT)/components/libraries/experimental_section_vars/nrf_section_iter.c \
  $(SDK_ROOT)/components/libraries/strerror/nrf_strerror.c \
  $(SDK_ROOT)/components/libraries/queue/nrf_queue.c \
  $(SDK_ROOT)/external/fprintf/nrf_fprintf.c \
  $(SDK_ROOT)/external/fprintf/nrf_fprintf_format.c \
  $(SDK_ROOT)/external/segger_rtt/SEGGER_RTT.c \
  $(SDK_ROOT)/external/segger_rtt/SEGGER_RTT_printf.c \
  $(SDK_ROOT)/integration/nrfx/legacy/nrf_drv_clock.c \
  $(SDK_ROOT)/integration/nrfx/legacy/nrf_drv_rng.c \
  $(SDK_ROOT)/modules/nrfx/soc/nrfx_atomic.c \
  $(SDK_ROOT)/modules/nrfx/drivers/src/nrfx_clock.c \
  $(SDK_ROOT)/modules/nrfx/drivers/src/nrfx_rng.c \
  $(SDK_ROOT)/modules/nrfx/drivers/src/nrfx_spis.c \
  $(SDK_ROOT)/modules/nrfx/drivers/src/prs/nrfx_prs.c \
  $(SDK_ROOT)/components/ble/common/ble_advdata.c \
  $(SDK_ROOT)/components/ble/common/ble_conn_params.c \
  $(SDK_ROOT)/components/ble/common/ble_conn_state.c \
  $(SDK_ROOT)/components/ble/common/ble_srv_common.c \
  $(SDK_ROOT)/components/ble/ble_advertising/ble_advertising.c \
  $(SDK_ROOT)/components/ble/ble_link_ctx_manager/ble_link_ctx_manager.c \
  $(SDK_ROOT)/components/ble/nrf_ble_gatt/nrf_ble_gatt.c \
  $(SDK_ROOT)/components/ble/nrf_ble_qwr/nrf_ble_qwr.c \
  $(SDK_ROOT)/components/ble/peer_manager/auth_status_tracker.c \
  $(SDK_ROOT)/components/ble/peer_manager/gatt_cache_manager.c \
  $(SDK_ROOT)/components/ble/peer_manager/gatts_cache_manager.c \
  $(SDK_ROOT)/components/ble/peer_manager/id_manager.c \
  $(SDK_ROOT)/components/ble/peer_manager/nrf_ble_lesc.c \
  $(SDK_ROOT)/components/ble/peer_manager/peer_data_storage.c \
  $(SDK_ROOT)/components/ble/peer_manager/peer_database.c \
  $(SDK_ROOT)/components/ble/peer_manager/peer_id.c \
  $(SDK_ROOT)/components/ble/peer_manager/peer_manager.c \
  $(SDK_ROOT)/components/ble/peer_manager/pm_buffer.c \
  $(SDK_ROOT)/components/ble/peer_manager/security_dispatcher.c \
  $(SDK_ROOT)/components/ble/peer_manager/security_manager.c \
  $(SDK_ROOT)/components/ble/ble_services/ble_bas/ble_bas.c \
  $(SDK_ROOT)/components/ble/ble_services/ble_dis/ble_dis.c \
  $(SDK_ROOT)/components/ble/ble_services/ble_hids/ble_hids.c \
  $(SDK_ROOT)/components/softdevice/common/nrf_sdh.c \
  $(SDK_ROOT)/components/softdevice/common/nrf_sdh_ble.c \
  $(SDK_ROOT)/components/softdevice/common/nrf_sdh_soc.c \
  $(SDK_ROOT)/components/libraries/crypto/nrf_crypto_init.c \
  $(SDK_ROOT)/components/libraries/crypto/nrf_crypto_ecc.c \
  $(SDK_ROOT)/components/libraries/crypto/nrf_crypto_ecdh.c \
  $(SDK_ROOT)/components/libraries/crypto/nrf_crypto_rng.c \
  $(SDK_ROOT)/components/libraries/crypto/nrf_crypto_shared.c \
  $(SDK_ROOT)/components/libraries/crypto/nrf_crypto_error.c \
  $(SDK_ROOT)/components/libraries/crypto/backend/oberon/oberon_backend_ecc.c \
  $(SDK_ROOT)/components/libraries/crypto/backend/oberon/oberon_backend_ecdh.c \
  $(SDK_ROOT)/components/libraries/crypto/backend/nrf_hw/nrf_hw_backend_init.c \
  $(SDK_ROOT)/components/libraries/crypto/backend/nrf_hw/nrf_hw_backend_rng.c \

INC_FOLDERS += \
  config \
  ../common \
  $(SDK_ROOT)/examples/ble_peripheral/ble_app_hids_keyboard/pca10040/s132/config \
  $(SDK_ROOT)/components \
  $(SDK_ROOT)/components/toolchain/cmsis/include \
  $(SDK_ROOT)/components/softdevice/common \
  $(SDK_ROOT)/components/softdevice/s132/headers \
  $(SDK_ROOT)/components/softdevice/s132/headers/nrf52 \
  $(SDK_ROOT)/components/ble/common \
  $(SDK_ROOT)/components/ble/ble_advertising \
  $(SDK_ROOT)/components/ble/ble_link_ctx_manager \
  $(SDK_ROOT)/components/ble/nrf_ble_gatt \
  $(SDK_ROOT)/components/ble/nrf_ble_qwr \
  $(SDK_ROOT)/components/ble/peer_manager \
  $(SDK_ROOT)/components/ble/ble_services/ble_bas \
  $(SDK_ROOT)/components/ble/ble_services/ble_dis \
  $(SDK_ROOT)/components/ble/ble_services/ble_hids \
  $(SDK_ROOT)/components/libraries/atomic \
  $(SDK_ROOT)/components/libraries/atomic_fifo \
  $(SDK_ROOT)/components/libraries/atomic_flags \
  $(SDK_ROOT)/components/libraries/balloc \
  $(SDK_ROOT)/components/libraries/crc16 \
  $(SDK_ROOT)/components/libraries/crypto \
  $(SDK_ROOT)/components/libraries/crypto/backend/cc310 \
  $(SDK_ROOT)/components/libraries/crypto/backend/cc310_bl \
  $(SDK_ROOT)/components/libraries/crypto/backend/cifra \
  $(SDK_ROOT)/components/libraries/crypto/backend/mbedtls \
  $(SDK_ROOT)/components/libraries/crypto/backend/micro_ecc \
  $(SDK_ROOT)/components/libraries/crypto/backend/nrf_hw \
  $(SDK_ROOT)/components/libraries/crypto/backend/nrf_sw \
  $(SDK_ROOT)/components/libraries/crypto/backend/oberon \
  $(SDK_ROOT)/components/libraries/crypto/backend/optiga \
  $(SDK_ROOT)/components/libraries/delay \
  $(SDK_ROOT)/components/libraries/experimental_section_vars \
  $(SDK_ROOT)/components/libraries/fds \
  $(SDK_ROOT)/components/libraries/fstorage \
  $(SDK_ROOT)/components/libraries/hardfault \
  $(SDK_ROOT)/components/libraries/log \
  $(SDK_ROOT)/components/libraries/log/src \
  $(SDK_ROOT)/components/libraries/memobj \
  $(SDK_ROOT)/components/libraries/mutex \
  $(SDK_ROOT)/components/libraries/pwr_mgmt \
  $(SDK_ROOT)/components/libraries/queue \
  $(SDK_ROOT)/components/libraries/ringbuf \
  $(SDK_ROOT)/components/libraries/scheduler \
  $(SDK_ROOT)/components/libraries/sortlist \
  $(SDK_ROOT)/components/libraries/stack_info \
  $(SDK_ROOT)/components/libraries/strerror \
  $(SDK_ROOT)/components/libraries/svc \
  $(SDK_ROOT)/components/libraries/timer \
  $(SDK_ROOT)/components/libraries/util \
  $(SDK_ROOT)/external/fprintf \
  $(SDK_ROOT)/external/nrf_oberon \
  $(SDK_ROOT)/external/nrf_oberon/include \
  $(SDK_ROOT)/external/segger_rtt \
  $(SDK_ROOT)/integration/nrfx \
  $(SDK_ROOT)/integration/nrfx/legacy \
  $(SDK_ROOT)/modules/nrfx \
  $(SDK_ROOT)/modules/nrfx/drivers/include \
  $(SDK_ROOT)/modules/nrfx/hal \
  $(SDK_ROOT)/modules/nrfx/mdk \

LIB_FILES += \
  $(SDK_ROOT)/external/nrf_oberon/lib/cortex-m4/hard-float/liboberon_3.0.8.a \

# -Os: the app must fit the 64 KB slot of the update container.
OPT = -Os -g3
# No RTT log by default (smaller, less idle current); NRF_LOG=1 adds it for SWD debugging (make clean first).
ifneq ($(NRF_LOG),1)
OPT += -DNRF_LOG_ENABLED=0
endif

COMMON_DEFS = -DAPP_TIMER_V2 -DAPP_TIMER_V2_RTC1_ENABLED -DCONFIG_GPIO_AS_PINRESET -DFLOAT_ABI_HARD \
  -DNRF52 -DNRF52832_XXAA -DNRF52_PAN_74 -DNRF_SD_BLE_API_VERSION=7 -DS132 -DSOFTDEVICE_PRESENT
CPU = -mcpu=cortex-m4 -mthumb -mabi=aapcs -mfloat-abi=hard -mfpu=fpv4-sp-d16

CFLAGS += $(OPT) $(COMMON_DEFS) $(CPU) -DUSE_APP_CONFIG
CFLAGS += -Wall -Werror
# keep every function in a separate section, this allows linker to discard unused ones
CFLAGS += -ffunction-sections -fdata-sections -fno-strict-aliasing
CFLAGS += -fno-builtin -fshort-enums

CXXFLAGS += $(OPT)

ASMFLAGS += -g3 $(COMMON_DEFS) $(CPU)
# Enter main() straight from the startup file (which then clears .bss itself) instead of
# newlib's crt0, whose exit path drags in stdio and its unimplemented syscall stubs.
ASMFLAGS += -D__START=main -D__STARTUP_CLEAR_BSS

LDFLAGS += $(OPT) $(CPU) -L$(SDK_ROOT)/modules/nrfx/mdk -T$(LINKER_SCRIPT)
LDFLAGS += -Wl,--gc-sections --specs=nano.specs -nostartfiles

nrf52832_xxaa: CFLAGS += -D__HEAP_SIZE=8192 -D__STACK_SIZE=8192
nrf52832_xxaa: ASMFLAGS += -D__HEAP_SIZE=8192 -D__STACK_SIZE=8192

# Standard libraries last, after all objects that may need their symbols.
LIB_FILES += -lc -lnosys -lm

.PHONY: default help

default: nrf52832_xxaa

help:
	@echo following targets are available:
	@echo		nrf52832_xxaa
	@echo		flash      - flash the app over SWD with pyOCD (sector erase only)

# GCC 15 false positives in unmodified SDK sources: zero-length arrays that the linker fills from
# section variables (and, in fds.c, the read of the MBR's bootloader-address word at 0xFF8). Vendor code, so silenced per file.
$(OUTPUT_DIRECTORY)/nrf52832_xxaa/nrf_fstorage.c.o: CFLAGS += -Wno-array-bounds
$(OUTPUT_DIRECTORY)/nrf52832_xxaa/fds.c.o: CFLAGS += -Wno-array-bounds
$(OUTPUT_DIRECTORY)/nrf52832_xxaa/nrf_crypto_init.c.o: CFLAGS += -Wno-array-bounds
$(OUTPUT_DIRECTORY)/nrf52832_xxaa/nrf_log_frontend.c.o: CFLAGS += -Wno-array-bounds

TEMPLATE_PATH := $(SDK_ROOT)/components/toolchain/gcc

include $(TEMPLATE_PATH)/Makefile.common

$(foreach target, $(TARGETS), $(call define_target, $(target)))

.PHONY: flash

# Only the app's sectors: the MBR, SoftDevice and PFU's bootloader must survive (no auto-unlock: it mass-erases).
flash: $(OUTPUT_DIRECTORY)/nrf52832_xxaa.hex
	pyocd load -t nrf52832 -O auto_unlock=false -e sector $<
	pyocd reset -t nrf52832 -O auto_unlock=false
