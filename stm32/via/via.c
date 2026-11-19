#include "via.h"
#include "FreeRTOS.h"
#include "keyboard.h"
#include "keys.h"
#include "matrix.h"
#include "task.h"

#include "usb.h"
#include <string.h>
#ifdef VIAL_ENABLE
#include "vial.h"
#include "events.h"
#include "tusb.h"

// Vial keycodes ECK keeps as-is internally: tap dance, Grave Escape, combo/override toggles,
// Caps Word, repeat keys, Layer Lock
static bool is_vial_keycode(uint32_t k) {
    return (k >= K_TD(0) && k < K_TD(VIAL_TAP_DANCE_ENTRIES)) || k == K_GRAVE_ESC ||
           (k >= K_COMBO_ON && k <= K_COMBO_TOGGLE) || (k >= K_KO_TOGGLE && k <= K_KO_OFF) ||
           k == K_CAPS_WORD || (k >= K_REPEAT && k <= K_LAYER_LOCK);
}
#endif

via_storage_t via_storage __attribute__((aligned(4)));

// Default keymap from config.h (compiled in layers.c)
extern const uint32_t keymap[][NUM_KEYS];
extern const size_t keymap_layers;
extern int16_t keyboard_get_flat_key_index(uint8_t row, uint8_t col);

// Enums matching the VIA protocol specification
enum {
    VIA_GET_PROTOCOL_VERSION = 0x01,
    VIA_GET_KEYBOARD_VALUE = 0x02,
    VIA_SET_KEYBOARD_VALUE = 0x03,
    VIA_DYNAMIC_KEYMAP_GET_KEYCODE = 0x04,
    VIA_DYNAMIC_KEYMAP_SET_KEYCODE = 0x05,
    VIA_DYNAMIC_KEYMAP_RESET = 0x06,
    VIA_CUSTOM_SET_VALUE = 0x07,
    VIA_CUSTOM_GET_VALUE = 0x08,
    VIA_CUSTOM_SAVE = 0x09,
    VIA_EEPROM_RESET = 0x0A,
    VIA_BOOTLOADER_JUMP = 0x0B,
    VIA_DYNAMIC_KEYMAP_MACRO_GET_COUNT = 0x0C,
    VIA_DYNAMIC_KEYMAP_MACRO_GET_BUFFER_SIZE = 0x0D,
    VIA_DYNAMIC_KEYMAP_MACRO_GET_BUFFER = 0x0E,
    VIA_DYNAMIC_KEYMAP_MACRO_SET_BUFFER = 0x0F,
    VIA_DYNAMIC_KEYMAP_MACRO_RESET = 0x10,
    VIA_DYNAMIC_KEYMAP_GET_LAYER_COUNT = 0x11,
    VIA_DYNAMIC_KEYMAP_GET_BUFFER = 0x12,
    VIA_DYNAMIC_KEYMAP_SET_BUFFER = 0x13,
};

// Subcommands for GET_KEYBOARD_VALUE
enum {
    VIA_SUB_UPTIME = 0x01,
    VIA_SUB_LAYOUT_OPTIONS = 0x02,
    VIA_SUB_SWITCH_MATRIX_STATE = 0x03,
    VIA_SUB_FIRMWARE_VERSION = 0x04
};

static uint8_t mask_to_via(uint8_t mod);

static uint8_t to_via_mods(uint8_t mod) {
    // A legacy HT() hold byte may be an HID modifier keycode (0xE0..0xE7): one VIA modifier
    switch (mod) {
    case HID_KEY_CONTROL_LEFT: return 0x01;
    case HID_KEY_SHIFT_LEFT:   return 0x02;
    case HID_KEY_ALT_LEFT:     return 0x04;
    case HID_KEY_GUI_LEFT:     return 0x08;
    case HID_KEY_CONTROL_RIGHT: return 0x11;
    case HID_KEY_SHIFT_RIGHT:   return 0x12;
    case HID_KEY_ALT_RIGHT:     return 0x14;
    case HID_KEY_GUI_RIGHT:     return 0x18;
    default: return mask_to_via(mod);
    }
}

