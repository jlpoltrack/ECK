cmake_minimum_required(VERSION 3.20)
# ECK firmware for the STM32L072. Build: ./build.sh hybrid|classic|type_s|hybrid_jis|type_s_jis|r3s (or cmake -DHHKB_VARIANT=...)

get_filename_component(STM32_ROOT "${CMAKE_CURRENT_LIST_DIR}" ABSOLUTE)
get_filename_component(REPO_ROOT "${STM32_ROOT}/.." ABSOLUTE)
set(VENDOR_DIR "${STM32_ROOT}/vendor")
set(ST_DIR "${VENDOR_DIR}/st")
# FreeRTOS Kernel V11.3.3 (44031977); its GCC/ARM_CM0 port is in board/freertos_port.
set(FREERTOS_DIR "${VENDOR_DIR}/freertos")
# TinyUSB 0.21.0 (dae3f9a36), only the files this build uses.
set(TINYUSB_DIR "${VENDOR_DIR}/tinyusb")
# compile_commands.json for clangd and IDEs; build.sh links the latest one at build/.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Variant preset (build.sh passes it) and optional bench options, comma-separated.
set(HHKB_VARIANT "hybrid" CACHE STRING "Keyboard: hybrid, classic, type_s, hybrid_jis, type_s_jis or r3s")
set_property(CACHE HHKB_VARIANT PROPERTY STRINGS hybrid classic type_s hybrid_jis type_s_jis r3s)
set(HHKB_EXTRA_DEFINES "" CACHE STRING "Bench options, e.g. HHKB_ADC_DIAGNOSTICS,HHKB_SCAN_DIAGNOSTICS")
if(NOT HHKB_VARIANT MATCHES "^(hybrid|classic|type_s|hybrid_jis|type_s_jis|r3s)$")
    message(FATAL_ERROR "HHKB_VARIANT must be hybrid, classic, type_s, hybrid_jis, type_s_jis or r3s (got '${HHKB_VARIANT}')")
endif()
# Board: matrix, layout, EEPROM map and board-only modules (stm32/boards/<board>/).
# The HHKB's sources keep their order in the list below: it fixes the link order (and so its binaries).
if(HHKB_VARIANT STREQUAL "r3s")
    set(ECK_BOARD "r3s")
else()
    set(ECK_BOARD "hhkb")
endif()
set(BOARD_DIR "${STM32_ROOT}/boards/${ECK_BOARD}")
if(HHKB_VARIANT MATCHES "_jis$")
    add_compile_definitions(HHKB_JIS) # 69-key JIS matrix and layout (boards/hhkb/board_config.h)
endif()
if(ECK_BOARD STREQUAL "hhkb")
    set(BOARD_SOURCES_DIP "${BOARD_DIR}/dip_modes.c" "${BOARD_DIR}/dip_switch.c")
    set(BOARD_SOURCES_SCAN "${BOARD_DIR}/classic_matrix.c")
    set(BOARD_SOURCES_HYBRID
        "${STM32_ROOT}/hybrid/hybrid.c"
        "${STM32_ROOT}/hybrid/hybrid_ble.c"
        "${STM32_ROOT}/hybrid/hybrid_chords.c"
        "${STM32_ROOT}/hybrid/hybrid_power.c"
        "${STM32_ROOT}/hybrid/hybrid_power_logic.c"
        "${STM32_ROOT}/hybrid/hybrid_sleep.c")
else()
    add_compile_definitions(ECK_BOARD_R3S)
    set(BOARD_SOURCES_DIP "")
    set(BOARD_SOURCES_SCAN "${BOARD_DIR}/r3s_matrix.c" "${BOARD_DIR}/r3s_leds.c")
    set(BOARD_SOURCES_HYBRID "")
endif()
string(REPLACE "," ";" _EXTRA_LIST "${HHKB_EXTRA_DEFINES}")
option(ECK_VIAL "Build with Vial (defines VIAL_ENABLE)" ON)
if(ECK_VIAL AND NOT "VIAL_ENABLE" IN_LIST _EXTRA_LIST)
    list(APPEND _EXTRA_LIST VIAL_ENABLE)
