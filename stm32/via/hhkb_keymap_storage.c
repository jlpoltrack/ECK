/* Two independently validated EEPROM snapshots of the VIA keymap and macros.
 * Both slots (0x08080600..0x08080fff) were zero in the stock Hybrid EEPROM dump.
 * Never overwrite a slot containing data that is not ours. */
#include "via.h"
#include "eeprom.h"
#include "keys.h"
#include "stm32l0xx.h"
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#ifndef SLOT_ADDRESS
#define SLOT_ADDRESS EEPROM_KEYMAP_SLOTS_ADDR
#endif
#define SLOT_MAGIC     0x334b4345UL // 'ECK3': keys as 16-bit VIA codes + macros
#define SLOT_MAGIC_V2  0x324b4345UL // 'ECK2': 32-bit keys + 80 zero bytes + 224 B macros; read, then rewritten
#define SLOT_MAGIC_OLD 0x324b4d44UL // format 2 under its tag from before the ECK rename
#define SLOT_MAGIC_V1  0x314b4d44UL // format 1 (keymap only), from before the ECK rename
#define SLOT_COMMIT   0x54494d43UL // 'CMIT'

#define NUM_SLOT_KEYS (DYNAMIC_KEYMAP_MAX_LAYERS * NUM_KEYS)
#define V1_SLOT_WORDS (4U + NUM_SLOT_KEYS)
#define V2_PAYLOAD_WORDS (NUM_SLOT_KEYS + 20U + 224U / 4U)
#define V2_MACRO_WORD (NUM_SLOT_KEYS + 20U)

#define KEYMAP_WORDS  (NUM_SLOT_KEYS / 2U) // two 16-bit VIA codes per word
#define MACRO_WORDS   ((DYNAMIC_KEYMAP_MACRO_BUFFER_SIZE + 3U) / 4U)
#define PAYLOAD_WORDS (KEYMAP_WORDS + MACRO_WORDS)
#ifdef EEPROM_KEYMAP_LEGACY_FORMATS /* HHKB: slots may hold formats 1 and 2 */
#define SLOT_WORDS    (4U + V2_PAYLOAD_WORDS) // slot stride, fixed since format 2 so slots never move
_Static_assert(DYNAMIC_KEYMAP_MAX_LAYERS == 4 && NUM_KEYS == 60, "HHKB EEPROM layout requires 4 x 60 keys");
#else
#define SLOT_WORDS    (4U + PAYLOAD_WORDS)
#endif
_Static_assert(4U + PAYLOAD_WORDS <= SLOT_WORDS, "keymap + macros must fit a slot");
_Static_assert(DYNAMIC_KEYMAP_MACRO_BUFFER_SIZE >= 224U, "format-2 macros must fit on migration");
_Static_assert(2 * SLOT_WORDS * 4 <= EEPROM_KEYMAP_SLOTS_SIZE, "keymap slots must fit the board's EEPROM_KEYMAP_SLOTS_SIZE");

static uint32_t snapshot[PAYLOAD_WORDS];
static uint32_t generation, checksum;
static int active_slot = -1, target_slot = -1;
static unsigned stage;
static bool pending;

static volatile const uint32_t *slot(unsigned n) {
    return (volatile const uint32_t *)(SLOT_ADDRESS + n * SLOT_WORDS * 4U);
}
static volatile const uint32_t *slot_v1(unsigned n) {
    return (volatile const uint32_t *)(SLOT_ADDRESS + n * V1_SLOT_WORDS * 4U);
}
static uint32_t hash_word(uint32_t hash, uint32_t word) {
    for (unsigned n = 0; n < 4; n++) { hash ^= word & 0xffU; hash *= 16777619U; word >>= 8; }
    return hash;
}
static uint32_t slot_hash(volatile const uint32_t *p, unsigned words) {
    uint32_t h = hash_word(2166136261U, p[1]);
    for (unsigned i = 0; i < words; i++) h = hash_word(h, p[4 + i]);
    return h;
}
static bool is_v2(uint32_t magic) {
    return magic == SLOT_MAGIC_V2 || magic == SLOT_MAGIC_OLD;
}
static bool valid(unsigned n) {
    volatile const uint32_t *p = slot(n);
    if (p[3] != SLOT_COMMIT) return false;
    if (p[0] == SLOT_MAGIC) return p[2] == slot_hash(p, PAYLOAD_WORDS);
    return is_v2(p[0]) && p[2] == slot_hash(p, V2_PAYLOAD_WORDS);
}
static bool valid_v1(unsigned n) {
    volatile const uint32_t *p = slot_v1(n);
    return p[0] == SLOT_MAGIC_V1 && p[3] == SLOT_COMMIT && p[2] == slot_hash(p, NUM_SLOT_KEYS);
}
static bool has_valid_v1(void) {
    return valid_v1(0) || valid_v1(1);
}
static bool writable(unsigned n) {
    volatile const uint32_t *p = slot(n);
    if (p[0] == SLOT_MAGIC || is_v2(p[0])) return true;
    if (n == 0 && p[0] == SLOT_MAGIC_V1) return true;
    if (valid(1 - n)) return true;
    if (has_valid_v1()) return true;
    for (unsigned i = 0; i < SLOT_WORDS; i++) if (p[i] != 0) return false;
    return true;
}
/* Format 1 keymaps hold the internal codes from before keys.h moved them: K_LYRUP/K_LYRDWN
 * 0xA5/0xA6, RGB keys 0xA7..0xB2 (no RGB: none), K_BOOTLOADER 0xB3. */
