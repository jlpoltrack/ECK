/* Host test; run via tests/run_all_tests.sh. */
#include "rapid_trigger.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#define BASE 800
#define PEAK 1800                       /* range 1000 counts -> 1 count == 1 permille */

static rt_key_t key;
static rt_cfg_t cfg;

static uint16_t raw(int pm) { return (uint16_t)(BASE + pm); }
static bool feed(int pm) { return rt_update(&key, &cfg, raw(pm)); }

static void setup(bool rt) {
    rt_cfg_defaults(&cfg);
    cfg.rt_enabled = rt;
    rt_key_set_range(&key, BASE, PEAK, cfg.reset_pm);
    rt_key_reset(&key);
}

int main(void) {
    int events;

    /* 1. Slow press: nothing before act point (750), pressed right at it. */
    setup(true);
    for (int pm = 0; pm < 750; pm += 5) assert(!feed(pm));
    assert(feed(750));

    /* 2. Mid-travel release: 20 up is ignored, 30 up releases. */
    for (int pm = 750; pm <= 1000; pm += 10) assert(feed(pm));
    assert(feed(980));
    assert(feed(971));
    assert(!feed(970));

    /* 3. Re-press without returning to the top: 20 down ignored, 30 down presses. */
    assert(!feed(980));
    assert(!feed(990));
    assert(feed(1000));

    /* 4. Rapid wiggle in 800..850 produces many transitions. */
    setup(true);
    for (int pm = 0; pm <= 850; pm += 10) feed(pm);
    events = 0;
    bool prev = feed(850);
    for (int i = 0; i < 10; i++) {
        for (int pm = 850; pm >= 800; pm -= 5) { bool s = feed(pm); if (s != prev) { events++; prev = s; } }
        for (int pm = 800; pm <= 850; pm += 5) { bool s = feed(pm); if (s != prev) { events++; prev = s; } }
    }
    assert(events == 20);

    /* 5. Going to the top dead zone disarms RT; partial press below act does not trigger. */
    feed(40);
    assert(!feed(400));
    assert(!feed(749));
    assert(feed(750));

    /* 6. Noise +-8 around a held key does not chatter (needs 30 drop from max). */
    setup(true);
    for (int pm = 0; pm <= 900; pm += 10) feed(pm);
    srand(1);
    for (int i = 0; i < 100000; i++) assert(feed(900 + (rand() % 17) - 8));

    /* 7. Noise +-8 below the actuation point never presses. */
    setup(true);
    for (int i = 0; i < 100000; i++) assert(!feed(720 + (rand() % 17) - 8));

    /* 8. Classic mode: fixed act 750, hysteresis = release_pm (30). */
    setup(false);
    assert(!feed(749));
    assert(feed(750));
    assert(feed(721));
    assert(!feed(719));
    assert(!feed(730));
    assert(feed(750));

    /* 9. Calibration fallback: peak <= base+150 does not divide by ~0. */
    rt_key_set_range(&key, BASE, BASE + 10, cfg.reset_pm);
    assert(rt_normalize(&key, BASE + 500) == 500);

    /* 10. reset_raw is the exact reset-zone boundary, so the scan's resting-key shortcut
     * (raw <= reset_raw: up, unlatched) matches rt_update for any range and reset depth. */
    srand(1);
    for (int i = 0; i < 20000; i++) {
        uint16_t base = (uint16_t)(100 + rand() % 1900);
        uint16_t peak = (uint16_t)(base + rand() % 3000);
        uint16_t reset_pm = (uint16_t)(25 * (rand() % 7));
        rt_key_t k;
        rt_key_set_range(&k, base, peak, reset_pm);
        assert(rt_normalize(&k, k.reset_raw) <= reset_pm);
        assert(k.reset_raw == UINT16_MAX || rt_normalize(&k, (uint16_t)(k.reset_raw + 1)) > reset_pm);
        rt_cfg_t c;
        rt_cfg_defaults(&c);
        c.reset_pm = reset_pm;
        c.rt_enabled = rand() & 1;
        for (int d = -3; d <= 3; d++) {
            int r = k.reset_raw + d;
            if (r < 0 || r > UINT16_MAX) continue;
            rt_key_t probe = k;
            probe.latched = probe.down = true; /* worst case: was pressed */
            bool pressed = rt_update(&probe, &c, (uint16_t)r);
            assert(d <= 0 ? (!pressed && !probe.latched) : 1);
        }
        rt_key_set_reset(&k, (uint16_t)(reset_pm + 25));
        assert(rt_normalize(&k, k.reset_raw) <= reset_pm + 25);
    }

    puts("rapid trigger: all tests passed");
    return 0;
}
