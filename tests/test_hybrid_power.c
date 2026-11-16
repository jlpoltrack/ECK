#define HHKB_HYBRID 1
#include <assert.h>
#include "hybrid_power_logic.c"

static void test_battery(void) {
    assert(pwr_batt_raw_level(false, false, false) == 0);
    assert(pwr_batt_raw_level(true, false, false) == 1);
    assert(pwr_batt_raw_level(true, true, false) == 2);
    assert(pwr_batt_raw_level(false, false, true) == 3); // PB9 wins
    assert(pwr_batt_percent(0, false) == 100 && pwr_batt_percent(1, false) == 15);
    assert(pwr_batt_percent(2, false) == 5 && pwr_batt_percent(3, false) == 0);
    assert(pwr_batt_percent(3, true) == 100); // USB power

    pwr_batt_filter_t f;
    pwr_batt_filter_init(&f, 0);
    assert(pwr_batt_filter_update(&f, 2) == 2); // worse latches at once
    for (int i = 0; i < 10; i++) assert(pwr_batt_filter_update(&f, 0) == 2);
    assert(pwr_batt_filter_update(&f, 0) == 0); // better after more than 10 samples
    for (int i = 0; i < 8; i++) pwr_batt_filter_update(&f, 1);
    assert(f.latched == 1);
    pwr_batt_filter_init(&f, 3);
    for (int i = 0; i < 6; i++) pwr_batt_filter_update(&f, 1);
    pwr_batt_filter_update(&f, 3); // a relapse restarts the count
    for (int i = 0; i < 10; i++) assert(pwr_batt_filter_update(&f, 1) == 3);
    assert(pwr_batt_filter_update(&f, 1) == 1);
}

static void test_debounce(void) {
    pwr_debounce_t d = {0};
    assert(!pwr_debounce_update(&d, true, 2));
    assert(pwr_debounce_update(&d, true, 2));
    assert(pwr_debounce_update(&d, false, 2)); // one glitch is ignored
    assert(pwr_debounce_update(&d, true, 2));
    assert(pwr_debounce_update(&d, false, 2));
    assert(!pwr_debounce_update(&d, false, 2));
}

static void test_switch(void) {
    pwr_switch_t s = {0};
    // Tap under 100 ms: nothing.
    assert(pwr_switch_update(&s, true, 1000) == PWR_SW_NONE);
    assert(pwr_switch_update(&s, false, 1050) == PWR_SW_NONE);
    // Short press.
    assert(pwr_switch_update(&s, true, 2000) == PWR_SW_NONE);
    assert(pwr_switch_update(&s, true, 2500) == PWR_SW_NONE);
    assert(pwr_switch_update(&s, false, 2600) == PWR_SW_SHORT);
    assert(pwr_switch_update(&s, false, 2700) == PWR_SW_NONE);
    // 2 s hold: OFF fires once while held, release adds nothing.
    assert(pwr_switch_update(&s, true, 5000) == PWR_SW_NONE);
    assert(pwr_switch_update(&s, true, 6999) == PWR_SW_NONE);
    assert(pwr_switch_update(&s, true, 7000) == PWR_SW_OFF);
    assert(pwr_switch_update(&s, true, 9000) == PWR_SW_NONE);
    assert(pwr_switch_update(&s, false, 9100) == PWR_SW_NONE);
    // Tick counter wrap.
    assert(pwr_switch_update(&s, true, 0xFFFFFF00U) == PWR_SW_NONE);
    assert(pwr_switch_update(&s, true, 0x00000800U) == PWR_SW_OFF);

    assert(pwr_wake_action(true, 999) == PWR_WAKE_WAIT);
    assert(pwr_wake_action(true, 1000) == PWR_WAKE_BLUE);
    assert(pwr_wake_action(false, 999) == PWR_WAKE_SLEEP);
    assert(pwr_wake_action(false, 1000) == PWR_WAKE_ON);
}