static uint8_t mask_to_via(uint8_t mod) {
    uint8_t via_mod = 0;
    if (mod & MOD_LCTRL)
        via_mod |= 0x01;
    if (mod & MOD_LSHIFT)
        via_mod |= 0x02;
    if (mod & MOD_LALT)
        via_mod |= 0x04;
    if (mod & MOD_LGUI)
        via_mod |= 0x08;
    if (mod & MOD_RCTRL)
        via_mod |= 0x11;
    if (mod & MOD_RSHIFT)
        via_mod |= 0x12;
    if (mod & MOD_RALT)
        via_mod |= 0x14;
    if (mod & MOD_RGUI)
        via_mod |= 0x18;
    return via_mod;
}

static uint8_t from_via_mods(uint8_t via_mod) {
    // VIA's 5-bit mods: bits 0-3 Ctrl/Shift/Alt/GUI, bit 4 makes them all right-hand
    uint8_t mod = via_mod & 0x0F; // MOD_LCTRL..MOD_LGUI are bits 0-3, MOD_RCTRL..MOD_RGUI bits 4-7
    return (via_mod & 0x10) ? (uint8_t)(mod << 4) : mod;
}

// VIA's media keycodes (0x00A8-0x00BE) and the consumer usages ECK sends for them.
static const struct {
    uint16_t via;
    uint16_t usage;
} via_consumer_keys[] = {
    {0x00A8, 0xE2},  {0x00A9, 0xE9},  {0x00AA, 0xEA},  {0x00AB, 0xB5},  {0x00AC, 0xB6},
    {0x00AD, 0xB7},  {0x00AE, 0xCD},  {0x00AF, 0x183}, {0x00B0, 0xB8},  {0x00B1, 0x18A},
    {0x00B2, 0x192}, {0x00B3, 0x194}, {0x00B4, 0x221}, {0x00B5, 0x223}, {0x00B6, 0x224},
    {0x00B7, 0x225}, {0x00B8, 0x226}, {0x00B9, 0x227}, {0x00BA, 0x22A}, {0x00BB, 0xB3},
    {0x00BC, 0xB4},  {0x00BD, 0x6F},  {0x00BE, 0x70},
};
#define VIA_KC_SYSTEM_POWER 0x00A5 // ECK has no system report: keyboard-page Power instead

// Helper function to map ECK 32-bit keycodes to VIA 16-bit keycodes
uint16_t to_via_keycode(uint32_t key) {
    if (key == K_TRNS)
        return 0x0001;
    if (key == K_NULL)
        return 0x0000;
    if ((key & 0xFF000000) == KIND_CONSUMER) {
        for (size_t i = 0; i < sizeof(via_consumer_keys) / sizeof(via_consumer_keys[0]); i++)
            if (via_consumer_keys[i].usage == (key & 0xFFFF))
                return via_consumer_keys[i].via;
        return 0x0000;
    }
    if (key == K_BOOTLOADER)
        return 0x7C00;
    if (key == K_LYRUP || key == K_LYRDWN) // no VIA keycode; kept as-is so EEPROM keeps them
        return (uint16_t)key;
    if (key >= 0xC0 && key <= 0xDF)
        return 0x7700 + (key - 0xC0);
    if ((key & 0xFF000000) == KIND_MO)
        return 0x5220 | (key & 0xFF);
    if ((key & 0xFF000000) == KIND_TG)
        return 0x5260 | (key & 0xFF);

    if ((key & 0xFF000000) == KIND_HT) {
        uint8_t layer_or_mod = (key >> 8) & 0xFF;
        uint8_t kc = key & 0xFF;
        if (HT_IS_LAYER(key)) return 0x4000 | (layer_or_mod << 8) | kc;
        uint8_t mods = (key & HT_MODS_FLAG) ? mask_to_via(layer_or_mod) : to_via_mods(layer_or_mod);
        return 0x2000 | (mods << 8) | kc;
    }
    if ((key & 0xFF000000) == KIND_MK) {
        return (uint16_t)((mask_to_via((key >> 8) & 0xFF) << 8) | (key & 0xFF));
    }
#ifdef VIAL_ENABLE
    if ((key & 0xFF000000) == KIND_OS) // OSM 0x52A0 + 5-bit mods, OSL 0x5280 + layer
        return (key & 0x10000) ? (uint16_t)(0x52A0 | (mask_to_via((key >> 8) & 0xFF) & 0x1F))
                               : (uint16_t)(0x5280 | (key & 0x1F));
#endif
    if (key >= L_0 && key <= L_15)
        return 0x5220 | (key - L_0);
    if (key >= 0x7E00 && key <= 0x7F3F) // QK_KB / QK_USER: HHKB custom keycodes
        return (uint16_t)key;
#ifdef VIAL_ENABLE
    if (is_vial_keycode(key))
        return (uint16_t)key;
#endif
    return (key <= 0xFF) ? (uint16_t)key : 0x0000;
}

