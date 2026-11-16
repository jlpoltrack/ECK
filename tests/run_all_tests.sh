#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BOLD='\033[1m'
NC='\033[0m'

echo -e "${BOLD}====================================================${NC}"
echo -e "${BOLD}              HHKB firmware test suite          ${NC}"
echo -e "${BOLD}====================================================${NC}\n"

# 1. C Core Unit Tests
echo -e "${BOLD}[1/2] Running Host-Based C Core Unit Tests...${NC}"
clang -Wall -Wextra -DMATRIX_REPORT_BATCH \
    -I "${SCRIPT_DIR}/tests" \
    -I "${SCRIPT_DIR}/tests/mocks" \
    -I "${SCRIPT_DIR}/stm32/app" \
    -I "${SCRIPT_DIR}/stm32/keys" \
    -I "${SCRIPT_DIR}/stm32/via" \
    -I "${SCRIPT_DIR}/stm32/board" \
    -I "${SCRIPT_DIR}/stm32/usb" \
    "${SCRIPT_DIR}/tests/test_core.c" -o "${SCRIPT_DIR}/tests/test_core"
"${SCRIPT_DIR}/tests/test_core"
rm -f "${SCRIPT_DIR}/tests/test_core"
clang -Wall -Wextra -Werror -I "${SCRIPT_DIR}/stm32/app" \
    "${SCRIPT_DIR}/tests/test_fmt.c" "${SCRIPT_DIR}/stm32/app/fmt.c" -o "${SCRIPT_DIR}/tests/test_fmt"
"${SCRIPT_DIR}/tests/test_fmt"
rm -f "${SCRIPT_DIR}/tests/test_fmt"
clang -Wall -Wextra -I "${SCRIPT_DIR}/tests/mocks" \
    "${SCRIPT_DIR}/tests/test_memory_hooks.c" "${SCRIPT_DIR}/stm32/app/memory.c" \
    -o "${SCRIPT_DIR}/tests/test_memory_hooks"
"${SCRIPT_DIR}/tests/test_memory_hooks"
rm -f "${SCRIPT_DIR}/tests/test_memory_hooks"
for heap_size in 3072 8192; do
    clang -Wall -Wextra -DconfigTOTAL_HEAP_SIZE="$heap_size" -I "${SCRIPT_DIR}/tests/heap_mocks" \
        "${SCRIPT_DIR}/tests/test_heap4_failure.c" "${SCRIPT_DIR}/stm32/app/memory.c" \
        "${SCRIPT_DIR}/stm32/vendor/freertos/heap_4.c" -o "${SCRIPT_DIR}/tests/test_heap4_failure"
    "${SCRIPT_DIR}/tests/test_heap4_failure"
done
rm -f "${SCRIPT_DIR}/tests/test_heap4_failure"
clang -Wall -Wextra -I "${SCRIPT_DIR}/tests/mocks" -I "${SCRIPT_DIR}/stm32/app" \
    -I "${SCRIPT_DIR}/stm32/board" -I "${SCRIPT_DIR}/stm32/usb" -I "${SCRIPT_DIR}/stm32/keys" \
    -I "${SCRIPT_DIR}/stm32/via" "${SCRIPT_DIR}/tests/test_features.c" "${SCRIPT_DIR}/stm32/keys/oneshot.c" \
    "${SCRIPT_DIR}/stm32/keys/macros.c" -o "${SCRIPT_DIR}/tests/test_features"
"${SCRIPT_DIR}/tests/test_features"
rm -f "${SCRIPT_DIR}/tests/test_features"

clang -Wall -Wextra -Wno-unused-variable -DconfigTICK_RATE_HZ=1000 \
    -I "${SCRIPT_DIR}/stm32/board" -I "${SCRIPT_DIR}/tests/mocks" -I "${SCRIPT_DIR}/stm32/app" \
    -I "${SCRIPT_DIR}/stm32/keys" -I "${SCRIPT_DIR}/stm32/usb" \
    "${SCRIPT_DIR}/tests/test_via_keycodes.c" "${SCRIPT_DIR}/stm32/via/via.c" \
    -o "${SCRIPT_DIR}/tests/test_via_keycodes"
"${SCRIPT_DIR}/tests/test_via_keycodes"
rm -f "${SCRIPT_DIR}/tests/test_via_keycodes"

