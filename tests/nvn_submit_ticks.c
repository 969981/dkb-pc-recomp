// Handwritten timestamp lookup/publication races. Only the extracted runtime code reads or writes the ring.
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#endif
#include "platform/platform.h"

static void test_after_tick_read(void);
static void test_after_tag_store(void);
static int rt_game_version;
static uint64_t kern_now_ns(void) { return 3200000; }
static uint64_t rt_ns_to_ticks(uint64_t ns) { return ns / 32; }
#include "timestamp_source.inc"

// Actual OS locks. Try-lock reports a proven blocked participant to the condition latch; it does not replace
// any runtime ring read, publication store or lookup decision.
#ifdef _WIN32
_Static_assert(sizeof(PlatMutex) == sizeof(SRWLOCK) && sizeof(PlatCond) == sizeof(CONDITION_VARIABLE), "lock storage");
static void raw_lock(PlatMutex *m) { AcquireSRWLockExclusive((PSRWLOCK)m); }
static int raw_trylock(PlatMutex *m) { return TryAcquireSRWLockExclusive((PSRWLOCK)m) != 0; }
static void raw_unlock(PlatMutex *m) { ReleaseSRWLockExclusive((PSRWLOCK)m); }
static void raw_wait(PlatCond *c, PlatMutex *m) {
    if (!SleepConditionVariableSRW((PCONDITION_VARIABLE)c, (PSRWLOCK)m, INFINITE, 0)) abort();
}
static void raw_wake(PlatCond *c) { WakeAllConditionVariable((PCONDITION_VARIABLE)c); }
typedef HANDLE Thread;
#define THREAD_RETURN DWORD WINAPI
#define THREAD_DONE 0
static Thread start(LPTHREAD_START_ROUTINE fn) {
    Thread t = CreateThread(NULL, 0, fn, NULL, 0, NULL); if (!t) abort(); return t;
}
static void join(Thread t) { WaitForSingleObject(t, INFINITE); CloseHandle(t); }
#else
static void *lazy(void *volatile *slot, int cond) {
    void *p = __atomic_load_n(slot, __ATOMIC_ACQUIRE);
    if (p) return p;
    void *fresh = malloc(cond ? sizeof(pthread_cond_t) : sizeof(pthread_mutex_t));
    if (!fresh) abort();
    if (cond) pthread_cond_init(fresh, NULL); else pthread_mutex_init(fresh, NULL);
    if (__atomic_compare_exchange_n(slot, &p, fresh, 0, __ATOMIC_RELEASE, __ATOMIC_ACQUIRE)) return fresh;
    if (cond) pthread_cond_destroy(fresh); else pthread_mutex_destroy(fresh);
    free(fresh); return p;
}
static void raw_lock(PlatMutex *m) { pthread_mutex_lock(lazy(&m->p, 0)); }
static int raw_trylock(PlatMutex *m) { return pthread_mutex_trylock(lazy(&m->p, 0)) == 0; }
static void raw_unlock(PlatMutex *m) { pthread_mutex_unlock(m->p); }
static void raw_wait(PlatCond *c, PlatMutex *m) { pthread_cond_wait(lazy(&c->p, 1), m->p); }
static void raw_wake(PlatCond *c) { pthread_cond_broadcast(lazy(&c->p, 1)); }
typedef pthread_t Thread;
#define THREAD_RETURN void *
#define THREAD_DONE NULL
static Thread start(void *(*fn)(void *)) {
    Thread t; if (pthread_create(&t, NULL, fn, NULL)) abort(); return t;
}
static void join(Thread t) { pthread_join(t, NULL); }
#endif

enum { ORDINARY, OLD_READER, INCOMPLETE_WRITER };
enum { MAIN, READER, WRITER };
static _Thread_local int role;
static PlatMutex latch = PLAT_MUTEX_INIT;
static PlatCond changed = PLAT_COND_INIT;
static int scenario, reader_paused, writer_paused, release_reader, release_writer;
static int reader_blocked, writer_blocked, reader_done, writer_done;
static uint64_t reader_result;

void plat_mutex_lock(PlatMutex *m) {
    if (raw_trylock(m)) return;
    raw_lock(&latch);
    if (role == READER) reader_blocked = 1;
    if (role == WRITER) writer_blocked = 1;
    raw_wake(&changed);
    raw_unlock(&latch);
    raw_lock(m);
}
void plat_mutex_unlock(PlatMutex *m) { raw_unlock(m); }