// Helper function to map VIA 16-bit keycodes to ECK 32-bit keycodes
uint32_t from_via_keycode(uint16_t via_key) {
    if (via_key == 0x0000)
        return K_NULL;
    if (via_key == 0x0001)
        return K_TRNS;
    if (via_key >= 0x7E00 && via_key <= 0x7F3F)
        return via_key;
#ifdef VIAL_ENABLE
    if (is_vial_keycode(via_key))
        return via_key;
#endif
    if (via_key == 0x7C00 || via_key == 0x5C00)
        return K_BOOTLOADER;
    if (via_key == K_LYRUP || via_key == K_LYRDWN)
        return via_key;
    if (via_key >= 0x7700 && via_key <= 0x771F)
        return 0xC0 + (via_key - 0x7700);

    if (via_key >= 0x00CD && via_key <= 0x00DF)
        return K_NULL; // mouse keys: no mouse report
    if (via_key == VIA_KC_SYSTEM_POWER)
        return K_POWER;
    for (size_t i = 0; i < sizeof(via_consumer_keys) / sizeof(via_consumer_keys[0]); i++)
        if (via_consumer_keys[i].via == via_key)
            return KIND_CONSUMER | via_consumer_keys[i].usage;
    if (via_key >= 0x00A5 && via_key <= 0x00BE)
        return K_NULL; // system sleep/wake: ECK has no system report
    if (via_key >= 0x0002 && via_key <= 0x00FF)
        return via_key;
    if (via_key >= 0x0100 && via_key <= 0x1FFF) {
        uint8_t mods = from_via_mods((via_key >> 8) & 0xFF);
        return mods ? (uint32_t)MK(mods, (via_key & 0xFF)) : from_via_keycode(via_key & 0xFF); // 0x10xx: no modifier
    }
    if (via_key >= 0x2000 && via_key <= 0x3FFF)
        return MT(from_via_mods((via_key >> 8) & 0x1F), (via_key & 0xFF));
    if (via_key >= 0x4000 && via_key <= 0x4FFF)
        return HT((via_key >> 8) & 0x0F, (via_key & 0xFF));

#ifdef VIAL_ENABLE
    if (via_key >= 0x5280 && via_key <= 0x529F)
        return OS_LAYER(via_key & 0x0F);
    if (via_key >= 0x52A0 && via_key <= 0x52BF)
        return OS_MOD(from_via_mods(via_key & 0x1F));
#endif

    // Layer Toggle
    if (via_key >= 0x5260 && via_key <= 0x527F) {
        return TG(via_key & 0x0F);
    }

    // Layer Momentary
    if (via_key >= 0x5220 && via_key <= 0x523F) {
        return MO(via_key & 0x0F);
    }

    return K_NULL;
}

#define VIA_EEPROM_SAVE_DELAY_MS 500
static volatile bool s_eeprom_dirty = false;
static volatile TickType_t s_eeprom_dirty_time = 0;

static void via_keymap_reset(void) {
    memset(dynamic_keymap, 0, sizeof(dynamic_keymap));
    size_t layers = keymap_layers > DYNAMIC_KEYMAP_MAX_LAYERS ? DYNAMIC_KEYMAP_MAX_LAYERS : keymap_layers;
    for (size_t l = 0; l < layers; l++) {
        for (size_t k = 0; k < NUM_KEYS; k++) {
            dynamic_keymap[l][k] = keymap[l][k];
        }
    }
}