endif()
foreach(DEF ${_EXTRA_LIST})
    string(STRIP "${DEF}" STRIPPED_DEF)
    if(NOT STRIPPED_DEF STREQUAL "")
        add_compile_definitions("${STRIPPED_DEF}")
    endif()
endforeach()

# Determine firmware version (CalVer tag or YY.MM.DD-hash)
if(DEFINED ENV{HHKB_FW_VERSION})
    set(FW_VERSION "$ENV{HHKB_FW_VERSION}")
elseif(DEFINED ENV{GITHUB_REF_TYPE} AND "$ENV{GITHUB_REF_TYPE}" STREQUAL "tag")
    set(FW_VERSION "$ENV{GITHUB_REF_NAME}")
else()
    execute_process(
        COMMAND git describe --tags --exact-match
        WORKING_DIRECTORY "${REPO_ROOT}"
        OUTPUT_VARIABLE _GIT_TAG
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    if(_GIT_TAG)
        set(FW_VERSION "${_GIT_TAG}")
    else()
        execute_process(
            COMMAND git log -1 --format=%cd --date=format:%y.%m.%d
            WORKING_DIRECTORY "${REPO_ROOT}"
            OUTPUT_VARIABLE _GIT_DATE
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
        )
        execute_process(
            COMMAND git rev-parse --short=7 HEAD
            WORKING_DIRECTORY "${REPO_ROOT}"
            OUTPUT_VARIABLE _GIT_HASH
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
        )
        if(_GIT_DATE AND _GIT_HASH)
            set(FW_VERSION "${_GIT_DATE}-${_GIT_HASH}")
        elseif(DEFINED ENV{GITHUB_SHA})
            string(SUBSTRING "$ENV{GITHUB_SHA}" 0 7 _CI_HASH)
            string(TIMESTAMP _CURRENT_DATE "%y.%m.%d")
            set(FW_VERSION "${_CURRENT_DATE}-${_CI_HASH}")
        else()
            string(TIMESTAMP _FALLBACK_DATE "%y.%m.%d")
            set(FW_VERSION "${_FALLBACK_DATE}-dev")
        endif()
    endif()
endif()

message(STATUS "Firmware version: ${FW_VERSION}")
add_compile_definitions(FW_VERSION="${FW_VERSION}")
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "MinSizeRel" CACHE STRING "Choose the type of build" FORCE)
endif()

add_compile_options(-Os -ffunction-sections -fdata-sections)

# Toolchain: STM32L072 (ARM Cortex-M0+)
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_VERSION 1)
set(CMAKE_SYSTEM_PROCESSOR arm)

find_program(ARM_GCC_SYSTEM NAMES arm-none-eabi-gcc)
if(NOT ARM_GCC_SYSTEM)
    message(FATAL_ERROR "ARM GCC toolchain (arm-none-eabi-gcc) not found in PATH.")
endif()

get_filename_component(TOOLCHAIN_BIN_DIR "${ARM_GCC_SYSTEM}" DIRECTORY)
set(TOOLCHAIN_PREFIX "${TOOLCHAIN_BIN_DIR}/arm-none-eabi-")

set(CMAKE_C_COMPILER   "${TOOLCHAIN_PREFIX}gcc")
set(CMAKE_CXX_COMPILER "${TOOLCHAIN_PREFIX}g++")
set(CMAKE_ASM_COMPILER "${TOOLCHAIN_PREFIX}gcc")
set(CMAKE_AR           "${TOOLCHAIN_PREFIX}ar")
set(CMAKE_OBJCOPY      "${TOOLCHAIN_PREFIX}objcopy")
set(CMAKE_OBJDUMP      "${TOOLCHAIN_PREFIX}objdump")
set(CMAKE_SIZE         "${TOOLCHAIN_PREFIX}size")
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

