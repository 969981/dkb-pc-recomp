// Unit tests for high-FPS stick-spin ring cadence accumulation (runtime/frame_rate.c spin_step).
// No game files or GPU required.
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cpu.h"

static uint64_t g_now_ns = 1000000000ull;
static uint64_t plat_now_ns(void) { return g_now_ns; }

typedef int PlatMutex;
#define PLAT_MUTEX_INIT 0
static void plat_mutex_lock(PlatMutex *m) { (*m)++; }
static void plat_mutex_unlock(PlatMutex *m) { assert(*m > 0); (*m)--; }

#include "stick_spin_cut.c"

static const double PI = 3.14159265358979323846;

/* Faithful model of Bananza 3.2.0 f_70002409d0 (main+0x2409d0):
 * computes signed angle delta / dt, optionally filters via spin_step(), pushes into 90-entry ring ceil(s0) times,
 * then walks back up to 15 entries stopping on sign reversal or >2 consecutive |v| < 1e-5f samples.
 * Spin trigger (f_700146872c / f_70003bf174): streak >= 15 and |sum / (min(streak, 15) + 1)| >= 15.0.
 */
typedef struct {
    float ring[90];
    int head, count;
    float prev_x, prev_y;
    int has_prev;
    float out_avg, out_streak;
    float last_raw_rate, last_eff_dt, last_eff_rate;
} SpinSim;

static void spin_sim_step(SpinSim *sim, float sx, float sy, float dt, uint64_t det_key, int use_hook) {
    float rate = 0.0f;
    if (sim->has_prev) {
        float r0 = sim->prev_x * sim->prev_x + sim->prev_y * sim->prev_y;
        float r1 = sx * sx + sy * sy;
        if (r0 >= 1e-10f && r1 >= 1e-10f) {
            float cross = sim->prev_x * sy - sim->prev_y * sx;
            float dot = sim->prev_x * sx + sim->prev_y * sy;
            float ang = atan2f(cross, dot) * 57.295776f;
            rate = ang / dt;
        }
    }
    sim->prev_x = sx;
    sim->prev_y = sy;
    sim->has_prev = 1;

    float eff_dt = dt;
    float eff_rate = rate;
    if (use_hook) {
        float dt_mem = dt;
        Cpu c;
        memset(&c, 0, sizeof c);
        c.x[0] = (uint64_t)(uintptr_t)&dt_mem;
        c.x[19] = det_key;
        set_f32(&c, 8, rate);
        spin_step(&c);
        eff_dt = reg_f32(&c, 0);
        eff_rate = reg_f32(&c, 8);
    }

    sim->last_raw_rate = rate;
    sim->last_eff_dt = eff_dt;
    sim->last_eff_rate = eff_rate;

    int pushes = (int)ceilf(eff_dt);
    for (int p = 0; p < pushes; p++) {
        sim->ring[sim->head] = eff_rate;
        sim->head = (sim->head + 1) % 90;
        if (sim->count < 90) sim->count++;
    }

    int streak = 0, zeros = 0;
    float sum = 0.0f, dir = 0.0f;
    int avail = sim->count < 15 ? sim->count : 15;
    for (int k = 0; k < avail; k++) {
        float v = sim->ring[((sim->head - 1 - k) % 90 + 90) % 90];
        if (fabsf(v) < 1e-5f) {
            if (++zeros > 2) break;
            streak++;
            continue;
        }
        zeros = 0;
        if (dir == 0.0f) dir = v;
        else if (dir * v <= 0.0f) break;
        sum += v;
        streak++;
    }
    sim->out_streak = (float)streak;
    sim->out_avg = sum / (float)((streak < 15 ? streak : 15) + 1);
}

