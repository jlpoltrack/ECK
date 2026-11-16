#ifdef HHKB_HYBRID
#include "hybrid_power_logic.h"

uint8_t pwr_batt_raw_level(bool pb7_low, bool pb8_low, bool pb9_low) {
    if (pb9_low) return 3;
    if (pb8_low) return 2;
    if (pb7_low) return 1;
    return 0;
}

void pwr_batt_filter_init(pwr_batt_filter_t *f, uint8_t level) {
    f->latched = level;
    f->better = 0;
}

uint8_t pwr_batt_filter_update(pwr_batt_filter_t *f, uint8_t raw) {
    if (raw >= f->latched) {
        f->latched = raw;
        f->better = 0;
    } else if (++f->better > PWR_BATT_RECOVER_SAMPLES) {
        f->latched = raw;
        f->better = 0;
    }
    return f->latched;
}

uint8_t pwr_batt_percent(uint8_t level, bool usb_power) {
    static const uint8_t percent[4] = {100, 15, 5, 0};
    if (usb_power || level > PWR_BATT_EMPTY) return 100;
    return percent[level];
}

bool pwr_debounce_update(pwr_debounce_t *d, bool raw, uint8_t samples) {
    if (raw == d->state) {
        d->count = 0;
    } else if (++d->count >= samples) {
        d->state = raw;
        d->count = 0;
    }
    return d->state;
}

pwr_sw_event_t pwr_switch_update(pwr_switch_t *s, bool pressed, uint32_t now) {
    if (pressed && !s->pressed) {
        s->pressed = true;
        s->fired = false;
        s->since = now;
        return PWR_SW_NONE;
    }
    uint32_t held = now - s->since;
    if (pressed) {
        if (!s->fired && held >= PWR_OFF_HOLD_MS) {
            s->fired = true;
            return PWR_SW_OFF;
        }
        return PWR_SW_NONE;
    }
    if (!s->pressed) return PWR_SW_NONE;
    s->pressed = false;
    return !s->fired && held >= PWR_SHORT_MIN_MS && held < PWR_OFF_HOLD_MS ? PWR_SW_SHORT : PWR_SW_NONE;
}

pwr_wake_t pwr_wake_action(bool pressed, uint32_t held_ms) {
    if (pressed) return held_ms >= PWR_ON_HOLD_MS ? PWR_WAKE_BLUE : PWR_WAKE_WAIT;
    return held_ms >= PWR_ON_HOLD_MS ? PWR_WAKE_ON : PWR_WAKE_SLEEP;
}

void pwr_blink_start(pwr_blink_t *b, uint8_t count, uint8_t half_steps) {
    b->toggles = (uint8_t)(2U * count);
    b->half = half_steps ? half_steps : 1U;
    b->phase = 0;
    b->on = false;
}

bool pwr_blink_busy(const pwr_blink_t *b) {
    return b->toggles != 0;
}

bool pwr_blink_step(pwr_blink_t *b) {
    if (!b->toggles) return b->on = false;
    if (b->phase == 0) b->on = !b->on;
    if (++b->phase >= b->half) {
        b->phase = 0;
        b->toggles--;
    }
    return b->on;
}

bool pwr_warn_step(pwr_warn_t *w, uint8_t level, bool active) {
    if (!active || level == 0) {
        w->level = 0;
        w->blink.toggles = 0;
        return w->blink.on = false;
    }
    if (level != w->level) { /* new or changed warning: blink at once, then on the slow count */
        w->level = level;
        w->slow_div = 0;
        w->slow = 0;
        pwr_blink_start(&w->blink, level == 1 ? 1 : 2, PWR_WARN_HALF_STEPS);
    }
    if (++w->slow_div >= PWR_WARN_SLOW_STEPS) {
        w->slow_div = 0;
        if (++w->slow >= (level == 1 ? PWR_WARN_LOW_COUNTS : PWR_WARN_EMPTY_COUNTS)) {
            w->slow = 0;
            pwr_blink_start(&w->blink, level == 1 ? 1 : 2, PWR_WARN_HALF_STEPS);
        }
    }
    return pwr_blink_step(&w->blink);
}

static uint32_t min_u32(uint32_t a, uint32_t b) {
    return a < b ? a : b;
}

bool pwr_auto_off_due(const pwr_auto_t *a) {
    if (a->usb_power || !a->power_saving) return false;
    if (a->idle_ms >= PWR_IDLE_OFF_MS) return true;
    if (a->pairing && min_u32(a->idle_ms, a->pairing_ms) >= PWR_PAIRING_OFF_MS) return true;
    return a->standby && !a->pairing && min_u32(a->idle_ms, a->standby_ms) >= PWR_STANDBY_OFF_MS;
}
#endif
