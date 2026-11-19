#ifndef HYBRID_CHORDS_H
#define HYBRID_CHORDS_H

#include <stdbool.h>
#include <stdint.h>

/* Hybrid Bluetooth chords, all Fn+Ctrl (PFU's Q, X and Z+Delete take Fn only): 1..4 select a host
 * slot, 0 USB; Q pairing standby, then 1..4 pairs, Delete+1..4 forgets a host and Delete+Z forgets
 * all; X cancels. */
typedef enum {
    CHORD_NONE,
    CHORD_SELECT,    /* *slot */
    CHORD_PAIR,      /* *slot */
    CHORD_DELETE,    /* *slot */
    CHORD_USB,
    CHORD_STANDBY,
    CHORD_CANCEL,
    CHORD_CLEAR_ALL,
} hybrid_chord_t;

/* Chord for the key at flat index pos pressed while Fn is held (by position, not keycode). */
hybrid_chord_t hybrid_chord(int pos, bool ctrl, bool delete_held, bool standby, bool pairing, uint8_t *slot);

#endif
