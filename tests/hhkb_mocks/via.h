#ifndef MOCK_VIA_H
#define MOCK_VIA_H

#include <stdint.h>
#include <stddef.h>

#define DYNAMIC_KEYMAP_MAX_LAYERS 4
#define NUM_KEYS 60
#define DYNAMIC_KEYMAP_MACRO_BUFFER_SIZE 512

typedef struct {
    uint32_t keymap[DYNAMIC_KEYMAP_MAX_LAYERS][NUM_KEYS];
    uint8_t macro_buffer[DYNAMIC_KEYMAP_MACRO_BUFFER_SIZE];
} via_storage_t;

extern via_storage_t via_storage;
#define dynamic_keymap (via_storage.keymap) // as in stm32/via/via.h
#define dynamic_macro_buffer (via_storage.macro_buffer)

uint16_t to_via_keycode(uint32_t key);
uint32_t from_via_keycode(uint16_t via_key);
void via_keymap_load(void);
void via_keymap_save(void);
void via_keymap_poll(void);

#endif
