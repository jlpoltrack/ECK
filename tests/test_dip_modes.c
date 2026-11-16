#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include "keys.h"

static uint16_t state = 1;
static uint8_t dips;
uint16_t layers_get_state(void) { return state; }
int16_t keyboard_get_flat_key_index(uint8_t row, uint8_t col) { return (int16_t)(row * 15 + col); }
void dip_switch_init(void) {}
uint8_t dip_switch_read(void) { return dips; }
#include "dip_modes.c"

#define FN (1U | (1U << 1))
// Flat index -> (row, col) for the mock above.
static uint32_t map(int idx, uint32_t key) { return dip_modes_map_key((uint8_t)(idx / 15), (uint8_t)(idx % 15), key); }
static void boot(uint8_t sw) { dips = sw; state = 1; dip_modes_early_init(); }
static bool key(int idx, bool pressed) { return dip_modes_matrix_change((uint8_t)(idx / 15), (uint8_t)(idx % 15), pressed); }
#define SW(n) (1U << ((n) - 1))

int main(void) {
    // HHK mode, all OFF: Delete key sends Delete, Fn+Delete Backspace, Fn+right ◇ Stop.
    boot(0);
    assert(map(POS_DEL, K_BSPC) == K_DEL);
    assert(map(POS_DEL, K_SPC) == K_SPC);
    assert(map(POS_LGUI, K_LGUI) == K_LGUI && map(POS_LALT, K_LALT) == K_LALT);
    assert(map(POS_RGUI, K_RGUI) == K_RGUI);
    state = FN;
    assert(map(POS_DEL, K_BSPC) == K_BSPC);
    assert(map(POS_RGUI, K_RGUI) == K_STOP_KB);
    assert(map(POS_F, K_F) == K_F);

    // SW3: Delete key sends Backspace, Fn+Delete Delete.
    boot(SW(3));
    assert(map(POS_DEL, K_BSPC) == K_BSPC && map(POS_DEL, K_DEL) == K_BSPC);
    state = FN;
    assert(map(POS_DEL, K_BSPC) == K_DEL);

    // Win mode: no Stop. Mac mode: SW3 ignored, Fn+F Eject, Fn+Delete Clear.
    boot(SW(1));
    state = FN;
    assert(map(POS_RGUI, K_RGUI) == K_RGUI);
    boot(SW(2) | SW(3));
    assert(map(POS_DEL, K_BSPC) == K_BSPC);
    state = FN;
    assert(map(POS_DEL, K_BSPC) == K_CLEAR && map(POS_F, K_F) == K_EJCT);
    assert(map(POS_F, K_M) == K_M && map(POS_RGUI, K_RGUI) == K_RGUI);
    boot(SW(1) | SW(2)); // both ON is treated as HHK
    state = FN;
    assert(map(POS_RGUI, K_RGUI) == K_STOP_KB);

    // SW4: left ◇ is Fn for the selected system; SW5 swaps Alt and ◇ on both sides.
    boot(SW(4));
    assert(map(POS_LGUI, K_LGUI) == MO(1) && map(POS_RGUI, K_RGUI) == K_RGUI);
    state = 1 | (1U << 2);
    assert(map(POS_LGUI, K_LGUI) == MO(3));
    boot(SW(5));
    assert(map(POS_LALT, K_LALT) == K_LGUI && map(POS_LGUI, K_LGUI) == K_LALT);
    assert(map(POS_RGUI, K_RGUI) == K_RALT && map(POS_RALT, K_RALT) == K_RGUI);
    assert(map(POS_LGUI, K_SPC) == K_SPC); // a VIA remap is left alone
    boot(SW(4) | SW(5));
    assert(map(POS_LGUI, K_LGUI) == MO(1) && map(POS_LALT, K_LALT) == K_LGUI);

    // Fn+Ctrl+W/M switch modes by key position and are swallowed; Ctrl or Fn alone pass through.
    // The layer state no longer matters: a keymap without MO(1) on Fn still works.
    boot(0);
    assert(!key(POS_CTRL, true));
    assert(!key(POS_M, true) && !key(POS_M, false));
    assert(!key(POS_FN, true));
    assert(hhkb_fn_held() && hhkb_ctrl_held());
    assert(key(POS_M, true));
    assert(!key(POS_FN, false)); // Fn released before M: the release is still swallowed
    assert(key(POS_M, false));
    assert(s_mode == MODE_MAC && map(POS_DEL, K_BSPC) == K_BSPC);
    key(POS_FN, true);
    assert(key(POS_W, true) && key(POS_W, false));
    assert(s_mode == MODE_HHK); // DIPs select HHK, so Fn+Ctrl+W returns to HHK
    assert(!key(POS_CTRL, false) && !hhkb_ctrl_held());
    assert(!key(POS_W, true) && !key(POS_W, false));
    key(POS_FN, false);
    boot(SW(2)); // Mac DIPs: Fn+Ctrl+W selects Win
    key(POS_FN, true);
    key(POS_CTRL, true);
    assert(key(POS_W, true) && key(POS_W, false));
    assert(s_mode == MODE_WIN);
    key(POS_CTRL, false);
    key(POS_FN, false);
    // SW4: the left ◇ is Fn for the chords too; without SW4 it is not.
    assert(!hhkb_fn_held());
    key(POS_LGUI, true);
    assert(!hhkb_fn_held());
    key(POS_LGUI, false);
    boot(SW(4));
    key(POS_LGUI, true);
    key(POS_CTRL, true);
    assert(hhkb_fn_held() && key(POS_M, true) && key(POS_M, false) && s_mode == MODE_MAC);
    key(POS_CTRL, false);
    key(POS_LGUI, false);
    return 0;
}
