#ifndef FMT_H
#define FMT_H

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Small snprintf for status and debug text: %d %u %x %c %s %%, optional 'l'.
 * Avoids newlib's printf (~2 KB flash, stdio and malloc) on small MCUs. */
int fmt_vsnprintf(char *buf, size_t size, const char *fmt, va_list ap);
int fmt_snprintf(char *buf, size_t size, const char *fmt, ...);

#ifdef __cplusplus
}
#endif

#endif
