// Unit tests for Switch 2 controller radial stick calibration and circular clamping (runtime/switch2_proto.h:
// s2_cal_apply, s2_pad_input, s2_joy_stick). No SDL, Bluetooth, or game files required.
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "switch2_proto.h"

static const double PI = 3.14159265358979323846;

static double wrap_deg(double a) {
    while (a > 180.0) a -= 360.0;
    while (a <= -180.0) a += 360.0;
    return a;
}

static uint16_t raw_coord(float centre, float up, float down, double dir, float scale) {
    double v = centre + dir * (dir >= 0.0 ? up : down) * scale;
    if (v < 0.0) v = 0.0;
    if (v > 4095.0) v = 4095.0;
    return (uint16_t)lrint(v);
}

static void cal_params(const s2_cal *cal, s2_cal *eff) {
    if (cal && cal->valid) *eff = *cal;
    else s2_cal_default(eff);
}

static void check_cal_sweep(const s2_cal *cal, float dead, const char *label) {
    s2_cal eff;
    cal_params(cal, &eff);
    const float scales[2] = {0.65f, 1.15f};
    for (int si = 0; si < 2; si++) {
        double prev_ang = 0.0;
        for (int deg = 0; deg <= 360; deg += 5) {
            double rad = (double)deg * PI / 180.0;
            uint16_t rx = raw_coord(eff.cx, eff.maxx, eff.minx, cos(rad), scales[si]);
            uint16_t ry = raw_coord(eff.cy, eff.maxy, eff.miny, sin(rad), scales[si]);
            float x = 0.f, y = 0.f;
            s2_cal_apply(cal, rx, ry, dead, &x, &y);
            assert(isfinite(x) && isfinite(y));
            float r = hypotf(x, y);
            if (!(r > 0.1f && r <= 1.0001f)) {
                fprintf(stderr, "FAIL %s scale %.2f deg %d: r=%.5f (x=%.5f y=%.5f)\n",
                        label, scales[si], deg, r, x, y);
                abort();
            }
            double ang = atan2((double)y, (double)x) * (180.0 / PI);
            if (deg > 0) {
                double d = wrap_deg(ang - prev_ang);
                if (!(d > 1.0 && d < 15.0)) {
                    fprintf(stderr, "FAIL %s scale %.2f deg %d: non-monotonic delta %.4f (prev=%.3f now=%.3f)\n",
                            label, scales[si], deg, d, prev_ang, ang);
                    abort();
                }
            }
            prev_ang = ang;
        }
    }
}

static void test_s2_cal_apply(void) {
    /* 1. Default calibration (NULL and invalid). */
    check_cal_sweep(NULL, S2_DEADZONE, "default-null");
    s2_cal inv = {0};
    check_cal_sweep(&inv, S2_DEADZONE, "default-invalid");

    /* 2. Symmetric and asymmetric calibrations. */
    s2_cal sym = {.cx = 2048.f, .cy = 2048.f, .maxx = 1300.f, .minx = 1300.f, .maxy = 1300.f, .miny = 1300.f, .valid = 1};
    s2_cal asym = {.cx = 1980.f, .cy = 2110.f, .maxx = 1420.f, .minx = 1150.f, .maxy = 1180.f, .miny = 1360.f, .valid = 1};
    check_cal_sweep(&sym, S2_DEADZONE, "symmetric");
    check_cal_sweep(&asym, S2_DEADZONE, "asymmetric");

    /* 3. Radial deadzone interior vs diagonal outside axial threshold. */
    float x, y;
    s2_cal_apply(&sym, 2048, 2048, S2_DEADZONE, &x, &y);
    assert(x == 0.f && y == 0.f);
    s2_cal_apply(&sym, 2048 + 25, 2048 - 25, S2_DEADZONE, &x, &y);   // hypot(25, 25) = 35.35 < 40
    assert(x == 0.f && y == 0.f);
    s2_cal_apply(&sym, 2048 + 35, 2048 + 35, S2_DEADZONE, &x, &y);   // hypot(35, 35) = 49.50 > 40 (radial, not axial)
    assert(x > 0.f && y > 0.f && hypotf(x, y) <= 1.0f);

    /* 4. Cardinal full travel reaches +/-1.0; diagonal overtravel clamps circularly to radius 1.0. */
    s2_cal_apply(&sym, 2048 + 1300, 2048, S2_DEADZONE, &x, &y);
    assert(fabsf(x - 1.f) < 1e-5f && fabsf(y) < 1e-5f);
    s2_cal_apply(&sym, 2048, 2048 - 1300, S2_DEADZONE, &x, &y);
    assert(fabsf(x) < 1e-5f && fabsf(y + 1.f) < 1e-5f);
    s2_cal_apply(&sym, 2048 + 1300, 2048 + 1300, S2_DEADZONE, &x, &y);
    assert(fabsf(hypotf(x, y) - 1.f) < 1e-5f && fabsf(x - y) < 1e-5f);

    /* 5. Edge cases: zero spans, dead >= min_span, negative deadzone, raw 0..4095 extremes. */
    s2_cal zpartial = {.cx = 2048.f, .cy = 2048.f, .maxx = 0.f, .minx = 1000.f, .maxy = 1000.f, .miny = 0.f, .valid = 1};
    s2_cal_apply(&zpartial, 3000, 3000, S2_DEADZONE, &x, &y);
    assert(isfinite(x) && isfinite(y) && hypotf(x, y) <= 1.0001f);
    s2_cal zall = {.cx = 2048.f, .cy = 2048.f, .maxx = 0.f, .minx = 0.f, .maxy = 0.f, .miny = 0.f, .valid = 1};
    s2_cal_apply(&zall, 4095, 0, S2_DEADZONE, &x, &y);
    assert(isfinite(x) && isfinite(y) && x == 0.f && y == 0.f);

    /* dead == min_span and dead > min_span clamp d0 to 0.99 without dividing by zero. */
    s2_cal_apply(&sym, 2048 + 1000, 2048, 1300.f, &x, &y);
    assert(isfinite(x) && isfinite(y) && x == 0.f && y == 0.f);
    s2_cal_apply(&sym, 2048 + 1300, 2048, 1300.f, &x, &y);
    assert(isfinite(x) && fabsf(x - 1.f) < 1e-4f && fabsf(y) < 1e-5f);
    s2_cal_apply(&sym, 3000, 2048, 5000.f, &x, &y);
    assert(isfinite(x) && isfinite(y) && x == 0.f && y == 0.f);
    s2_cal_apply(&sym, 2048 + 1300, 2048, 5000.f, &x, &y);
    assert(isfinite(x) && fabsf(x - 1.f) < 1e-4f);

    /* Negative deadzone behaves as zero deadzone. */
    s2_cal_apply(&sym, 2048 + 650, 2048, -10.f, &x, &y);
    assert(isfinite(x) && fabsf(x - 0.5f) < 1e-5f && fabsf(y) < 1e-5f);

    /* Raw 12-bit corners clamp within unit circle. */
    const uint16_t corners[4][2] = {{0, 0}, {0, 4095}, {4095, 0}, {4095, 4095}};
    for (int i = 0; i < 4; i++) {
        s2_cal_apply(&asym, corners[i][0], corners[i][1], S2_DEADZONE, &x, &y);
        assert(isfinite(x) && isfinite(y) && hypotf(x, y) <= 1.0001f);
    }
}