static int simulate_spin_triggers(double fps, double poll_hz, double spin_dps, int use_hook, uint64_t key) {
    SpinSim sim;
    memset(&sim, 0, sizeof sim);
    double frame_s = 1.0 / fps;
    double poll_s = 1.0 / poll_hz;
    float dt = (float)(60.0 / fps);
    int total_frames = (int)(1.0 * fps);   // 1 second of continuous stick rotation
    int triggered = 0;

    g_now_ns += 1000000000ull;   // fresh window (>250ms after any prior test)
    for (int f = 0; f < total_frames; f++) {
        double t = f * frame_s;
        /* Quantize stick position to latest HID report time (models polled USB/BT controller). */
        double sample_t = floor(t / poll_s) * poll_s;
        double rad = sample_t * spin_dps * (PI / 180.0);
        float sx = (float)cos(rad), sy = (float)sin(rad);
        g_now_ns += (uint64_t)llround(frame_s * 1e9);
        spin_sim_step(&sim, sx, sy, dt, key, use_hook);
        if (sim.out_streak >= 15.0f && fabsf(sim.out_avg) >= 15.0f) triggered++;
    }
    return triggered;
}

static void test_spin_step_cadence_and_invariants(void) {
    /* 1. Across 60/90/120/144/180/240 FPS and 60/66.7/125/1000 Hz controllers at 1080 deg/s (3 rev/s):
     *    spin_step MUST reliably trigger spin attacks in every combination. */
    const double fps_list[] = {60.0, 90.0, 120.0, 144.0, 180.0, 240.0};
    const double poll_list[] = {60.0, 66.7, 125.0, 1000.0};
    uint64_t next_key = 0x10000;

    for (size_t fi = 0; fi < sizeof fps_list / sizeof *fps_list; fi++) {
        for (size_t pi = 0; pi < sizeof poll_list / sizeof *poll_list; pi++) {
            int hits = simulate_spin_triggers(fps_list[fi], poll_list[pi], 1080.0, 1, next_key++);
            if (hits <= 0) {
                fprintf(stderr, "FAIL spin_step did not trigger at %.1f fps, %.1f Hz poll\n",
                        fps_list[fi], poll_list[pi]);
                abort();
            }
        }
    }

    /* Confirm the unpatched game fails completely at 240 FPS on 60/66.7 Hz pads (>2 consecutive zeros break streak). */
    assert(simulate_spin_triggers(240.0, 60.0, 1080.0, 0, 0) == 0);
    assert(simulate_spin_triggers(240.0, 66.7, 1080.0, 0, 0) == 0);

    /* 2. dt >= 0.95 (near-60 FPS or slower) and dt <= 0 are strict no-ops: load s0 = [x0], leave s8 and every other reg intact. */
    const float slow_dts[] = {0.95f, 0.97f, 0.99f, 1.0f, 1.5f, 2.0f, 0.0f, -1.0f};
    for (size_t i = 0; i < sizeof slow_dts / sizeof *slow_dts; i++) {
        float dt_mem = slow_dts[i];
        Cpu c, want;
        memset(&c, 0, sizeof c);
        for (int r = 0; r < 32; r++) {
            c.x[r] = 0x5555000011110000ull + (uint64_t)r;
            c.v[r] = ((__uint128_t)c.x[r] << 64) | ~c.x[r];
        }
        c.sp = 0x70001000ull;
        c.nzcv = 0x60000000u;
        c.x[0] = (uint64_t)(uintptr_t)&dt_mem;
        set_f32(&c, 8, 19.25f);
        want = c;
        set_f32(&want, 0, dt_mem);
        spin_step(&c);
        assert(memcmp(&c, &want, sizeof c) == 0);
    }

    /* 3. Register preservation at 0 < dt < 1: ONLY v[0] (and v[8] on emit) may change. */
    {
        float dt_mem = 0.5f;
        uint64_t key = 0x9990001;
        g_now_ns += 500000000ull;
        Cpu c, want;
        memset(&c, 0, sizeof c);
        for (int r = 0; r < 32; r++) {
            c.x[r] = 0xaaaa000022220000ull + (uint64_t)r;
            c.v[r] = ((__uint128_t)c.x[r] << 64) | ~c.x[r];
        }
        c.sp = 0x70002000ull;
        c.nzcv = 0x90000000u;
        c.x[0] = (uint64_t)(uintptr_t)&dt_mem;
        c.x[19] = key;
        set_f32(&c, 8, 36.0f);
        want = c;
        set_f32(&want, 0, 0.0f);   // first 0.5-tick frame: skip push, v[8] untouched
        spin_step(&c);
        assert(memcmp(&c, &want, sizeof c) == 0);

        g_now_ns += 8333333ull;
        set_f32(&c, 8, 0.0f);      // second 0.5-tick frame: emits (36*0.5 + 0*0.5)/1.0 = 18.0
        want = c;
        set_f32(&want, 0, 1.0f);
        set_f32(&want, 8, 18.0f);
        spin_step(&c);
        assert(memcmp(&c, &want, sizeof c) == 0);
    }

    /* 4. >250 ms gap resets accumulated partial dt and angle cleanly. */
    {
        float dt_mem = 0.6f;
        uint64_t key = 0x9990002;
        g_now_ns += 500000000ull;
        Cpu c;
        memset(&c, 0, sizeof c);
        c.x[0] = (uint64_t)(uintptr_t)&dt_mem;
        c.x[19] = key;
        set_f32(&c, 8, 50.0f);
        spin_step(&c);   // acc.dt = 0.6 < 1.0 -> s0 = 0.0
        assert(reg_f32(&c, 0) == 0.0f);

        g_now_ns += 300000000ull;   // 300 ms gap > 250 ms -> must reset acc.dt to 0!
        set_f32(&c, 8, 10.0f);
        spin_step(&c);   // if not reset, 0.6 + 0.6 = 1.2 would emit; after reset, 0.6 < 1.0 -> s0 = 0.0!
        assert(reg_f32(&c, 0) == 0.0f);

        g_now_ns += 10000000ull;
        set_f32(&c, 8, 10.0f);
        spin_step(&c);   // 0.6 + 0.6 = 1.2 >= 1.0 -> emits 10.0 (NOT contaminated by old 50.0!)
        assert(reg_f32(&c, 0) == 1.0f);
        assert(fabsf(reg_f32(&c, 8) - 10.0f) < 1e-4f);
    }

    /* 5. Exact [0, 1.0] bucket carry leaves no post-stop exponential decay tail. */
    {
        float dt_mem = 0.6f;
        uint64_t key = 0x9990003;
        g_now_ns += 500000000ull;
        Cpu c;
        memset(&c, 0, sizeof c);
        c.x[0] = (uint64_t)(uintptr_t)&dt_mem;
        c.x[19] = key;

        /* Frame 1 (rate = 20.0): acc.dt = 0.6 -> s0 = 0.0 */
        set_f32(&c, 8, 20.0f);
        spin_step(&c);
        assert(reg_f32(&c, 0) == 0.0f);

        /* Frame 2 (rate = 20.0): next_dt = 1.2 -> emits 20.0, carries dt = 0.2, angle = 4.0 */
        g_now_ns += 10000000ull;
        set_f32(&c, 8, 20.0f);
        spin_step(&c);
        assert(reg_f32(&c, 0) == 1.0f);
        assert(fabsf(reg_f32(&c, 8) - 20.0f) < 1e-4f);

        /* Frame 3 (rate = 0.0): next_dt = 0.8 -> s0 = 0.0 */
        g_now_ns += 10000000ull;
        set_f32(&c, 8, 0.0f);
        spin_step(&c);
        assert(reg_f32(&c, 0) == 0.0f);

        /* Frame 4 (rate = 0.0): next_dt = 1.4 -> emits 4.0, carries dt = 0.4, angle = 0.0 */
        g_now_ns += 10000000ull;
        set_f32(&c, 8, 0.0f);
        spin_step(&c);
        assert(reg_f32(&c, 0) == 1.0f);
        assert(fabsf(reg_f32(&c, 8) - 4.0f) < 1e-4f);

        /* Frame 5 (rate = 0.0): next_dt = 1.0 -> emits exact 0.0 with no decay tail */
        g_now_ns += 10000000ull;
        set_f32(&c, 8, 0.0f);
        spin_step(&c);
        assert(reg_f32(&c, 0) == 1.0f);
        assert(fabsf(reg_f32(&c, 8)) == 0.0f);
    }

    /* 6. Bypass frames (dt >= 0.95) clear any stale partial bucket for this instance without touching other regs. */
    const float bypass_dts[2] = {0.99f, 1.05f};
    for (size_t bi = 0; bi < 2; bi++) {
        float dt_sub = 0.6f;
        float dt_bypass = bypass_dts[bi];
        uint64_t key = 0x9990004 + bi;
        g_now_ns += 500000000ull;
        Cpu c, want;
        memset(&c, 0, sizeof c);
        c.x[0] = (uint64_t)(uintptr_t)&dt_sub;
        c.x[19] = key;
        set_f32(&c, 8, 50.0f);
        spin_step(&c);   // acc.dt = 0.6, acc.angle = 30.0 -> s0 = 0.0
        assert(reg_f32(&c, 0) == 0.0f);

        g_now_ns += 10000000ull;
        for (int r = 0; r < 32; r++) {
            c.x[r] = 0x3333000077770000ull + (uint64_t)r;
            c.v[r] = ((__uint128_t)c.x[r] << 64) | ~c.x[r];
        }
        c.sp = 0x70003000ull;
        c.nzcv = 0x40000000u;
        c.x[0] = (uint64_t)(uintptr_t)&dt_bypass;
        c.x[19] = key;
        set_f32(&c, 8, 18.0f);
        want = c;
        set_f32(&want, 0, dt_bypass);
        spin_step(&c);
        assert(memcmp(&c, &want, sizeof c) == 0);
        assert(reg_f32(&c, 0) == dt_bypass);
        assert(reg_f32(&c, 8) == 18.0f);

        g_now_ns += 10000000ull;
        c.x[0] = (uint64_t)(uintptr_t)&dt_sub;
        set_f32(&c, 8, 10.0f);
        spin_step(&c);   // bucket was cleared -> 0.6 < 1.0 -> s0 = 0.0
        assert(reg_f32(&c, 0) == 0.0f);

        g_now_ns += 10000000ull;
        set_f32(&c, 8, 10.0f);
        spin_step(&c);   // 0.6 + 0.6 = 1.2 >= 1.0 -> emits 10.0 (uncontaminated by old 50.0)
        assert(reg_f32(&c, 0) == 1.0f);
        assert(fabsf(reg_f32(&c, 8) - 10.0f) < 1e-4f);
    }

    /* 7. 60 fps DKB_REALTIME=1 dither around dt = 1.0 passes straight through every frame and triggers spin. */
    {
        const float dither_dts[6] = {0.98f, 1.02f, 0.99f, 1.01f, 0.96f, 1.04f};
        const double poll_s = 1.0 / 60.0;
        const uint64_t key = 0x9990010;
        SpinSim sim;
        memset(&sim, 0, sizeof sim);
        g_now_ns += 1000000000ull;
        double t = 0.0;
        int hits = 0;
        for (int f = 0; f < 60; f++) {
            float dt = dither_dts[f % 6];
            double frame_s = (double)dt / 60.0;
            double sample_t = floor(t / poll_s) * poll_s;
            double rad = sample_t * 1080.0 * (PI / 180.0);
            float sx = (float)cos(rad), sy = (float)sin(rad);
            g_now_ns += (uint64_t)llround(frame_s * 1e9);
            spin_sim_step(&sim, sx, sy, dt, key, 1);
            assert(sim.last_eff_dt == dt);
            assert(sim.last_eff_rate == sim.last_raw_rate);
            if (sim.out_streak >= 15.0f && fabsf(sim.out_avg) >= 15.0f) hits++;
            t += frame_s;
        }
        assert(hits > 0);
    }
}

int main(void) {
    test_spin_step_cadence_and_invariants();
    puts("stick_spin_test: ok");
    return 0;
}
