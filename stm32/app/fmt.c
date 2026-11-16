#include "fmt.h"

/* Supports %d %u %x %c %s %% with an optional 'l'; no width or precision.
 * Anything else is copied as written. Returns the untruncated length, like vsnprintf. */
int fmt_vsnprintf(char *buf, size_t size, const char *fmt, va_list ap) {
    size_t n = 0;
#define PUT(ch)                                    \
    do {                                           \
        char put_ = (char)(ch); /* always evaluated */ \
        if (n + 1 < size) buf[n] = put_;           \
        n++;                                       \
    } while (0)
    for (; *fmt; fmt++) {
        if (*fmt != '%') {
            PUT(*fmt);
            continue;
        }
        const char *spec = fmt++;
        bool is_long = *fmt == 'l';
        if (is_long) fmt++;
        char c = *fmt;
        if (c == '%') {
            PUT('%');
        } else if (c == 'c') {
            PUT(va_arg(ap, int));
        } else if (c == 's') {
            const char *s = va_arg(ap, const char *);
            if (!s) s = "(null)";
            while (*s) PUT(*s++);
        } else if (c == 'd' || c == 'u' || c == 'x') {
            unsigned long v;
            if (c == 'd') {
                long sv = is_long ? va_arg(ap, long) : va_arg(ap, int);
                if (sv < 0) {
                    PUT('-');
                    v = 0UL - (unsigned long)sv;
                } else {
                    v = (unsigned long)sv;
                }
            } else {
                v = is_long ? va_arg(ap, unsigned long) : va_arg(ap, unsigned int);
            }
            unsigned base = c == 'x' ? 16U : 10U;
            char digits[sizeof(unsigned long) * 3];
            unsigned i = 0;
            do {
                digits[i++] = "0123456789abcdef"[v % base];
                v /= base;
            } while (v);
            while (i) PUT(digits[--i]);
        } else {
            if (!c) fmt--; /* '%' at the end: stop at the terminator */
            for (const char *p = spec; p <= fmt && *p; p++) PUT(*p);
        }
    }
#undef PUT
    if (size) buf[n < size ? n : size - 1] = '\0';
    return (int)n;
}

int fmt_snprintf(char *buf, size_t size, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = fmt_vsnprintf(buf, size, fmt, ap);
    va_end(ap);
    return n;
}