static void via_storage_init_default(void) {
    memset(&via_storage, 0, sizeof(via_storage));
    via_keymap_reset();
}

void via_eeprom_save(void) {
    s_eeprom_dirty_time = xTaskGetTickCount();
    s_eeprom_dirty = true;
}

void via_eeprom_task(void) {
    via_keymap_poll();
#ifdef VIAL_ENABLE
    vial_storage_poll();
#endif
    if (s_eeprom_dirty) {
        TickType_t now = xTaskGetTickCount();
        if ((now - s_eeprom_dirty_time) >= pdMS_TO_TICKS(VIA_EEPROM_SAVE_DELAY_MS)) {
            s_eeprom_dirty = false;
            via_keymap_save();
        }
    }
}

void via_init(void) {
    s_eeprom_dirty = false;
    via_storage_init_default();
    via_keymap_load();
#ifdef VIAL_ENABLE
    vial_init();
#endif
}

#ifdef VIAL_ENABLE
static volatile bool s_via_mode; // until restart: report VIA protocol 12 instead of Vial's 9
static volatile bool s_via_reconnect;

/* The apps read the protocol only when they connect: reconnect USB, and blink to confirm
 * (2 blinks into VIA mode, 1 back to Vial). */
void via_mode_toggle(void) {
    s_via_mode = !s_via_mode;
    s_via_reconnect = true;
    indicators_blink(s_via_mode ? 2 : 1);
}

/* USB task: drop off the bus for 100 ms so the host re-enumerates and the app reconnects. */
void via_mode_task(void) {
    if (!s_via_reconnect) return;
    s_via_reconnect = false;
    if (!tud_mounted()) return;
    tud_disconnect();
    vTaskDelay(pdMS_TO_TICKS(100));
    tud_connect();
}
#endif