static void check_int_stick(int32_t sx, int32_t sy, int deg, double *prev_ang, const char *label) {
    assert(sx >= -DM_STICK_MAX && sx <= DM_STICK_MAX);
    assert(sy >= -DM_STICK_MAX && sy <= DM_STICK_MAX);
    double r = hypot((double)sx, (double)sy) / (double)DM_STICK_MAX;
    if (!(r > 0.1 && r <= 1.001)) {
        fprintf(stderr, "FAIL %s deg %d: r=%.5f (sx=%d sy=%d)\n", label, deg, r, sx, sy);
        abort();
    }
    double ang = atan2((double)sy, (double)sx) * (180.0 / PI);
    if (deg > 0) {
        double d = wrap_deg(ang - *prev_ang);
        if (!(d > 1.0 && d < 15.0)) {
            fprintf(stderr, "FAIL %s deg %d: non-monotonic delta %.4f (prev=%.3f now=%.3f)\n",
                    label, deg, d, *prev_ang, ang);
            abort();
        }
    }
    *prev_ang = ang;
}

static void test_s2_pad_input(void) {
    s2_cal sym = {.cx = 2048.f, .cy = 2048.f, .maxx = 1300.f, .minx = 1300.f, .maxy = 1300.f, .miny = 1300.f, .valid = 1};
    s2_cal asym = {.cx = 1980.f, .cy = 2110.f, .maxx = 1420.f, .minx = 1150.f, .maxy = 1180.f, .miny = 1360.f, .valid = 1};

    /* Empty pad returns 0 and zeroes sticks. */
    s2_pad_state empty = {0};
    uint64_t btn = 99;
    int32_t st[4] = {1, 2, 3, 4};
    assert(s2_pad_input(&empty, &btn, st) == 0);
    assert(st[0] == 0 && st[1] == 0 && st[2] == 0 && st[3] == 0);

    /* Pro Controller 2: 360-degree sweep on both sticks simultaneously (symmetric left, asymmetric right). */
    s2_pad_state pro = {0};
    pro.pro.have = 1;
    pro.pro.cal[0] = sym;
    pro.pro.cal[1] = asym;
    double prev_l = 0.0, prev_r = 0.0;
    for (int deg = 0; deg <= 360; deg += 5) {
        double rad_l = (double)deg * PI / 180.0;
        double rad_r = (double)(deg + 37) * PI / 180.0;
        pro.pro.r.lx = raw_coord(sym.cx, sym.maxx, sym.minx, cos(rad_l), 1.15f);
        pro.pro.r.ly = raw_coord(sym.cy, sym.maxy, sym.miny, sin(rad_l), 1.15f);
        pro.pro.r.rx = raw_coord(asym.cx, asym.maxx, asym.minx, cos(rad_r), 0.70f);
        pro.pro.r.ry = raw_coord(asym.cy, asym.maxy, asym.miny, sin(rad_r), 0.70f);
        assert(s2_pad_input(&pro, &btn, st) == 1);
        check_int_stick(st[0], st[1], deg, &prev_l, "pro-left");
        check_int_stick(st[2], st[3], deg, &prev_r, "pro-right");
    }

    /* Pro Controller 2 corner overtravel clamps circularly to DM_STICK_MAX, not sqrt(2) * DM_STICK_MAX. */
    pro.pro.r.lx = 4095;
    pro.pro.r.ly = 4095;
    pro.pro.r.rx = 0;
    pro.pro.r.ry = 4095;
    assert(s2_pad_input(&pro, &btn, st) == 1);
    assert(hypot((double)st[0], (double)st[1]) <= (double)DM_STICK_MAX + 1.0);
    assert(hypot((double)st[2], (double)st[3]) <= (double)DM_STICK_MAX + 1.0);

    /* Paired Joy-Con 2 (default calibration on left, asymmetric on right). */
    s2_pad_state pair = {0};
    pair.left.have = 1;
    pair.right.have = 1;
    pair.right.cal[0] = asym;
    s2_cal def;
    s2_cal_default(&def);
    prev_l = prev_r = 0.0;
    for (int deg = 0; deg <= 360; deg += 5) {
        double rad = (double)deg * PI / 180.0;
        pair.left.r.lx = raw_coord(def.cx, def.maxx, def.minx, cos(rad), 0.80f);
        pair.left.r.ly = raw_coord(def.cy, def.maxy, def.miny, sin(rad), 0.80f);
        pair.right.r.rx = raw_coord(asym.cx, asym.maxx, asym.minx, cos(rad), 1.15f);
        pair.right.r.ry = raw_coord(asym.cy, asym.maxy, asym.miny, sin(rad), 1.15f);
        assert(s2_pad_input(&pair, &btn, st) == 1);
        check_int_stick(st[0], st[1], deg, &prev_l, "pair-left");
        check_int_stick(st[2], st[3], deg, &prev_r, "pair-right");
    }

    /* Single Joy-Con 2 right alone maps its stick to st[0..1] and zeroes st[2..3]. */
    s2_pad_state alone = {0};
    alone.right.have = 1;
    alone.right.cal[0] = asym;
    alone.right.r.rx = (uint16_t)lrint(asym.cx + asym.maxx);
    alone.right.r.ry = (uint16_t)lrint(asym.cy);
    assert(s2_pad_input(&alone, &btn, st) == 1);
    assert(st[0] == DM_STICK_MAX && st[1] == 0 && st[2] == 0 && st[3] == 0);
}

