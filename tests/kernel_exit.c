// Handwritten contexts and bounded scheduler/dispatch stubs around the actual kernel and existing exit probe code.
#include <windows.h>
#include "kernel_exit_setjmp.inc"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "host.h"
#include "platform/platform.h"

typedef struct Fiber Fiber;
struct RtThread {
    Cpu *cpu, *cur;
    uint64_t entry, arg, stack_top;
    uint32_t handle, tid;
    PlatIntr *hthr;
    int started, exited, closed, host_depth, exit_pending;
    PlatCond cv;
    jmp_buf exit_jmp;
    PlatFiber *base_fiber;
    Fiber *fiber, *leaving;
};
struct Fiber {
    uint64_t addr, func, arg, stack, size;
    Cpu *cpu;
    PlatFiber *host;
    int state;
    volatile int running;
};
typedef struct { int stage, depth, bad; uint64_t fiber, stack; } ExitJob;
enum { F_NEW, F_STARTED, F_DONE, S_OFF = 0, HOST_STACK = 256 << 20, STACK = 1 << 20 };
static __thread RtThread *tcur;
static RtThread *owned_thread;
static PlatMutex G;
static Fiber owned_fiber;
static Cpu fiber_cpu;
static int checks, failures, finishes;
static const uint64_t a_exit_entry=1, a_exit_nested=2, a_fiber_exit_fn=3, a_fiber_exit_thread=4, a_fib_init=5, a_fib_switch=6;
static int sealed = 1;
#define CHECK(c, text) do { checks++; if (!(c)) { failures++; fprintf(stderr, "FAIL: %s\n", text); } } while (0)
static uint64_t ga(const void *p) { return (uintptr_t)p; }
static void exit_nested(Cpu *c), exit_entry(Cpu *c), fiber_exit_fn(Cpu *c), fiber_exit_thread(Cpu *c);
static void fiber_switch_to(RtThread *t, const Cpu *from, Fiber *to);
static void fiber_main(void *p), svc_exit(Cpu *c);

void rt_fatal(const char *fmt, ...) {
    va_list args; va_start(args, fmt); vfprintf(stderr, fmt, args); va_end(args); fputc('\n', stderr); exit(70);
}
static void lock(void) {}
static void unlock(void) {}
static void core_take(RtThread *t) { (void)t; }
static void core_put(RtThread *t) { CHECK(t->fiber == NULL && t->cur == t->cpu, "finish is on the base context"); }
static void acct(RtThread *t, int state) { (void)t; (void)state; }
static void wake_sync_waiters(void) { CHECK(owned_thread->exited, "thread is signalled only after exit"); finishes++; }
static void thr_free(RtThread *t) { (void)t; rt_fatal("unexpected closed thread fixture"); }
static RtThread *cur(uint32_t imm) { (void)imm; if (!tcur) abort(); return tcur; }
static PlatIntr *ap_open_self(void) { return NULL; }
static Fiber *fiber_find(uint64_t addr) { return addr == owned_fiber.addr ? &owned_fiber : NULL; }
void plat_intr_free(PlatIntr *t) { (void)t; rt_fatal("unexpected preemption fixture"); }
uint32_t plat_thread_id(void) { return GetCurrentThreadId(); }
void plat_cond_wait(PlatCond *c, PlatMutex *m) { (void)c; (void)m; rt_fatal("unexpected wait before thread start"); }
const char *ldr_describe(uint64_t pc, char *buf, int size) { (void)pc; (void)buf; (void)size; return "owned entry"; }
void hostfn_seal(void) { rt_fatal("unexpected registry initialization"); }
uint32_t kern_main_thread(Cpu **cpu) { tcur = owned_thread; *cpu = tcur->cpu; return 1; }
static void sv(Cpu *c, uint32_t op, uint64_t a, uint64_t b, uint64_t d, uint64_t e) {
    (void)a; (void)b; (void)d; (void)e; if (op != 0x0A) abort(); svc_exit(c);
}
static void exit_thread(Cpu *c) { svc_exit(c); }
static void test_target(RtThread *t) {
    ULONG_PTR low, high;
    GetCurrentThreadStackLimits(&low, &high);
    _JUMP_BUFFER *target = (_JUMP_BUFFER*)t->exit_jmp;
    CHECK(target->Frame >= low && target->Frame < high, "setjmp records an eligible frame on the target stack");
    DWORD64 image = 0;
    CHECK(RtlLookupFunctionEntry(target->Rip, &image, NULL) != NULL, "landing site has Windows unwind metadata");
}
static void test_exit_stack(RtThread *t) {
    ULONG_PTR low, high;
    GetCurrentThreadStackLimits(&low, &high);
    _JUMP_BUFFER *target = (_JUMP_BUFFER*)t->exit_jmp;
    CHECK(target->Rsp >= low && target->Rsp < high, "longjmp stays on its saved stack, including fiber exit");
    CHECK(t->fiber == NULL && t->leaving == NULL, "guest fiber has been left and released before longjmp");
}

