/* Rapid Trigger against recorded sensor noise: how often a key held at 60 % depth toggles.
 * Record: tools/bench/key_depth.py record 20 rest.json (hands off). Run from the repo root:
 *   cc -O2 -I stm32/ec -o /tmp/rt_sim tools/bench/rt_noise_sim.c stm32/ec/rapid_trigger.c
 *   python3 -c "import json;v=json.load(open('rest.json'));print(len(v),len(v[0]));
 *      [print(*r) for r in v]" | /tmp/rt_sim
 * The range is 2400 counts (a full press on the Hybrid); key 0 (c0r0) is skipped. */
#include <stdio.h>
#include "rapid_trigger.h"
/* stdin: "samples keys", then one row of raw readings per sample */
int main(void) {
    int n, keys;
    if (scanf("%d %d", &n, &keys) != 2 || n < 1 || n > 20000 || keys < 2 || keys > 16) {
        fprintf(stderr, "expected at most 20000 samples of 2-16 keys\n");
        return 1;
    }
    static int raw[20000][16]; long sum[16] = {0};
    for (int i = 0; i < n; i++) for (int k = 0; k < keys; k++) { scanf("%d", &raw[i][k]); sum[k] += raw[i][k]; }
    int sens[] = {25, 50, 75, 100, 125, 150};
    printf("sensitivity(permille)  false toggles per key in %d scans (key held at 60%%)\n", n);
    for (int s = 0; s < 6; s++) {
        rt_cfg_t c; rt_cfg_defaults(&c); c.rt_enabled = true; c.press_pm = c.release_pm = (uint16_t)sens[s];
        printf("%4d:", sens[s]);
        for (int k = 1; k < keys; k++) {
            int base = (int)(sum[k] / n); rt_key_t key; rt_key_reset(&key); rt_key_set_range(&key, (uint16_t)base, (uint16_t)(base + 2400));
            for (int i = 0; i < 50; i++) rt_update(&key, &c, (uint16_t)(base + 1440)); /* press and hold */
            int toggles = 0; bool prev = true;
            for (int i = 0; i < n; i++) {
                bool d = rt_update(&key, &c, (uint16_t)(base + 1440 + raw[i][k] - base));
                toggles += d != prev; prev = d;
            }
            printf(" %3d", toggles);
        }
        printf("\n");
    }
    return 0;
}
