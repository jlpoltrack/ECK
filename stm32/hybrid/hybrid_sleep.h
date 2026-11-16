#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Battery mode only: start LPTIM1 on LSI and measure it (task context, takes 250 ms). */
void hybrid_sleep_init(void);
/* Deep sleep: LPTIM1 wakes the core from STOP after ms (false before hybrid_sleep_init). */
bool hybrid_sleep_wake_after(uint32_t ms);
/* True (and cleared) when that wake-up was the timer's. */
bool hybrid_sleep_timer_fired(void);
/* The matrix task's idle stage: allow STOP between scans. */
void hybrid_sleep_set_idle(bool idle);
/* portSUPPRESS_TICKS_AND_SLEEP for Hybrid builds. */
void hybrid_sleep(uint32_t expected_ticks);