add_compile_options(
    -mcpu=cortex-m0plus
    -mthumb
    -mfloat-abi=soft
    -Os
    -ffunction-sections
    -fdata-sections
    -fno-common
    -fno-builtin
    -Wall
    -Wextra
)

add_compile_definitions(
    STM32L072xx
    HSE_VALUE=8000000
    HSI_VALUE=16000000
    CFG_TUSB_MCU=OPT_MCU_STM32L0
    USER_VECT_TAB_ADDRESS
    VECT_TAB_OFFSET=0x10000U
)

# Flash layout: Bank 2 starts at 0x08010000 (64 KB)
set(LINKER_SCRIPT "${STM32_ROOT}/board/stm32l072_bank2.ld")

if(HHKB_VARIANT MATCHES "^hybrid")
    set(HHKB_HYBRID ON)
    add_compile_definitions(HHKB_HYBRID TICKLESS_SLEEP_HOOK=hybrid_sleep) # STOP between scans on batteries
else()
    set(HHKB_HYBRID OFF)
endif()

# PFU's Hybrid .hfb is not redistributed; the Hybrid container is built from a local copy.
# Nordic's S132 v7.2.0 SoftDevice, which the Hybrid .hfb carries (PFU's nRF52 bootloader rewrites it).
set(HHKB_HYBRID_SOFTDEVICE "${REPO_ROOT}/../nRF5_SDK_17.1.1/components/softdevice/s132/hex/s132_nrf52_7.2.0_softdevice.hex" CACHE FILEPATH "Nordic S132 v7.2.0 SoftDevice .hex for the Hybrid .hfb")
# The nRF52 Bluetooth app from nrf52/ (built with make). Required:
# with PFU's nRF52 app both chips would drive the link as SPI master.
set(HHKB_HYBRID_NRF_APP "${REPO_ROOT}/nrf52/_build/nrf52832_xxaa.bin" CACHE FILEPATH "nRF52 app placed in the Hybrid .hfb")

project(hhkb_ec ASM C CXX)

set(TARGET_NAME hhkb_ec_${HHKB_VARIANT})
add_executable(${TARGET_NAME})
set_target_properties(${TARGET_NAME} PROPERTIES SUFFIX ".elf")

target_include_directories(${TARGET_NAME} PRIVATE
    "${BOARD_DIR}"
    "${STM32_ROOT}/board"
    "${STM32_ROOT}/usb"
    "${ST_DIR}/cmsis_core/Include"
    "${ST_DIR}/cmsis_device_l0/Include"
    "${ST_DIR}/stm32l0xx_hal_driver/Inc"
    "${FREERTOS_DIR}/include"
    "${STM32_ROOT}/board/freertos_port"
    "${TINYUSB_DIR}/src"
    "${STM32_ROOT}/app"
    "${STM32_ROOT}/keys"
    "${STM32_ROOT}/via"
    "${STM32_ROOT}/ec"
    "${STM32_ROOT}/hybrid"
    "${REPO_ROOT}/common"
)