#include "kernel_exit_platform.inc"
#include "kernel_exit_source.inc"

void rt_dispatch(Cpu *c) {
    switch (c->pc) {
    case 1: exit_entry(c); break;
    case 2: exit_nested(c); break;
    case 3: fiber_exit_fn(c); break;
    case 4: fiber_exit_thread(c); break;
    case 5:
        owned_fiber = (Fiber){.addr=c->x[0], .func=c->x[1], .arg=c->x[2], .stack=c->x[3], .size=c->x[4], .cpu=&fiber_cpu};
        owned_fiber.host = plat_fiber_new(HOST_STACK, fiber_main, &owned_fiber);
        if (!owned_fiber.host) rt_fatal("fiber allocation failed");
        break;
    case 6: fiber_switch_to(tcur, c, &owned_fiber); break;
    default: rt_fatal("unexpected fixture dispatch");
    }
}
static DWORD WINAPI worker(void *is_main) {
    if (is_main) guest_main((void*)exit_entry); else thread_main(owned_thread);
    return 0;
}
static void run(int fiber, int is_main) {
    ExitJob job = {.fiber=0x100, .stack=0x1000};
    Cpu cpu = {0};
    RtThread t = {.cpu=&cpu, .cur=&cpu, .entry=fiber ? a_fiber_exit_thread : a_exit_entry,
                  .arg=ga(&job), .stack_top=0x200000, .started=1};
    cpu.x[0] = ga(&job);
    owned_thread = &t;
    HANDLE thread = CreateThread(NULL, 0, worker, is_main ? (void*)1 : NULL, 0, NULL);
    if (!thread) rt_fatal("thread allocation failed");
    WaitForSingleObject(thread, INFINITE); CloseHandle(thread);
    CHECK(t.exited, "actual thread_finish runs after nonlocal exit");
    CHECK(!job.bad, "nothing executes after ExitThread");
    if (fiber) {
        CHECK(job.stage == 3, "existing fiber exit probe reaches stage 3");
        CHECK(t.exit_pending && !owned_fiber.running, "pending fiber exit releases its running state");
        plat_fiber_free(owned_fiber.host);
    } else CHECK(job.stage == 2 && job.depth == 3, "existing probe exits through three nested host calls");
}
int main(void) {
    PROCESS_MITIGATION_USER_SHADOW_STACK_POLICY policy = {0};
    BOOL available = GetProcessMitigationPolicy(GetCurrentProcess(), ProcessUserShadowStackPolicy, &policy, sizeof policy);
    printf("mitigation query=%d flags=%#lx SetContextIpValidation=%d (read-only)\n", (int)available, policy.Flags,
           (int)policy.SetContextIpValidation);
    run(0, 0); run(1, 0); run(0, 1);
    CHECK(finishes == 3, "all three actual landing paths finish once");
    printf("kernel exit: %d checks, %d failures; nested stage 2/depth 3, fiber stage 3, guest main\n", checks, failures);
    return failures ? 1 : 0;
}
