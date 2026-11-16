/* Hybrid Bluetooth chords by key position (stm32/hybrid/hybrid_chords.c), and the positions
 * against the default keymap: the documented chords hit the same keys as before. */
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include "keys.h"
#define DEFINE_KEYMAP
#include "config.h"
#include "hhkb_positions.h"
#include "hybrid_chords.h"

int main(void) {
    // Default keymap: these positions hold the keys the old keycode-based chords looked for.
    assert(keymap[0][POS_FN] == MO(1) && keymap[2][POS_FN] == MO(3));
    assert(keymap[0][POS_CTRL] == K_LCTL && keymap[0][POS_DEL] == K_BSPC);
    for (int l = 1; l <= 3; l += 2) {
        assert(keymap[l][POS_1] == K_F1 && keymap[l][POS_4] == K_F4 && keymap[l][POS_0] == K_F10);
        assert(keymap[l][POS_Q] == K_TRNS && keymap[l][POS_X] == K_TRNS && keymap[l][POS_Z] == K_TRNS);
    }
    assert(keymap[0][POS_Q] == K_Q && keymap[0][POS_X] == K_X && keymap[0][POS_Z] == K_Z);
    assert(keymap[0][POS_W] == K_W && keymap[0][POS_M] == K_M);

    uint8_t slot = 0;
    // Fn+Ctrl+1..4: select; in standby pair; with Delete held forget.
    assert(hybrid_chord(POS_1, true, false, false, false, &slot) == CHORD_SELECT && slot == 1);
    assert(hybrid_chord(POS_4, true, false, false, false, &slot) == CHORD_SELECT && slot == 4);
    assert(hybrid_chord(POS_1 + 1, true, false, true, false, &slot) == CHORD_PAIR && slot == 2);
    assert(hybrid_chord(POS_1 + 2, true, true, true, false, &slot) == CHORD_DELETE && slot == 3);
    assert(hybrid_chord(POS_1 + 2, true, true, false, false, &slot) == CHORD_SELECT); // Delete needs standby
    // Fn+Ctrl+0: USB. Fn+Ctrl+other keys: nothing.
    assert(hybrid_chord(POS_0, true, false, false, false, &slot) == CHORD_USB);
    assert(hybrid_chord(POS_1 + 4, true, false, false, false, &slot) == CHORD_NONE); // 5
    assert(hybrid_chord(POS_Q, true, false, false, false, &slot) == CHORD_NONE);
    // Fn+Q standby, Fn+X cancels only while pairing or in standby, Fn+Z+Delete in standby clears.
    assert(hybrid_chord(POS_Q, false, false, false, false, &slot) == CHORD_STANDBY);
    assert(hybrid_chord(POS_X, false, false, false, false, &slot) == CHORD_NONE);
    assert(hybrid_chord(POS_X, false, false, true, false, &slot) == CHORD_CANCEL);
    assert(hybrid_chord(POS_X, false, false, false, true, &slot) == CHORD_CANCEL);
    assert(hybrid_chord(POS_Z, false, true, true, false, &slot) == CHORD_CLEAR_ALL);
    assert(hybrid_chord(POS_Z, false, false, true, false, &slot) == CHORD_NONE);
    assert(hybrid_chord(POS_Z, false, true, false, false, &slot) == CHORD_NONE);
    // Fn+1 without Ctrl is F1 for the host.
    assert(hybrid_chord(POS_1, false, false, false, false, &slot) == CHORD_NONE);
    puts("hybrid chord tests passed");
    return 0;
}