static void test_s2_joy_stick(void) {
    s2_cal cals[3] = {
        {0},   // invalid -> default
        {.cx = 2048.f, .cy = 2048.f, .maxx = 1300.f, .minx = 1300.f, .maxy = 1300.f, .miny = 1300.f, .valid = 1},
        {.cx = 1980.f, .cy = 2110.f, .maxx = 1420.f, .minx = 1150.f, .maxy = 1180.f, .miny = 1360.f, .valid = 1},
    };
    const char *names[3] = {"joy-default", "joy-sym", "joy-asym"};
    const float scales[2] = {0.65f, 1.15f};

    for (int ci = 0; ci < 3; ci++) {
        s2_cal eff;
        cal_params(&cals[ci], &eff);
        for (int right = 0; right < 2; right++) {
            s2_dev d = {0};
            d.have = 1;
            d.cal[0] = cals[ci];
            int32_t out[2];

            /* Deadzone interior -> (0, 0). */
            if (right) { d.r.rx = (uint16_t)lrint(eff.cx); d.r.ry = (uint16_t)lrint(eff.cy); }
            else { d.r.lx = (uint16_t)lrint(eff.cx); d.r.ly = (uint16_t)lrint(eff.cy); }
            s2_joy_stick(&d, right, out);
            assert(out[0] == 0 && out[1] == 0);

            /* 360-degree sweeps at 65% and 115% deflection. */
            for (int si = 0; si < 2; si++) {
                double prev_ang = 0.0;
                for (int deg = 0; deg <= 360; deg += 5) {
                    double rad = (double)deg * PI / 180.0;
                    uint16_t rx = raw_coord(eff.cx, eff.maxx, eff.minx, cos(rad), scales[si]);
                    uint16_t ry = raw_coord(eff.cy, eff.maxy, eff.miny, sin(rad), scales[si]);
                    if (right) { d.r.rx = rx; d.r.ry = ry; }
                    else { d.r.lx = rx; d.r.ly = ry; }
                    s2_joy_stick(&d, right, out);
                    check_int_stick(out[0], out[1], deg, &prev_ang, names[ci]);
                }
            }
        }
    }
}

int main(void) {
    test_s2_cal_apply();
    test_s2_pad_input();
    test_s2_joy_stick();
    puts("switch2_stick_test: ok");
    return 0;
}