void via_process_packet(uint8_t const *request, uint8_t *response) {
    // Fill response with request values as a default/fallback
    memcpy(response, request, 32);

    uint8_t command_id = request[0];
    switch (command_id) {
    case VIA_GET_PROTOCOL_VERSION: {
#ifdef VIAL_ENABLE
        // The Vial app accepts only protocol 9; VIA mode reports 12 for usevia.app's menus
        response[1] = 0x00;
        response[2] = s_via_mode ? 0x0C : 0x09;
#else
        // VIA v3 (Protocol 12) for usevia.app Custom UI menus
        response[1] = 0x00;
        response[2] = 0x0C;
#endif
        break;
    }

    case VIA_GET_KEYBOARD_VALUE: {
        uint8_t sub = request[1];
        switch (sub) {
        case VIA_SUB_UPTIME: {
            uint32_t uptime = (uint32_t)(xTaskGetTickCount() * (1000 / configTICK_RATE_HZ));
            response[2] = (uptime >> 24) & 0xFF;
            response[3] = (uptime >> 16) & 0xFF;
            response[4] = (uptime >> 8) & 0xFF;
            response[5] = uptime & 0xFF;
            break;
        }
        case VIA_SUB_LAYOUT_OPTIONS: {
            response[2] = 0;
            response[3] = 0;
            response[4] = 0;
            response[5] = 0;
            break;
        }
        case VIA_SUB_SWITCH_MATRIX_STATE: {
#ifdef VIAL_ENABLE
            // VIA protocol 9 (Vial): all rows from byte 2, no row offset
            if (!s_via_mode) {
                memset(&response[2], 0, 30);
                for (uint8_t r = 0, idx = 2; r < NUM_ROWS; r++) {
                    uint16_t value = 0;
                    for (uint8_t c = 0; c < NUM_COLS; c++)
                        if (matrix_is_pressed(r, c)) value |= (uint16_t)(1U << c);
                    response[idx++] = (uint8_t)(value >> 8);
                    response[idx++] = (uint8_t)value;
                }
                _Static_assert(NUM_COLS <= 16 && 2 + 2 * NUM_ROWS <= 32, "matrix must fit one protocol-9 reply");
                break;
            }
#endif
            uint8_t row_offset = request[2];
            response[2] = row_offset;
            uint8_t col_bytes = (NUM_COLS + 7) / 8;
            uint8_t max_rows = col_bytes ? (28 / col_bytes) : 0;
            memset(&response[3], 0, 29);
            uint8_t idx = 3;
            for (uint8_t r = 0; r < max_rows && (uint8_t)(r + row_offset) < NUM_ROWS; r++) {
                uint8_t actual_row = r + row_offset;
                uint32_t value = 0;
                for (uint8_t c = 0; c < NUM_COLS; c++) {
                    if (matrix_is_pressed(actual_row, c)) {
                        value |= (1UL << c);
                    }
                }
                for (int8_t b = (int8_t)col_bytes - 1; b >= 0; b--) {
                    if (idx < 32) {
                        response[idx++] = (uint8_t)((value >> (b * 8)) & 0xFF);
                    }
                }
            }
            break;
        }
        case VIA_SUB_FIRMWARE_VERSION: {
#ifndef VIA_FIRMWARE_VERSION
#define VIA_FIRMWARE_VERSION 1U
#endif
            uint32_t version = VIA_FIRMWARE_VERSION;
            response[2] = (uint8_t)(version >> 24);
            response[3] = (uint8_t)(version >> 16);
            response[4] = (uint8_t)(version >> 8);
            response[5] = (uint8_t)version;
            break;
        }
        default:
            break;
        }
        break;
    }

    case VIA_DYNAMIC_KEYMAP_GET_LAYER_COUNT: {
        response[1] = (uint8_t)keymap_layers;
        break;
    }

    case VIA_DYNAMIC_KEYMAP_GET_KEYCODE: {
        uint8_t layer = request[1];
        uint8_t row = request[2];
        uint8_t col = request[3];
        int16_t ki = keyboard_get_flat_key_index(row, col);
        uint16_t via_key = 0x0001; // transparent default
        if (ki >= 0 && layer < DYNAMIC_KEYMAP_MAX_LAYERS) {
            via_key = to_via_keycode(dynamic_keymap[layer][ki]);
        }
        response[4] = (via_key >> 8) & 0xFF;
        response[5] = via_key & 0xFF;
        break;
    }

    case VIA_DYNAMIC_KEYMAP_SET_KEYCODE: {
        uint8_t layer = request[1];
        uint8_t row = request[2];
        uint8_t col = request[3];
        uint16_t via_key = ((uint16_t)request[4] << 8) | request[5];
        int16_t ki = keyboard_get_flat_key_index(row, col);
        if (ki >= 0 && layer < DYNAMIC_KEYMAP_MAX_LAYERS) {
            dynamic_keymap[layer][ki] = from_via_keycode(via_key);
            via_eeprom_save();
        }
        break;
    }

    case VIA_DYNAMIC_KEYMAP_GET_BUFFER: {
        uint16_t offset = ((uint16_t)request[1] << 8) | request[2];
        uint8_t size = request[3];
        if (size > 28) {
            size = 28;
        }
        uint16_t start = offset / 2;
        uint8_t count = size / 2;
        uint16_t matrix_size = (uint16_t)NUM_ROWS * (uint16_t)NUM_COLS;
        for (uint8_t i = 0; i < count; i++) {
            uint16_t idx = start + i;
            uint8_t layer = matrix_size ? (idx / matrix_size) : 0;
            uint16_t m_pos = matrix_size ? (idx % matrix_size) : 0;
            uint8_t row = m_pos / NUM_COLS;
            uint8_t col = m_pos % NUM_COLS;
            int16_t ki = keyboard_get_flat_key_index(row, col);
            uint16_t via_key = 0x0001; // transparent default
            if (layer < DYNAMIC_KEYMAP_MAX_LAYERS && ki >= 0 && ki < (int16_t)NUM_KEYS) {
                via_key = to_via_keycode(dynamic_keymap[layer][ki]);
            }
            response[4 + i * 2] = (via_key >> 8) & 0xFF;
            response[5 + i * 2] = via_key & 0xFF;
        }
        break;
    }

    case VIA_DYNAMIC_KEYMAP_SET_BUFFER: {
        uint16_t offset = ((uint16_t)request[1] << 8) | request[2];
        uint8_t size = request[3];
        if (size > 28) {
            size = 28;
        }
        uint16_t start = offset / 2;
        uint8_t count = size / 2;
        uint16_t matrix_size = (uint16_t)NUM_ROWS * (uint16_t)NUM_COLS;
        for (uint8_t i = 0; i < count; i++) {
            uint16_t idx = start + i;
            uint8_t layer = matrix_size ? (idx / matrix_size) : 0;
            uint16_t m_pos = matrix_size ? (idx % matrix_size) : 0;
            uint8_t row = m_pos / NUM_COLS;
            uint8_t col = m_pos % NUM_COLS;
            int16_t ki = keyboard_get_flat_key_index(row, col);
            uint16_t via_key = ((uint16_t)request[4 + i * 2] << 8) | request[5 + i * 2];
            if (layer < DYNAMIC_KEYMAP_MAX_LAYERS && ki >= 0 && ki < (int16_t)NUM_KEYS) {
                dynamic_keymap[layer][ki] = from_via_keycode(via_key);
            }
        }
        via_eeprom_save();
        break;
    }

    case VIA_DYNAMIC_KEYMAP_RESET: {
        via_keymap_reset();
        via_eeprom_save();
        break;
    }

    case VIA_EEPROM_RESET: {
        via_storage_init_default();
        via_eeprom_save();
        break;
    }

    case VIA_BOOTLOADER_JUMP: {
        bootloader_jump();
        break;
    }

    case VIA_CUSTOM_SET_VALUE:
    case VIA_CUSTOM_GET_VALUE:
    case VIA_CUSTOM_SAVE:
        if (!hhkb_via_command(request, response)) response[0] = 0xFF; // no other channels
        break;

    case VIA_DYNAMIC_KEYMAP_MACRO_GET_COUNT: {
        response[1] = DYNAMIC_KEYMAP_MACRO_COUNT;
        break;
    }

    case VIA_DYNAMIC_KEYMAP_MACRO_GET_BUFFER_SIZE: {
        response[1] = (DYNAMIC_KEYMAP_MACRO_BUFFER_SIZE >> 8) & 0xFF;
        response[2] = DYNAMIC_KEYMAP_MACRO_BUFFER_SIZE & 0xFF;
        break;
    }

    case VIA_DYNAMIC_KEYMAP_MACRO_GET_BUFFER: {
        uint16_t offset = ((uint16_t)request[1] << 8) | request[2];
        uint8_t size = request[3];
        if (size > 28) {
            size = 28;
        }
        if ((uint32_t)offset + size <= DYNAMIC_KEYMAP_MACRO_BUFFER_SIZE) {
            memcpy(&response[4], &dynamic_macro_buffer[offset], size);
        }
        break;
    }

    case VIA_DYNAMIC_KEYMAP_MACRO_SET_BUFFER: {
        uint16_t offset = ((uint16_t)request[1] << 8) | request[2];
        uint8_t size = request[3];
        if (size > 28) {
            size = 28;
        }
        if ((uint32_t)offset + size <= DYNAMIC_KEYMAP_MACRO_BUFFER_SIZE) {
            memcpy(&dynamic_macro_buffer[offset], &request[4], size);
            via_eeprom_save();
        }
        break;
    }

    case VIA_DYNAMIC_KEYMAP_MACRO_RESET: {
        memset(dynamic_macro_buffer, 0, DYNAMIC_KEYMAP_MACRO_BUFFER_SIZE);
        via_eeprom_save();
        break;
    }

#ifdef VIAL_ENABLE
    case 0xFE:
        vial_process(response);
        break;
#endif

    default:
        response[0] = 0xFF; // Unhandled command
        break;
    }
}

