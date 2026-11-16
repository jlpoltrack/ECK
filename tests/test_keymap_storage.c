#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "via.h"
#include "stm32l0xx.h"

flash_t flash;
flash_t *FLASH = &flash;
via_storage_t via_storage;
static uint32_t mock_eeprom[768];
#define SLOT_ADDRESS ((uintptr_t)mock_eeprom)
#include "hhkb_keymap_storage.c"

static void restart(void) {
    active_slot = -1;
    target_slot = -1;
    generation = 0;
    stage = 0;
    pending = false;
    memset(&via_storage, 0, sizeof via_storage);
    via_keymap_load();
}
static void poll_all(void) {
    for (int i = 0; i < 400; i++) via_keymap_poll();
}
#define FIXTURE_SLOTS_OFFSET 0x600 /* SLOT_ADDRESS - 0x08080000 */
static uint8_t fixture[6144];

static void load_fixture(void) {
    FILE *f = fopen(FIXTURE_DIR "/hybrid_eeprom_2026-11-16.bin", "rb");
    assert(f && fread(fixture, 1, sizeof fixture, f) == sizeof fixture);
    fclose(f);
    memset(mock_eeprom, 0, sizeof mock_eeprom);
    memcpy(mock_eeprom, &fixture[FIXTURE_SLOTS_OFFSET], 2 * SLOT_WORDS * 4);
}

/* The owner's Hybrid EEPROM (two slots under the pre-ECK tag, generations 19 and
 * 20, written by the Vial firmware): load the newest keymap and macros, rewrite them under 'ECK2'. */
static void test_owner_eeprom(void) {
    load_fixture();
    const uint8_t *newest = &fixture[FIXTURE_SLOTS_OFFSET + SLOT_WORDS * 4]; /* slot 1 */
    uint32_t gen;
    memcpy(&gen, newest + 4, 4);
    assert(gen == 20);
    restart();
    assert(active_slot == 1);
    assert(memcmp(dynamic_keymap, newest + 16, KEYMAP_WORDS * 4) == 0);
    assert(memcmp(dynamic_macro_buffer, newest + 16 + (KEYMAP_WORDS + RESERVED_WORDS) * 4,
                  DYNAMIC_KEYMAP_MACRO_BUFFER_SIZE) == 0);
    /* Spot checks against the keymap VIA shows on that keyboard. */
    assert(dynamic_keymap[0][54] == 0x10000001); /* Fn: MO(1) */
    assert(dynamic_keymap[1][53] == 0x10000002); /* Fn layer right Shift: MO(2) */
    assert(dynamic_keymap[1][43] == 0xC0);       /* Fn+Z: macro 0 */
    assert(memcmp(dynamic_macro_buffer, "exampleex\x01\x01\x28\x00", 13) == 0);

    /* Loading an old-tag slot rewrites it into the older slot: same data, 'ECK2', reserved words zero. */
    poll_all();
    assert(active_slot == 0 && mock_eeprom[0] == SLOT_MAGIC && mock_eeprom[1] == 21);
    assert(valid(0) && valid(1));
    assert(memcmp(&mock_eeprom[4], newest + 16, KEYMAP_WORDS * 4) == 0);
    for (unsigned i = 0; i < RESERVED_WORDS; i++) assert(mock_eeprom[4 + KEYMAP_WORDS + i] == 0);
    assert(memcmp(&mock_eeprom[4 + KEYMAP_WORDS + RESERVED_WORDS], newest + 16 + (KEYMAP_WORDS + RESERVED_WORDS) * 4,
                  MACRO_WORDS * 4) == 0);
    restart();
    assert(active_slot == 0 && memcmp(dynamic_keymap, newest + 16, KEYMAP_WORDS * 4) == 0);

    /* A slot whose reserved words still hold Vial combos loads the same keymap and macros. */
    load_fixture();
    uint32_t *s1 = &mock_eeprom[SLOT_WORDS];
    s1[4 + KEYMAP_WORDS] = 0x00050004U; /* combo inputs A, B */
    s1[2] = slot_hash(s1);
    restart();
    assert(active_slot == 1 && memcmp(dynamic_keymap, newest + 16, KEYMAP_WORDS * 4) == 0);
    assert(memcmp(dynamic_macro_buffer, "exampleex", 9) == 0);
}

