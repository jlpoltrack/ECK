#ifndef HHKB_HYBRID_POWER_LOGIC_H
#define HHKB_HYBRID_POWER_LOGIC_H

/* Hardware-free parts of the Hybrid's power handling (battery, power switch, LED blinks,
 * auto power-off), so host tests can run them. Stock behaviour: docs/hybrid-stock-behaviour.md. */

#include <stdbool.h>
#include <stdint.h>

/* ---- Battery: three detector inputs, level 0 (fine) .. 3 (empty) ---- */
#define PWR_BATT_EMPTY 3U
#define PWR_BATT_RECOVER_SAMPLES 10U /* a better level must persist for more than this */

typedef struct {
    uint8_t latched;
    uint8_t better; /* consecutive samples better than latched */
} pwr_batt_filter_t;

/* PB9 low -> 3, else PB8 low -> 2, else PB7 low -> 1, else 0. */
uint8_t pwr_batt_raw_level(bool pb7_low, bool pb8_low, bool pb9_low);
void pwr_batt_filter_init(pwr_batt_filter_t *f, uint8_t level);
/* One 100 ms sample: worse levels latch at once, better ones after the persistence count. */
uint8_t pwr_batt_filter_update(pwr_batt_filter_t *f, uint8_t raw);
/* 100 / 15 / 5 / 0 %, and 100 % while USB powers the keyboard. */
uint8_t pwr_batt_percent(uint8_t level, bool usb_power);

/* ---- Debounce: the output follows the input once it is stable for `samples` reads ---- */
typedef struct {
    bool state;
    uint8_t count;
} pwr_debounce_t;
bool pwr_debounce_update(pwr_debounce_t *d, bool raw, uint8_t samples);

/* ---- Power switch while on (manual p.4, p.12) ---- */
#define PWR_SHORT_MIN_MS 100U
#define PWR_OFF_HOLD_MS 2000U
#define PWR_ON_HOLD_MS 1000U

typedef enum {
    PWR_SW_NONE,
    PWR_SW_SHORT, /* released after 100 ms .. 2 s: show the host slot */
    PWR_SW_OFF,   /* held for 2 s: fires once, while still held */
} pwr_sw_event_t;

typedef struct {
    bool pressed;
    bool fired;
    uint32_t since;
} pwr_switch_t;

/* Feed the debounced switch state; `now` in ms. */
pwr_sw_event_t pwr_switch_update(pwr_switch_t *s, bool pressed, uint32_t now);

/* ---- Power switch while off: what a wake from STOP leads to ---- */
typedef enum {
    PWR_WAKE_WAIT,  /* still held, under 1 s */
    PWR_WAKE_BLUE,  /* held 1 s or more: blue on, power on at release */
    PWR_WAKE_ON,    /* released after 1 s or more */
    PWR_WAKE_SLEEP, /* released early: back to STOP */
} pwr_wake_t;
pwr_wake_t pwr_wake_action(bool pressed, uint32_t held_ms);

/* ---- LED blinks, stepped every 100 ms ---- */
typedef struct {
    uint8_t toggles; /* LED changes left: 2 per blink */
    uint8_t half;    /* steps per on or off phase */
    uint8_t phase;
    bool on;
} pwr_blink_t;

void pwr_blink_start(pwr_blink_t *b, uint8_t count, uint8_t half_steps);
bool pwr_blink_step(pwr_blink_t *b); /* returns the LED state for this step */
bool pwr_blink_busy(const pwr_blink_t *b);

/* Low-battery warning (manual p.3): level 1 one blink every 30 s, level 2-3 two every 15 s.
 * Stock timing: 0.6 s phases, a slow count every 1.1 s, repeats after 30 / 15 counts. */
#define PWR_WARN_HALF_STEPS 6U
#define PWR_WARN_SLOW_STEPS 11U
#define PWR_WARN_LOW_COUNTS 30U
#define PWR_WARN_EMPTY_COUNTS 15U

typedef struct {
    uint8_t level; /* level being warned about, 0 = none */
    uint8_t slow_div;
    uint8_t slow;
    pwr_blink_t blink;
} pwr_warn_t;

/* `active`: on batteries with Bluetooth as the transport. Returns the orange LED state. */
bool pwr_warn_step(pwr_warn_t *w, uint8_t level, bool active);

/* ---- Auto power-off on batteries (manual p.3, p.6 notes *3 and *4, p.7) ---- */
#define PWR_IDLE_OFF_MS (60UL * 60UL * 1000UL) /* deep sleep on batteries, any key wakes */
#define PWR_PAIRING_OFF_MS (10UL * 60UL * 1000UL)
#define PWR_STANDBY_OFF_MS (60UL * 1000UL) /* stock mode timer: 1 min */

typedef struct {
    bool usb_power;
    bool power_saving; /* SW6 OFF */
    bool pairing;      /* pairing mode: advertising to any host */
    bool standby;      /* Bluetooth, not connected, advertising to the slot's host */
    uint32_t idle_ms;  /* since the last key press */
    uint32_t pairing_ms;
    uint32_t standby_ms;
} pwr_auto_t;

bool pwr_auto_off_due(const pwr_auto_t *a);

#endif
