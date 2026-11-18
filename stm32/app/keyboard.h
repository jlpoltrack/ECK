#ifndef KEYBOARD_H
#define KEYBOARD_H

#include <stdbool.h>
#include <stdint.h>

#include "FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize layers, hold-tap, one-shot, the VIA keymap and the board indicators.
 */
void keyboard_init(void);

/**
 * @brief Main keyboard task loop executed by FreeRTOS scheduler.
 * @param pvParameters FreeRTOS task parameters pointer (unused)
 */
void keyboard_task(void *pvParameters);

/**
 * @brief Queue standard key press or release event to USB report task.
 * @param keycode 16-bit USB HID keycode
 * @param pressed True on press, false on release
 */
void keyboard_send_key(uint16_t keycode, bool pressed);

/**
 * @brief Queue a modifier bitmask update to the USB report task.
 * @param mod_mask 8-bit modifier mask
 * @param pressed True on press, false on release
 */
void keyboard_send_modifiers(uint8_t mod_mask, bool pressed);

/**
 * @brief Record a resolved keycode for a pressed physical switch so release uses the same keycode even if layers change.
 * @param row Matrix row index
 * @param col Matrix col index
 * @param key Resolved 32-bit keycode
 */
void keyboard_record_pressed_key(uint8_t row, uint8_t col, uint32_t key);

/**
 * @brief Reset MCU and jump to platform bootloader.
 */
void bootloader_jump(void);

#ifdef VIAL_ENABLE
/* Row for keys the Vial features press themselves (never a matrix row) */
#define KEYBOARD_VIRTUAL_ROW 0xFE
void keyboard_handle_press(uint8_t row, uint8_t col);
/* A key pressed by a feature, with a real press's steps; returns the key to release later */
uint32_t keyboard_virtual_press(uint8_t slot, uint32_t key);
void keyboard_virtual_release(uint8_t slot, uint32_t key);
/* Just the key action (macro playback) */
void keyboard_process_virtual(uint8_t slot, uint32_t key, bool pressed);
#endif

/* Power Saving (dip_switch.c): true blocks USB remote wakeup on a key press. */
bool keyboard_power_saving_enabled(void);

/* Board indicators and system layer (indicators_system.c), run by the keyboard task. */
void keyboard_platform_init(void);
void keyboard_platform_tick(TickType_t now);
TickType_t keyboard_platform_timeout(void);

#ifdef __cplusplus
}
#endif

#endif // KEYBOARD_H