target_sources(${TARGET_NAME} PRIVATE
    # Core orchestration, key features, drivers
    "${STM32_ROOT}/app/main.c"
    "${STM32_ROOT}/app/keyboard.c"
    "${BOARD_DIR}/events.c"
    "${STM32_ROOT}/app/memory.c"
    "${STM32_ROOT}/app/fmt.c"
    "${STM32_ROOT}/keys/layers.c"
    "${STM32_ROOT}/usb/usb.c"
    "${STM32_ROOT}/keys/hold_tap.c"
    "${STM32_ROOT}/keys/oneshot.c"
    "${STM32_ROOT}/keys/macros.c"
    # FreeRTOS kernel and port
    "${FREERTOS_DIR}/list.c"
    "${FREERTOS_DIR}/queue.c"
    "${FREERTOS_DIR}/tasks.c"
    "${STM32_ROOT}/board/freertos_port/port.c"
    "${STM32_ROOT}/board/freertos_port/portasm.c"
    # Board support and start-up (STM32L0 Low-Layer drivers are header-only here)
    "${STM32_ROOT}/board/platform.c"
    "${STM32_ROOT}/board/eeprom.c"
    "${STM32_ROOT}/board/syscalls.c"
    "${ST_DIR}/cmsis_device_l0/Source/Templates/gcc/startup_stm32l072xx.s"
    "${ST_DIR}/cmsis_device_l0/Source/Templates/system_stm32l0xx.c"
    # TinyUSB stack
    "${STM32_ROOT}/usb/app_usb_tinyusb.c"
    "${STM32_ROOT}/usb/usb_descriptors.c"
    "${TINYUSB_DIR}/src/tusb.c"
    "${TINYUSB_DIR}/src/common/tusb_fifo.c"
    "${TINYUSB_DIR}/src/device/usbd.c"
    "${TINYUSB_DIR}/src/class/hid/hid_device.c"
    "${TINYUSB_DIR}/src/portable/st/stm32_fsdev/dcd_stm32_fsdev.c"
    "${TINYUSB_DIR}/src/portable/st/stm32_fsdev/fsdev_common.c"
    # VIA protocol
    "${STM32_ROOT}/via/via.c"
    # HHKB modules: DIP switches, EC matrix, Hybrid link, indicators, VIA settings
    ${BOARD_SOURCES_DIP}
    ${BOARD_SOURCES_SCAN}
    "${STM32_ROOT}/ec/ec_settings.c"
    "${STM32_ROOT}/ec/matrix.c"
    "${STM32_ROOT}/ec/rapid_trigger.c"
    ${BOARD_SOURCES_HYBRID}
    "${STM32_ROOT}/board/indicators_system.c"
    "${STM32_ROOT}/via/hhkb_keymap_storage.c"
    "${STM32_ROOT}/via/hhkb_via_controls.c"
)

# Vial (ECK_VIAL, on by default): definition built from the board's VIA definition
if(ECK_BOARD STREQUAL "r3s")
    set(VIA_JSON "${BOARD_DIR}/via.json")
elseif(HHKB_VARIANT MATCHES "_jis$")
    set(VIA_JSON "${STM32_ROOT}/via/via_jis.json")
else()
    set(VIA_JSON "${STM32_ROOT}/via/via.json")
endif()
if("VIAL_ENABLE" IN_LIST _EXTRA_LIST)
    add_custom_command(
        OUTPUT ${CMAKE_BINARY_DIR}/vial_definition.h
        COMMAND python3 ${REPO_ROOT}/tools/vial_def.py ${VIA_JSON} ${CMAKE_BINARY_DIR}/vial_definition.h
        DEPENDS ${VIA_JSON} ${REPO_ROOT}/tools/vial_def.py
    )
    target_sources(${TARGET_NAME} PRIVATE
        "${STM32_ROOT}/via/vial.c"
        "${STM32_ROOT}/keys/vial_features.c"
        ${CMAKE_BINARY_DIR}/vial_definition.h
    )
    target_include_directories(${TARGET_NAME} PRIVATE ${CMAKE_BINARY_DIR})
endif()

target_link_libraries(${TARGET_NAME} PRIVATE
    -mcpu=cortex-m0plus
    -mthumb
    -mfloat-abi=soft
    --specs=nosys.specs
    --specs=nano.specs
    -T${LINKER_SCRIPT}
)
target_link_options(${TARGET_NAME} PRIVATE -Wl,--gc-sections)

# Post-build packaging into .bin and HHKB .hfb container
if(HHKB_VARIANT STREQUAL "hybrid_jis")
    set(HFB_HYBRID_MODEL_ARG "--model" "AHJX01" "--version" "A2.49") # PFU's Hybrid JIS image (HHKB820_FW_JP_A249)
else()
    set(HFB_HYBRID_MODEL_ARG "")
