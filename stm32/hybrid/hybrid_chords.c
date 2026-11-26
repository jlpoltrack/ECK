/* Pure chord decision for hybrid_ble.c; host-tested (tests/test_hybrid_chords.c). */
#include "hybrid_chords.h"
#include "hhkb_positions.h"

hybrid_chord_t hybrid_chord(int pos, bool ctrl, bool delete_held, bool standby, bool pairing, uint8_t *slot) {
    if (!ctrl) return CHORD_NONE;
    if (pos >= POS_1 && pos <= POS_4) {
        *slot = (uint8_t)(pos - POS_1 + 1);
        if (standby) return delete_held ? CHORD_DELETE : CHORD_PAIR;
        return CHORD_SELECT;
    }
    if (pos == POS_0) return CHORD_USB;
    if (pos == POS_Q) return CHORD_STANDBY;
    if (pos == POS_U) return CHORD_ARM_UPDATE;
    if (pos == POS_X) return (standby || pairing) ? CHORD_CANCEL : CHORD_NONE;
    if (pos == POS_Z && standby && delete_held) return CHORD_CLEAR_ALL;
    return CHORD_NONE;
}
