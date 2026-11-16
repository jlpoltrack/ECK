#ifndef HYBRID_CHORDS_H
#define HYBRID_CHORDS_H

#include <stdbool.h>
#include <stdint.h>

/* Stock Hybrid Bluetooth chords (PFU manual), all with Fn held: Ctrl+1..4 select a host slot,
 * Ctrl+0 USB; Q pairing standby, then Ctrl+1..4 pairs, Ctrl+Delete+1..4 forgets a host and
 * Z+Delete forgets all; X cancels. */
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