# USB report routing (app_usb_tinyusb.c with FreeRTOS/TinyUSB mocks)
clang -std=c11 -Wall -Wextra -Werror -I "${SCRIPT_DIR}/tests/usb_mocks" -I "${SCRIPT_DIR}/stm32/usb" \
    -I "${SCRIPT_DIR}/stm32/app" -I "${SCRIPT_DIR}/stm32/board" \
    "${SCRIPT_DIR}/tests/test_usb_routing.c" "${SCRIPT_DIR}/stm32/usb/app_usb_tinyusb.c" \
    -o "${SCRIPT_DIR}/tests/test_usb_routing"
"${SCRIPT_DIR}/tests/test_usb_routing"
rm -f "${SCRIPT_DIR}/tests/test_usb_routing"

# HHKB host-only algorithm and EEPROM guard tests
HHKB_TEST_DIR="${SCRIPT_DIR}/tests"
HHKB_MOCKS_DIR="${SCRIPT_DIR}/tests/hhkb_mocks"
STM32_DIR="${SCRIPT_DIR}/stm32"
# Tests #include module .c files: copied alone so their quoted includes find the mocks, not real headers.
MODULE_SRC_DIR="$(mktemp -d)"
trap 'rm -rf "$MODULE_SRC_DIR"' EXIT
cp "${STM32_DIR}/board/dip_modes.c" "${STM32_DIR}/board/indicators_system.c" \
    "${STM32_DIR}/via/hhkb_keymap_storage.c" "${STM32_DIR}/hybrid/hybrid_power_logic.c" "$MODULE_SRC_DIR"
clang -std=c11 -Wall -Wextra -Werror -O2 -I "${STM32_DIR}/ec" \
    "${STM32_DIR}/ec/rapid_trigger.c" "${HHKB_TEST_DIR}/test_rapid_trigger.c" \
    -o "${HHKB_TEST_DIR}/test_rapid_trigger"
"${HHKB_TEST_DIR}/test_rapid_trigger"
rm -f "${HHKB_TEST_DIR}/test_rapid_trigger"
clang -std=c11 -Wall -Wextra -Werror -I "${STM32_DIR}/via" \
    "${HHKB_TEST_DIR}/test_settings_storage_guard.c" -o "${HHKB_TEST_DIR}/test_settings_storage_guard"
"${HHKB_TEST_DIR}/test_settings_storage_guard"
rm -f "${HHKB_TEST_DIR}/test_settings_storage_guard"
clang -std=c11 -Wall -Wextra -Werror -I "${HHKB_MOCKS_DIR}" -I "$MODULE_SRC_DIR" -I "${STM32_DIR}/board" \
    "${HHKB_TEST_DIR}/test_dip_modes.c" -o "${HHKB_TEST_DIR}/test_dip_modes"
"${HHKB_TEST_DIR}/test_dip_modes"
rm -f "${HHKB_TEST_DIR}/test_dip_modes"
clang -std=c11 -Wall -Wextra -Werror -I "${STM32_DIR}/board" -I "${STM32_DIR}/keys" -I "${STM32_DIR}/hybrid" \
    "${HHKB_TEST_DIR}/test_hybrid_chords.c" "${STM32_DIR}/hybrid/hybrid_chords.c" -o "${HHKB_TEST_DIR}/test_hybrid_chords"
"${HHKB_TEST_DIR}/test_hybrid_chords"
rm -f "${HHKB_TEST_DIR}/test_hybrid_chords"
for hhkb_test in test_keymap_storage test_indicators_system test_hybrid_power; do
    clang -std=c11 -Wall -Wextra -Werror -I "${HHKB_MOCKS_DIR}" -I "$MODULE_SRC_DIR" -I "${STM32_DIR}/hybrid" \
        -DFIXTURE_DIR="\"${HHKB_TEST_DIR}/fixtures\"" "${HHKB_TEST_DIR}/${hhkb_test}.c" -o "${HHKB_TEST_DIR}/${hhkb_test}"
    "${HHKB_TEST_DIR}/${hhkb_test}"
    rm -f "${HHKB_TEST_DIR}/${hhkb_test}"
done

# 2. Python Tool Tests
echo -e "${BOLD}[2/2] Running Python Tool Unit Tests...${NC}"
uv run python3 -m unittest discover -s "${SCRIPT_DIR}/tests" -p "test_*.py"

echo -e "\n${GREEN}${BOLD}🎉 All test suites completed successfully!${NC}\n"
