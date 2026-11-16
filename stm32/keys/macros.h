#ifndef MACROS_H
#define MACROS_H

#include "FreeRTOS.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Process a macro keycode (0xC0 - 0xDF). Returns true if handled.
bool macros_process_key(uint32_t key, bool pressed);

// Periodic check for active non-blocking macro playback delay steps
TickType_t macros_check_timeouts(TickType_t now);

// Returns true if a macro is currently playing back
bool macros_has_active(void);

#ifdef __cplusplus
}
#endif

#endif // MACROS_H