static uint16_t ascii_to_keycode(char c, bool *shift) {
    *shift = false;
    if (c >= 'a' && c <= 'z')
        return (c - 'a' + 0x04);
    if (c >= 'A' && c <= 'Z') {
        *shift = true;
        return (c - 'A' + 0x04);
    }
    if (c >= '1' && c <= '9')
        return (c - '1' + 0x1E);
    if (c == '0')
        return 0x27;

    static const struct {
        char c;
        uint8_t kc;
        bool s;
    } tbl[] = {{' ', 0x2C, 0},  {'\n', 0x28, 0}, {'\t', 0x2B, 0}, {'-', 0x2D, 0}, {'_', 0x2D, 1},
               {'=', 0x2E, 0},  {'+', 0x2E, 1},  {'[', 0x2F, 0},  {'{', 0x2F, 1}, {']', 0x30, 0},
               {'}', 0x30, 1},  {'\\', 0x31, 0}, {'|', 0x31, 1},  {';', 0x33, 0}, {':', 0x33, 1},
               {'\'', 0x34, 0}, {'"', 0x34, 1},  {'`', 0x35, 0},  {'~', 0x35, 1}, {',', 0x36, 0},
               {'<', 0x36, 1},  {'.', 0x37, 0},  {'>', 0x37, 1},  {'/', 0x38, 0}, {'?', 0x38, 1}};
    for (size_t i = 0; i < sizeof(tbl) / sizeof(tbl[0]); i++) {
        if (tbl[i].c == c) {
            *shift = tbl[i].s;
            return tbl[i].kc;
        }
    }
    return 0;
}