static uint32_t v1_keycode(uint32_t key) {
    if (key == 0xA5 || key == 0xA6) return 0x7C01 + (key - 0xA5); // K_LYRUP, K_LYRDWN
    if (key >= 0xA7 && key <= 0xB2) return K_NULL;
    if (key == 0xB3) return 0x7C00;                               // K_BOOTLOADER
    return key;
}
void via_keymap_load(void) {
    bool a = valid(0), b = valid(1);
    if (a && b) active_slot = (int32_t)(slot(1)[1] - slot(0)[1]) > 0 ? 1 : 0;
    else if (a) active_slot = 0;
    else if (b) active_slot = 1;
    else {
        bool v1_a = valid_v1(0), v1_b = valid_v1(1);
        if (!v1_a && !v1_b) return;
        int v1_slot = (v1_a && v1_b) ? ((int32_t)(slot_v1(1)[1] - slot_v1(0)[1]) > 0 ? 1 : 0) : (v1_a ? 0 : 1);
        volatile const uint32_t *p = slot_v1((unsigned)v1_slot);
        generation = p[1];
        uint32_t *dst = &dynamic_keymap[0][0];
        for (unsigned i = 0; i < NUM_SLOT_KEYS; i++) dst[i] = v1_keycode(p[4 + i]);
        memset(dynamic_macro_buffer, 0, sizeof(dynamic_macro_buffer)); // format 1 had no macros
        pending = true;
        return;
    }
    volatile const uint32_t *p = slot((unsigned)active_slot);
    generation = p[1];
    uint32_t *dst = &dynamic_keymap[0][0];
    const volatile uint8_t *macro_src;
    size_t macro_len;
    if (p[0] == SLOT_MAGIC) {
        for (unsigned i = 0; i < NUM_SLOT_KEYS; i++)
            dst[i] = from_via_keycode((uint16_t)(p[4 + i / 2] >> (16 * (i & 1))));
        macro_src = (const volatile uint8_t *)&p[4 + KEYMAP_WORDS];
        macro_len = DYNAMIC_KEYMAP_MACRO_BUFFER_SIZE;
    } else { // format 2: internal codes; rewrite as format 3
        for (unsigned i = 0; i < NUM_SLOT_KEYS; i++) dst[i] = p[4 + i];
        macro_src = (const volatile uint8_t *)&p[4 + V2_MACRO_WORD];
        macro_len = 224;
        pending = true;
    }
    memset(dynamic_macro_buffer, 0, sizeof(dynamic_macro_buffer));
    for (size_t i = 0; i < macro_len; i++) dynamic_macro_buffer[i] = macro_src[i];
}
void via_keymap_save(void) { pending = true; }

/* One EEPROM word per poll keeps the USB task responsive during a full save. */
void via_keymap_poll(void) {
    if (target_slot < 0) {
        if (!pending) return;
        pending = false;
        int candidate = active_slot < 0 ? 0 : 1 - active_slot;
        if (!writable((unsigned)candidate)) {
            if (active_slot >= 0 || !writable(1)) return;
            candidate = 1;
        }
        memset(snapshot, 0, sizeof(snapshot));
        const uint32_t *keys = &dynamic_keymap[0][0];
        for (unsigned i = 0; i < NUM_SLOT_KEYS; i++)
            snapshot[i / 2] |= (uint32_t)to_via_keycode(keys[i]) << (16 * (i & 1));
        memcpy(&snapshot[KEYMAP_WORDS], dynamic_macro_buffer, sizeof(dynamic_macro_buffer));
        generation++;
        checksum = hash_word(2166136261U, generation);
        for (unsigned i = 0; i < PAYLOAD_WORDS; i++) checksum = hash_word(checksum, snapshot[i]);
        target_slot = candidate;
        stage = 0;
    }
    volatile uint32_t *dst = (volatile uint32_t *)(SLOT_ADDRESS + (unsigned)target_slot * SLOT_WORDS * 4U);
    unsigned offset; uint32_t value;
    if (stage == 0) { offset = 3; value = 0; }
    else if (stage == 1) { offset = 0; value = SLOT_MAGIC; }
    else if (stage == 2) { offset = 1; value = generation; }
    else if (stage == 3) { offset = 2; value = checksum; }
    else if (stage < 4 + PAYLOAD_WORDS) { offset = stage; value = snapshot[stage - 4]; }
    else { offset = 3; value = SLOT_COMMIT; }
    if (!eeprom_write_word(&dst[offset], value)) { target_slot = -1; return; }
    if (++stage == 5 + PAYLOAD_WORDS) {
        active_slot = target_slot;
        target_slot = -1;
    }
}