endif()
if(HHKB_HYBRID)
    if(NOT EXISTS "${HHKB_HYBRID_NRF_APP}" OR NOT EXISTS "${HHKB_HYBRID_SOFTDEVICE}")
        # Without the nRF52 app and SoftDevice the STM32 .bin alone still updates the firmware (hybrid_update.py stm32).
        message(STATUS "No nRF52 app (${HHKB_HYBRID_NRF_APP}) or SoftDevice (${HHKB_HYBRID_SOFTDEVICE}): building the STM32 .bin only")
        add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
            COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${TARGET_NAME}> ${CMAKE_BINARY_DIR}/${TARGET_NAME}.bin
            COMMAND ${CMAKE_SIZE} $<TARGET_FILE:${TARGET_NAME}>
            COMMENT "Generating the HHKB Hybrid STM32 binary for ${TARGET_NAME}"
        )
    else()
        # Relink (and so repackage) when only the nRF52 app changed.
        set_property(TARGET ${TARGET_NAME} APPEND PROPERTY LINK_DEPENDS ${HHKB_HYBRID_NRF_APP})
        add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
            COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${TARGET_NAME}> ${CMAKE_BINARY_DIR}/${TARGET_NAME}.bin
            COMMAND python3 ${REPO_ROOT}/tools/make_hfb.py ${CMAKE_BINARY_DIR}/${TARGET_NAME}.bin ${CMAKE_BINARY_DIR}/${TARGET_NAME}.hfb --nrf-app ${HHKB_HYBRID_NRF_APP} --softdevice ${HHKB_HYBRID_SOFTDEVICE} ${HFB_HYBRID_MODEL_ARG}
            COMMAND ${CMAKE_SIZE} $<TARGET_FILE:${TARGET_NAME}>
            COMMENT "Generating binary and HHKB Hybrid .hfb for ${TARGET_NAME}"
        )
    endif()
elseif(ECK_BOARD STREQUAL "r3s")
    add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
        COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${TARGET_NAME}> ${CMAKE_BINARY_DIR}/${TARGET_NAME}.bin
        COMMAND python3 ${REPO_ROOT}/tools/make_hfb.py --rfb ${CMAKE_BINARY_DIR}/${TARGET_NAME}.bin ${CMAKE_BINARY_DIR}/${TARGET_NAME}.rfb
        COMMAND ${CMAKE_SIZE} $<TARGET_FILE:${TARGET_NAME}>
        COMMENT "Generating binary and Realforce R3S .rfb for ${TARGET_NAME}"
    )
else()
    if(HHKB_VARIANT STREQUAL "type_s")
        set(HFB_MODEL_ARG "--model" "AHHS01" "--version" "A8.50")
        set(MODEL_COMMENT "Type-S (AHHS01)")
    elseif(HHKB_VARIANT STREQUAL "type_s_jis")
        set(HFB_MODEL_ARG "--model" "AHKS01" "--version" "A9.50") # PFU's JIS Type-S image: AHKS01 A9.04
        set(MODEL_COMMENT "Type-S JIS (AHKS01)")
    else()
        set(HFB_MODEL_ARG "--model" "AHHX01" "--version" "A4.50")
        set(MODEL_COMMENT "Classic (AHHX01)")
    endif()

    add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
        COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${TARGET_NAME}> ${CMAKE_BINARY_DIR}/${TARGET_NAME}.bin
        COMMAND python3 ${REPO_ROOT}/tools/make_hfb.py ${CMAKE_BINARY_DIR}/${TARGET_NAME}.bin ${CMAKE_BINARY_DIR}/${TARGET_NAME}.hfb ${HFB_MODEL_ARG}
        COMMAND ${CMAKE_SIZE} $<TARGET_FILE:${TARGET_NAME}>
        COMMENT "Generating binary and HHKB ${MODEL_COMMENT} .hfb update package for ${TARGET_NAME}"
    )

    # Custom target to flash directly via USB HID: cmake --build <dir> --target flash
    add_custom_target(flash
        COMMAND uv run --with hidapi python3 ${REPO_ROOT}/tools/hhkb_flasher.py ${CMAKE_BINARY_DIR}/${TARGET_NAME}.bin
        DEPENDS ${TARGET_NAME}
        COMMENT "Flashing ${TARGET_NAME}.bin to HHKB via USB HID..."
    )
endif()