static void send_event(uint16_t kc, bool pressed) {
    keyboard_send_key(kc, pressed);
}

typedef struct {
    uint16_t kc;
    uint8_t pressed;
    uint8_t delay_ms;
} via_macro_uop_t;

static uint16_t s_macro_offset = DYNAMIC_KEYMAP_MACRO_BUFFER_SIZE;
static TickType_t s_macro_deadline = 0;
static via_macro_uop_t s_uops[4];
static uint8_t s_uop_head = 0;
static uint8_t s_uop_count = 0;

static void push_uop(uint16_t kc, uint8_t pressed, uint8_t delay_ms) {
    if (s_uop_count < 4) {
        s_uops[s_uop_count++] = (via_macro_uop_t){.kc = kc, .pressed = pressed, .delay_ms = delay_ms};
    }
}

static void queue_tap_key(uint16_t kc) {
    push_uop(kc, 1, 10);
    push_uop(kc, 0, 10);
}

static void queue_ascii(char c) {
    bool shift = false;
    uint16_t kc = ascii_to_keycode(c, &shift);
    if (kc) {
        if (shift) {
            push_uop(0xE1, 1, 5);
        }
        queue_tap_key(kc);
        if (shift) {
            push_uop(0xE1, 0, 5);
        }
    }
}

bool via_macro_has_active(void) {
    return (s_uop_head < s_uop_count) ||
           (s_macro_offset < DYNAMIC_KEYMAP_MACRO_BUFFER_SIZE && dynamic_macro_buffer[s_macro_offset] != 0) ||
           (s_macro_deadline != 0);
}