static void test_after_tick_read(void) {
    if (role != READER || scenario != OLD_READER) return;
    raw_lock(&latch);
    reader_paused = 1;
    raw_wake(&changed);
    while (!release_reader) raw_wait(&changed, &latch);
    raw_unlock(&latch);
}
static void test_after_tag_store(void) {
    if (role != WRITER || scenario != INCOMPLETE_WRITER) return;
    raw_lock(&latch);
    writer_paused = 1;
    raw_wake(&changed);
    while (!release_writer) raw_wait(&changed, &latch);
    raw_unlock(&latch);
}
static THREAD_RETURN read_slot(void *unused) {
    (void)unused;
    role = READER;
    uint64_t found = nvn_submit_ticks(1025);
    raw_lock(&latch);
    reader_result = found;
    reader_done = 1;
    raw_wake(&changed);
    raw_unlock(&latch);
    return THREAD_DONE;
}
static THREAD_RETURN reuse_slot(void *unused) {
    (void)unused;
    role = WRITER;
    submit_ticks_publish(1025, 9999);
    raw_lock(&latch);
    writer_done = 1;
    raw_wake(&changed);
    raw_unlock(&latch);
    return THREAD_DONE;
}
static void reset(int next_scenario) {
    // Previous participants have joined before fixture state is reused.
    scenario = next_scenario;
    reader_paused = writer_paused = release_reader = release_writer = 0;
    reader_blocked = writer_blocked = reader_done = writer_done = 0;
    reader_result = UINT64_MAX;
    submit_ticks_publish(1, 111);
}
static int checks, failures;
#define CHECK(c, text) do { checks++; if (!(c)) { failures++; fprintf(stderr, "FAIL: %s\n", text); } } while (0)

static void report(uint64_t fence, uint64_t result[2]) {
    CounterWrite *write = malloc(sizeof *write);
    if (!write) abort();
    *write = (CounterWrite){(uint64_t)(uintptr_t)result, fence};
    result[0] = result[1] = UINT64_MAX;
    counter_cb(write);
}

int main(void) {
    CHECK(SUBMIT_TICKS == 1024, "timestamp ring remains bounded");
    CHECK(nvn_submit_ticks(1) == 0, "unpublished fence misses");
    reset(ORDINARY);
    CHECK(nvn_submit_ticks(1) == 111, "published pair is found");
    CHECK(nvn_submit_ticks(1025) == 0, "different tag in the same slot misses");
    uint64_t result[2];
    report(1, result);
    CHECK(result[0] == 0 && result[1] == 111, "Switch 2 callback retains the submission timestamp");
    report(1025, result);
    CHECK(result[0] == 0 && result[1] == 100000, "completion before publication takes the existing fallback");
    rt_game_version = 143;
    report(1, result);
    CHECK(result[0] == 0 && result[1] == 100000, "legacy callback retains completion-time behavior");
    rt_game_version = 0;

    // The reader has already fetched the old tick when another submission tries to reuse its slot. Waiting for
    // either actual lock contention or publisher completion makes the old broken ordering deterministic too.
    reset(OLD_READER);
    Thread reader = start(read_slot);
    raw_lock(&latch);
    while (!reader_paused) raw_wait(&changed, &latch);
    raw_unlock(&latch);
    Thread writer = start(reuse_slot);
    raw_lock(&latch);
    while (!writer_blocked && !writer_done) raw_wait(&changed, &latch);
    CHECK(writer_blocked, "slot reuse cannot split an in-progress lookup");
    release_reader = 1;
    raw_wake(&changed);
    raw_unlock(&latch);
    join(reader); join(writer);
    CHECK(reader_result == 0, "old reader never accepts tick 111 with the new fence tag 1025");
    CHECK(nvn_submit_ticks(1025) == 9999, "new pair is visible after publication");
    CHECK(nvn_submit_ticks(1) == 0, "evicted fence misses");

    // Stop the publisher after its tag store, before its tick store. A lookup must not observe this incomplete pair.
    reset(INCOMPLETE_WRITER);
    writer = start(reuse_slot);
    raw_lock(&latch);
    while (!writer_paused) raw_wait(&changed, &latch);
    raw_unlock(&latch);
    reader = start(read_slot);
    raw_lock(&latch);
    while (!reader_blocked && !reader_done) raw_wait(&changed, &latch);
    CHECK(reader_blocked, "lookup cannot observe an incomplete publication");
    release_writer = 1;
    raw_wake(&changed);
    raw_unlock(&latch);
    join(reader); join(writer);
    CHECK(reader_result == 9999, "lookup pairs the new fence with its own tick");
    report(1025, result);
    CHECK(result[0] == 0 && result[1] == 9999, "completed publication reaches the actual timestamp callback");
    report(1, result);
    CHECK(result[0] == 0 && result[1] == 100000, "ring eviction retains the callback fallback");
    submit_ticks_publish(2, 0);
    report(2, result);
    CHECK(result[0] == 0 && result[1] == 100000, "zero tick retains the callback fallback");
    submit_ticks_publish(UINT64_MAX, 777);
    CHECK(nvn_submit_ticks(UINT64_MAX) == 777, "full-width fence tags are preserved");
    printf("timestamp publication: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
