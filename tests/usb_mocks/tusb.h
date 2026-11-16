#include <stdbool.h>
#include <stdint.h>
bool tud_mounted(void);
bool tud_suspended(void);
bool tud_hid_ready(void);
bool tud_remote_wakeup(void);
bool tud_hid_keyboard_report(uint8_t id, uint8_t mods, const uint8_t keys[6]);
bool tud_hid_report(uint8_t id, const void *report, uint16_t len);
void tud_task_ext(uint32_t timeout, bool in_isr);
bool tusb_init(void);