TickType_t via_macro_check_timeouts(TickType_t now) {
    if (s_macro_deadline != 0) {
        if ((int32_t)(now - s_macro_deadline) < 0) {
            return (s_macro_deadline - now);
        }
        s_macro_deadline = 0;
    }

    const uint16_t end = DYNAMIC_KEYMAP_MACRO_BUFFER_SIZE;
    while (s_uop_head < s_uop_count || (s_macro_offset < end && dynamic_macro_buffer[s_macro_offset] != 0)) {
        if (s_uop_head < s_uop_count) {
            via_macro_uop_t uop = s_uops[s_uop_head++];
            if (s_uop_head >= s_uop_count) {
                s_uop_head = 0;
                s_uop_count = 0;
            }
            send_event(uop.kc, uop.pressed != 0);
            if (uop.delay_ms > 0) {
                TickType_t ticks = pdMS_TO_TICKS(uop.delay_ms);
                if (ticks == 0) {
                    ticks = 1;
                }
                s_macro_deadline = now + ticks;
                return ticks;
            }
            continue;
        }

        uint8_t c = dynamic_macro_buffer[s_macro_offset++];
        if (c == 0) {
            s_macro_offset = end;
            break;
        }

        if (c == 1) { // SS_QMK_PREFIX
            if (s_macro_offset >= end) {
                s_macro_offset = end;
                break;
            }
            uint8_t action = dynamic_macro_buffer[s_macro_offset++];
            if (action == 0) {
                s_macro_offset = end;
                break;
            }

            if (action == 1 || action == 2 || action == 3) { // SS_TAP_CODE, SS_DOWN_CODE, SS_UP_CODE
                if (s_macro_offset >= end) {
                    s_macro_offset = end;
                    break;
                }
                uint8_t kc = dynamic_macro_buffer[s_macro_offset++];
                if (kc == 0) {
                    s_macro_offset = end;
                    break;
                }
                if (action == 1) {
                    queue_tap_key(kc);
                } else if (action == 2) {
                    send_event(kc, true);
                } else {
                    send_event(kc, false);
                }
#ifdef VIAL_ENABLE
            } else if (action >= 5 && action <= 7) { // Vial: tap / down / up of a 16-bit keycode
                if (s_macro_offset + 1 >= end) {
                    s_macro_offset = end;
                    break;
                }
                uint16_t kc = (uint16_t)(dynamic_macro_buffer[s_macro_offset] | (dynamic_macro_buffer[s_macro_offset + 1] << 8));
                s_macro_offset += 2;
                if (!(kc & 0xFF) || !(kc >> 8)) {
                    s_macro_offset = end;
                    break;
                }
                if (kc > 0xFF00)
                    kc = (uint16_t)((kc & 0xFF) << 8);
                uint32_t key = from_via_keycode(kc);
                if (key == K_GRAVE_ESC)
                    key = vial_grave_escape();
                if (key == K_NULL) {
                    // unknown to ECK: skip it
                } else if (action == 5 && (key <= HID_KEY_EXSEL || (key >= 0xE0 && key <= 0xE7))) {
                    queue_tap_key((uint16_t)key); // a real HID usage
                } else {
                    if (action != 7) keyboard_process_virtual(VIAL_SLOT_MACRO, key, true);
                    if (action != 6) keyboard_process_virtual(VIAL_SLOT_MACRO, key, false);
                }
#endif
            } else if (action == 4) { // SS_DELAY_CODE
                if (s_macro_offset + 1 >= end) {
                    s_macro_offset = end;
                    break;
                }
                uint8_t d0 = dynamic_macro_buffer[s_macro_offset++];
                uint8_t d1 = dynamic_macro_buffer[s_macro_offset++];
                if (d0 == 0 || d1 == 0) {
                    s_macro_offset = end;
                    break;
                }
                uint32_t ms = (uint32_t)(d0 - 1) + (uint32_t)(d1 - 1) * 255u;
                if (ms > 0) {
                    TickType_t ticks = pdMS_TO_TICKS(ms);
                    if (ticks == 0) {
                        ticks = 1;
                    }
                    s_macro_deadline = now + ticks;
                    return ticks;
                }
            }
        } else {
            queue_ascii((char)c);
        }
    }

    s_macro_offset = end;
    return portMAX_DELAY;
}

void via_macro_send(uint8_t id) {
    if (id >= DYNAMIC_KEYMAP_MACRO_COUNT) {
        return;
    }

    while (s_uop_head < s_uop_count) {
        via_macro_uop_t uop = s_uops[s_uop_head++];
        if (!uop.pressed) {
            send_event(uop.kc, false);
        }
    }
    s_uop_head = 0;
    s_uop_count = 0;
    s_macro_deadline = 0;

    uint16_t offset = 0;
    const uint16_t end = DYNAMIC_KEYMAP_MACRO_BUFFER_SIZE;
    while (id > 0) {
        if (offset >= end) {
            s_macro_offset = end;
            return;
        }
        if (dynamic_macro_buffer[offset] == 0) {
            --id;
        }
        ++offset;
    }

    if (offset >= end || dynamic_macro_buffer[offset] == 0) {
        s_macro_offset = end;
        return;
    }

    s_macro_offset = offset;
    via_macro_check_timeouts(xTaskGetTickCount());
}

