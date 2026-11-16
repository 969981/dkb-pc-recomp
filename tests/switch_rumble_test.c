// runtime/switch_rumble.c: neutral value and a non-zero case checked against SDL's tables.
#include <stdio.h>
#include <string.h>
#include "switch_rumble.h"

static int fails;
static void expect(const char *what, const uint8_t *got, const uint8_t *want, int n) {
    if (memcmp(got, want, n)) {
        fails++;
        printf("FAIL %s:", what);
        for (int i = 0; i < n; i++) printf(" %02x", got[i]);
        printf("\n");
    }
}

int main(void) {
    uint8_t o[4], p[10];
    const float neutral[4] = { 0, 160, 0, 320 };
    switch_rumble_encode(neutral, o);
    expect("neutral", o, (const uint8_t[]){ 0x00, 0x01, 0x40, 0x40 }, 4);
    // 320 Hz / 160 Hz at full amplitude: hf 0x100, hfa[100] = 0xc8, lf 0x40, lfa[100] = 0x0072
    const float full[4] = { 1, 160, 1, 320 };
    switch_rumble_encode(full, o);
    expect("full", o, (const uint8_t[]){ 0x00, 0xc9, 0x40, 0x72 }, 4);
    // amplitude 0.5 (32768): hfa entry 32861 -> 0x88, lfa entry 32861 -> 0x0062; 640 Hz low band: code 192 clamps to 191 -> 0x7f
    const float mid[4] = { 0.5f, 640, 0.5f, 320 };
    switch_rumble_encode(mid, o);
    expect("mid", o, (const uint8_t[]){ 0x00, 0x89, 0x7f, 0x62 }, 4);
    const float pk[2][4] = { { 0, 160, 0, 320 }, { 1, 160, 1, 320 } };
    switch_rumble_packet(pk, p);
    expect("packet", p, (const uint8_t[]){ 0x10, 0, 0, 1, 0x40, 0x40, 0, 0xc9, 0x40, 0x72 }, 10);
    printf(fails ? "switch_rumble: %d failed\n" : "switch_rumble: ok\n", fails);
    return fails != 0;
}
