/* dip_modes.c on the JIS HHKB (HHKB_JIS): PFU's JIS DIP switches, two Fn keys, 69 positions. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include "keys.h"

static uint16_t state = 1;
static uint8_t dips;
uint16_t layers_get_state(void) { return state; }
int16_t keyboard_get_flat_key_index(uint8_t row, uint8_t col) { return (int16_t)(row * 15 + col); }
uint8_t dip_switch_read(void) { return dips; }
static int last_blinks;
void indicators_blink(uint8_t count) { last_blinks = count; }
#include "dip_modes.c"

#define FN (1U | (1U << 1))
static uint32_t map(int idx, uint32_t key) { return dip_modes_map_key((uint8_t)(idx / 15), (uint8_t)(idx % 15), key); }
static void boot(uint8_t sw) { dips = sw; state = 1; dip_modes_early_init(); }
static bool key(int idx, bool pressed) { return dip_modes_matrix_change((uint8_t)(idx / 15), (uint8_t)(idx % 15), pressed); }
#define SW(n) (1U << ((n) - 1))

int main(void) {
    // All OFF: Win mode; BS, Fn+BS Delete; the IME keys, arrows, Ctrl and ◇/Alt pass through.
    boot(0);
    assert(s_mode == MODE_WIN);
    assert(map(POS_DEL, K_BSPC) == K_BSPC && map(POS_HANKAKU, K_GRAV) == K_GRAV);
    assert(map(POS_MUHENKAN, K_INT5) == K_INT5 && map(POS_KANA, K_INT2) == K_INT2);
    assert(map(POS_UP, K_UP) == K_UP && map(POS_CTRL, K_LCTL) == K_LCTL && map(POS_FN_LEFT, MO(1)) == MO(1));
    assert(map(POS_LGUI, K_LGUI) == K_LGUI);
    state = FN;
    assert(map(POS_DEL, K_BSPC) == K_DEL);

    // SW1: Mac mode: Caps Lock, 英数 (LANG2), かな (LANG1), right ◇; remaps left alone.
    boot(SW(1));
    assert(s_mode == MODE_MAC);
    assert(map(POS_HANKAKU, K_GRAV) == K_CAPS && map(POS_MUHENKAN, K_INT5) == K_LANG2);
    assert(map(POS_HENKAN, K_INT4) == K_LANG1 && map(POS_KANA, K_INT2) == K_RGUI);
    assert(map(POS_KANA, K_SPC) == K_SPC && map(POS_DEL, K_BSPC) == K_BSPC);

    // SW2: Control is 英数, the left Fn is Ctrl (also for the chords); the right Fn stays Fn.
    boot(SW(2));
    assert(map(POS_CTRL, K_LCTL) == K_LANG2 && map(POS_FN_LEFT, MO(1)) == K_LCTL && map(POS_FN_LEFT, MO(3)) == K_LCTL);
    key(POS_FN_LEFT, true);
    assert(hhkb_ctrl_held() && !hhkb_fn_held());
    key(POS_FN, true);
    assert(key(POS_W, true) && key(POS_W, false) && s_mode == MODE_MAC);
    key(POS_FN, false);
    key(POS_FN_LEFT, false);

    // SW3: BS is Delete, Fn+BS Backspace.
    boot(SW(3));
    assert(map(POS_DEL, K_BSPC) == K_DEL);
    state = FN;
    assert(map(POS_DEL, K_BSPC) == K_BSPC);

    // SW4: arrows are Shift, ◇, Delete, Ctrl.
    boot(SW(4));
    assert(map(POS_UP, K_UP) == K_RSFT && map(POS_DOWN, K_DOWN) == K_RGUI);
    assert(map(POS_LEFT, K_LEFT) == K_DEL && map(POS_RIGHT, K_RGHT) == K_RCTL);

    // SW5: left ◇ and left Alt swap; the right Alt doesn't.
    boot(SW(5));
    assert(map(POS_LALT, K_LALT) == K_LGUI && map(POS_LGUI, K_LGUI) == K_LALT && map(POS_RALT, K_RALT) == K_RALT);

    // Either Fn key works for the chords; the right one (position 65) doesn't alias position 1.
    boot(0);
    key(POS_FN, true);
    assert(hhkb_fn_held() && !held(POS_1));
    key(POS_CTRL, true);
    assert(key(POS_W, true) && key(POS_W, false) && s_mode == MODE_MAC && last_blinks == 2);
    key(POS_FN, false);
    assert(!hhkb_fn_held());
    key(POS_FN_LEFT, true);
    assert(key(POS_W, true) && key(POS_W, false) && s_mode == MODE_WIN && last_blinks == 1);
    key(POS_FN_LEFT, false);
    key(POS_CTRL, false);
    assert(!hhkb_fn_held() && !hhkb_ctrl_held());
    return 0;
}