static int count_blinks(pwr_blink_t *b, int steps) {
    int rises = 0;
    bool last = false;
    for (int i = 0; i < steps; i++) {
        bool on = pwr_blink_step(b);
        if (on && !last) rises++;
        last = on;
    }
    return rises;
}

static void test_blink(void) {
    pwr_blink_t b = {0};
    assert(!pwr_blink_step(&b) && !pwr_blink_busy(&b));
    pwr_blink_start(&b, 3, 3);
    assert(pwr_blink_busy(&b));
    // 3 steps on, 3 off, three times.
    bool seq[18];
    for (int i = 0; i < 18; i++) seq[i] = pwr_blink_step(&b);
    for (int i = 0; i < 18; i++) assert(seq[i] == ((i % 6) < 3));
    assert(!pwr_blink_busy(&b) && !pwr_blink_step(&b));
    pwr_blink_start(&b, 4, 3);
    assert(count_blinks(&b, 100) == 4);
}

/* Orange blinks starting in `steps` 100 ms steps at a fixed level. */
static bool warn_last;
static int warn_blinks(pwr_warn_t *w, uint8_t level, bool active, int steps) {
    int rises = 0;
    for (int i = 0; i < steps; i++) {
        bool on = pwr_warn_step(w, level, active);
        if (on && !warn_last) rises++;
        warn_last = on;
    }
    return rises;
}

static void test_warn(void) {
    pwr_warn_t w = {0};
    assert(warn_blinks(&w, 0, true, 1000) == 0);
    assert(warn_blinks(&w, 2, false, 1000) == 0); // USB power or USB transport
    // Level 1: once at once, then every 30 slow counts (33 s).
    assert(warn_blinks(&w, 1, true, 1) == 1);
    assert(warn_blinks(&w, 1, true, 328) == 0);
    assert(warn_blinks(&w, 1, true, 330) == 1);
    // Level 2: changing level blinks twice at once, then twice every 15 counts (16.5 s).
    assert(warn_blinks(&w, 2, true, 24) == 2);
    assert(warn_blinks(&w, 2, true, 165 - 24) == 1); // the repeat starts on step 165
    assert(warn_blinks(&w, 0, true, 1) == 0); // batteries replaced
    assert(warn_blinks(&w, 3, true, 1649) == 2 + 2 * 9);
    assert(!pwr_warn_step(&w, 3, false) && w.level == 0);
}

static void test_auto_off(void) {
    pwr_auto_t a = {.power_saving = true};
    assert(!pwr_auto_off_due(&a));
    a.idle_ms = PWR_IDLE_OFF_MS;
    assert(pwr_auto_off_due(&a));
    a.usb_power = true;
    assert(!pwr_auto_off_due(&a)); // never on USB power
    a.usb_power = false;
    a.power_saving = false;
    assert(!pwr_auto_off_due(&a)); // SW6 ON: never
    a.power_saving = true;

    a.idle_ms = 9UL * 60UL * 1000UL;
    a.pairing = true;
    a.pairing_ms = PWR_PAIRING_OFF_MS;
    assert(!pwr_auto_off_due(&a)); // a key press within 10 min keeps pairing alive
    a.idle_ms = PWR_PAIRING_OFF_MS;
    assert(pwr_auto_off_due(&a));
    a.pairing_ms = 1000;
    assert(!pwr_auto_off_due(&a));

    a.pairing = false;
    a.standby = true;
    a.idle_ms = PWR_STANDBY_OFF_MS;
    a.standby_ms = PWR_STANDBY_OFF_MS - 1;
    assert(!pwr_auto_off_due(&a));
    a.standby_ms = PWR_STANDBY_OFF_MS;
    assert(pwr_auto_off_due(&a)); // host not there for a minute
    a.idle_ms = 100;
    assert(!pwr_auto_off_due(&a));
}

int main(void) {
    test_battery();
    test_debounce();
    test_switch();
    test_blink();
    test_warn();
    test_auto_off();
    return 0;
}