int main(void) {
    dynamic_keymap[2][23] = 0x12345678;
    via_keymap_save();
    poll_all();
    restart();
    assert(dynamic_keymap[2][23] == 0x12345678);

    dynamic_keymap[2][23] = 77;
    via_keymap_save();
    for (int i = 0; i < 20; i++) via_keymap_poll();
    restart();
    assert(dynamic_keymap[2][23] == 0x12345678);

    dynamic_keymap[2][23] = 99;
    via_keymap_save();
    poll_all();
    restart();
    assert(dynamic_keymap[2][23] == 99);

    dynamic_keymap[0][0] = 123;
    via_keymap_save();
    for (int i = 0; i < 30; i++) via_keymap_poll();
    dynamic_keymap[0][0] = 456;
    via_keymap_save();
    poll_all();
    poll_all();
    restart();
    assert(dynamic_keymap[0][0] == 456);

    // Macro persistence
    dynamic_macro_buffer[0] = 0x01; // Down action
    dynamic_macro_buffer[1] = 0x04; // Key 'A'
    dynamic_macro_buffer[2] = 0x02; // Up action
    dynamic_macro_buffer[3] = 0x04; // Key 'A'
    dynamic_macro_buffer[4] = 0x00; // End of macro
    via_keymap_save();
    poll_all();
    restart();
    assert(memcmp(dynamic_macro_buffer, "\x01\x04\x02\x04\x00", 5) == 0);

    memset(mock_eeprom, 0, sizeof mock_eeprom);
    mock_eeprom[12] = 0x1234;
    mock_eeprom[SLOT_WORDS + 12] = 0x5678;
    restart();
    dynamic_keymap[0][0] = 1;
    via_keymap_save();
    poll_all();
    assert(mock_eeprom[12] == 0x1234 && mock_eeprom[SLOT_WORDS + 12] == 0x5678);
    assert(active_slot == -1);

    // Verify migration from format 1 snapshots
    memset(mock_eeprom, 0, sizeof mock_eeprom);
    mock_eeprom[0] = SLOT_MAGIC_V1;
    mock_eeprom[1] = 1;
    mock_eeprom[3] = SLOT_COMMIT;
    for (unsigned i = 0; i < V1_KEYMAP_WORDS; i++) mock_eeprom[4 + i] = 0x1000 + i;
    mock_eeprom[2] = slot_v1_hash(&mock_eeprom[0]);

    uint32_t *v1_slot1 = &mock_eeprom[V1_SLOT_WORDS];
    v1_slot1[0] = SLOT_MAGIC_V1;
    v1_slot1[1] = 2;
    v1_slot1[3] = SLOT_COMMIT;
    for (unsigned i = 0; i < V1_KEYMAP_WORDS; i++) v1_slot1[4 + i] = 0x2000 + i;
    v1_slot1[4 + 1] = 0xB3;  // old K_BOOTLOADER
    v1_slot1[4 + 2] = 0xA7;  // old K_RGB_TOGG
    v1_slot1[4 + 3] = 0xA5;  // old K_LYRUP
    v1_slot1[2] = slot_v1_hash(v1_slot1);

    restart();
    assert(dynamic_keymap[0][0] == 0x2000);
    assert(dynamic_keymap[0][1] == 0x7C00 && dynamic_keymap[0][2] == K_NULL && dynamic_keymap[0][3] == 0x7C01);
    assert(dynamic_keymap[3][59] == 0x2000 + 239);

    poll_all();
    assert(active_slot == 0);
    assert(mock_eeprom[0] == SLOT_MAGIC);

    dynamic_keymap[0][0] = 0x9999;
    via_keymap_save();
    poll_all();
    assert(active_slot == 1);
    assert(mock_eeprom[SLOT_WORDS] == SLOT_MAGIC);

    restart();
    assert(dynamic_keymap[0][0] == 0x9999);
    assert(dynamic_keymap[3][59] == 0x2000 + 239);

    test_owner_eeprom();
    puts("keymap storage tests passed");
    return 0;
}
