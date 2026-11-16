#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include "fmt.h"

static char buf[64];

#define CHECK(expect, ...)                                           \
    do {                                                             \
        int n = fmt_snprintf(buf, sizeof buf, __VA_ARGS__);          \
        assert(strcmp(buf, expect) == 0);                            \
        assert(n == (int)strlen(expect));                            \
    } while (0)

int main(void) {
    CHECK("plain", "plain");
    CHECK("C3 R1: raw=2370 DN", "C%u R%u: raw=%u %s", 3u, 1u, 2370u, "DN");
    CHECK("rate=1000 Hz", "rate=%lu Hz", 1000UL);
    CHECK("-42 0 2147483647", "%d %d %d", -42, 0, INT_MAX);
    CHECK("-2147483648", "%d", INT_MIN);
    CHECK("4294967295", "%u", UINT_MAX);
    CHECK("-7", "%ld", -7L);
    CHECK("ff 0", "%x %lx", 255u, 0UL);
    CHECK("x=A", "x=%c", 'A');
    CHECK("50%", "%u%%", 50u);
    CHECK("(null)", "%s", (const char *)0);
    CHECK("%q %", "%q %"); /* unsupported and trailing '%' are copied */

    /* Truncation: stays terminated and returns the full length. */
    char small[5];
    assert(fmt_snprintf(small, sizeof small, "Travel: %u", 123u) == 11);
    assert(strcmp(small, "Trav") == 0);
    assert(fmt_snprintf(NULL, 0, "%u", 12345u) == 5);
    assert(fmt_snprintf(small, sizeof small, "%s|%c|%u", "longer than the buffer", 'z', 7u) == 26);
    assert(strcmp(small, "long") == 0);

    puts("fmt tests passed");
    return 0;
}
