// Equivalence of the staged uploads' blocked-byte lookup (runtime/vk_memory.c st_blocked, st_job_blocked and st_cut)
// with the union it replaced (st_blk_update and the cut loop of staged_upload before commit ce4178e), on random inputs.
// tests/staged_blocked_test.py pastes the production types and functions in at the markers below.
//
// A case is a few staged pools with their own lists (built by the production own_merge and own_cut), jobs with a
// write-back list (sorted, disjoint, as staged_wb_record builds it) and an owned list (in the order the writes came,
// then sorted by the production st_sort_job, as the sort thread does), and the fence of the prologue. For every query
// range [a, b) of a pool, the lookup's blocked bytes and the pieces st_cut gives to the upload must equal those of the
// old union: one sorted, merged list of every ended job's ranges before the fence and of every pool on st_own, and the
// old cut loop over it. The comparison of TOTK_STAGED_BLK_CHECK (st_chk_union, st_chk_range) runs on every query as well
// and must flag exactly the queries where the two disagree. On a mismatch the case is shrunk to a minimal one and
// printed with its seed.
//
// Usage: staged_blocked [cases] [first seed]
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "msort.h"
#include "platform/platform.h"

typedef void *VkBuffer;
typedef void *VkDeviceMemory;
typedef void *VmaAllocation;
typedef struct pool_t pool_t;

static void rt_fatal(const char *fmt, ...) __attribute__((noreturn, format(printf, 1, 2)));
static void rt_fatal(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    exit(2);
}
static uint64_t qpc(void) { return 0; }
static int show_log;   // print_case: the lines TOTK_STAGED_BLK_CHECK logs
static void rt_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void rt_log(const char *fmt, ...) {
    if (!show_log) return;
    va_list ap;
    va_start(ap, fmt);
    printf("  log: ");
    vprintf(fmt, ap);
    printf("\n");
    va_end(ap);
}

/* PRODUCTION_TYPES */

static int staged_fault;           // the own lists count (TOTK_STAGED_WB=fault)
static StJob *st_jobs;             // the case's jobs
static PoolRec **st_own;           // own_listed: the pools whose own list holds a range
static int nst_own, cst_own;
static volatile int64_t st_sort_ns;

/* PRODUCTION_FUNCTIONS */

// ---- the old union (ce4178e^ runtime/vk_memory.c st_blk_update, and the lookup and cut loop of staged_upload),
// kept here as the reference. Its own append and qsort, so it shares no code with the lookup.
typedef struct { const PoolRec *r; uint64_t off, size; } RefRange;
static RefRange *ref;
static int nref, cref;

static void ref_add(const PoolRec *r, uint64_t off, uint64_t size) {
    if (nref == cref) {
        cref = cref ? cref * 2 : 64;
        ref = realloc(ref, (size_t)cref * sizeof *ref);
        if (!ref) abort();
    }
    ref[nref++] = (RefRange){r, off, size};
}

static int ref_cmp(const void *x, const void *y) {
    const RefRange *p = x, *q = y;
    if (p->r != q->r) return (uintptr_t)p->r < (uintptr_t)q->r ? -1 : 1;
    return p->off < q->off ? -1 : p->off > q->off;
}

// The union of the write-back and owned ranges of the jobs recorded before the submission of `fence`, and with
// TOTK_STAGED_WB=fault of the own lists of the pools on st_own: sorted by pool and offset, the ranges that overlap or
// touch joined.
static void ref_build(uint64_t fence) {
    nref = 0;
    for (StJob *j = st_jobs; j; j = j->next) {
        if (!j->ended || j->fence >= fence) continue;
        for (int i = 0; i < j->n; i++) ref_add(j->w[i].r, j->w[i].off, j->w[i].size);
        for (int i = 0; i < j->no; i++) ref_add(j->o[i].r, j->o[i].off, j->o[i].size);
    }
    if (staged_fault)
        for (int i = 0; i < nst_own; i++)
            for (int k = 0; k < st_own[i]->nown; k++)
                ref_add(st_own[i], st_own[i]->own[k].lo, st_own[i]->own[k].hi - st_own[i]->own[k].lo);
    qsort(ref, (size_t)nref, sizeof *ref, ref_cmp);
    int o = 0;
    for (int i = 0; i < nref; i++) {
        if (o && ref[o - 1].r == ref[i].r && ref[i].off <= ref[o - 1].off + ref[o - 1].size) {
            uint64_t end = ref[i].off + ref[i].size;
            if (end > ref[o - 1].off + ref[o - 1].size) ref[o - 1].size = end - ref[o - 1].off;
        } else ref[o++] = ref[i];
    }
    nref = o;
}

typedef struct { uint64_t lo, hi; } Piece;
#define MAXPC 2048

// What the old staged_upload did with [a, b) of r: the union's ranges of r that end above a and start below b, the
// blocked bytes (clipped to [a, b)), and the pieces it copied, joined where they touch as range_add joined them.
static void ref_query(const PoolRec *r, uint64_t a, uint64_t b, Piece *blk, int *nblk, Piece *cut, int *ncut,
                      uint64_t *total, uint64_t *skipped) {
    int lo = 0, hi = nref;
    while (lo < hi) {
        int mid = lo + (hi - lo) / 2;
        if ((uintptr_t)ref[mid].r < (uintptr_t)r || (ref[mid].r == r && ref[mid].off + ref[mid].size <= a)) lo = mid + 1;
        else hi = mid;
    }
    *nblk = *ncut = 0;
    for (int k = lo; k < nref && ref[k].r == r && ref[k].off < b; k++) {
        uint64_t x = ref[k].off > a ? ref[k].off : a, y = ref[k].off + ref[k].size < b ? ref[k].off + ref[k].size : b;
        if (x < y && *nblk < MAXPC) blk[(*nblk)++] = (Piece){x, y};
    }
#define REF_CUT(x, y) do {                                                                                             \
        if (*ncut && cut[*ncut - 1].hi == (x)) cut[*ncut - 1].hi = (y);                                                \
        else if (*ncut < MAXPC) cut[(*ncut)++] = (Piece){(x), (y)};                                                   \
    } while (0)
    for (int k = lo; a < b && k < nref && ref[k].r == r && ref[k].off < b; k++) {
        uint64_t ka = ref[k].off, kb = ka + ref[k].size;
        if (ka > a) {
            REF_CUT(a, ka);
            *total += ka - a;
        }
        uint64_t e = kb < b ? kb : b;
        if (e > a) *skipped += e - (ka > a ? ka : a), a = e;
    }
    if (a < b) {
        REF_CUT(a, b);
        *total += b - a;
    }
#undef REF_CUT
}

// ---- cases
#define MAXP 6
#define MAXJ 8
#define MAXR 96
#define MAXQ 48
typedef struct { int p; uint64_t lo, hi; } In;   // a range of pool p, offsets from its import_lo
typedef struct {
    uint64_t fence;
    int ended, nw, no;
    In w[MAXR];   // sorted by pool and offset, disjoint (staged_wb_record)
    In o[MAXR];   // in the order the writes came; st_sort_job sorts them
} JobIn;
typedef struct {
    int np, nj, nq, fault;
    uint64_t size[MAXP];
    int nown[MAXP];
    Piece own[MAXP][MAXR];   // sorted and disjoint (own_merge, own_cut)
    JobIn job[MAXJ];
    uint64_t fence;
    In q[MAXQ];
} Case;

static PoolRec pools[MAXP];   // in address order, as the lists sort them
static StJob jobs[MAXJ];

static void install(const Case *c) {
    memset(pools, 0, sizeof pools);
    nst_own = 0;
    staged_fault = c->fault;
    for (int p = 0; p < c->np; p++) {
        PoolRec *r = &pools[p];
        r->import_lo = 0x100000000ull * (uint64_t)(p + 1), r->import_hi = r->import_lo + c->size[p];
        r->cown = c->nown[p] ? c->nown[p] : 1;
        r->own = malloc((size_t)r->cown * sizeof *r->own);
        for (int k = 0; k < c->nown[p]; k++) r->own[k] = (struct KeptRange){c->own[p][k].lo, c->own[p][k].hi};
        r->nown = c->nown[p];
        own_listed(r);
    }
    st_jobs = NULL;
    for (int i = c->nj - 1; i >= 0; i--) {   // st_jobs lists the newest first (mirror_begin)
        const JobIn *ji = &c->job[i];
        StJob *j = &jobs[i];
        memset(j, 0, sizeof *j);
        j->fence = ji->fence, j->ended = ji->ended;
        j->w = malloc((size_t)(ji->nw + 1) * sizeof *j->w), j->cap = ji->nw + 1;
        for (int k = 0; k < ji->nw; k++)
            j->w[j->n++] = (StRange){&pools[ji->w[k].p], 0, 0, ji->w[k].lo, ji->w[k].hi - ji->w[k].lo, 0};
        j->o = malloc((size_t)(ji->no + 1) * sizeof *j->o), j->co = ji->no + 1;
        for (int k = 0; k < ji->no; k++)
            j->o[j->no++] = (StRange){&pools[ji->o[k].p], 0, 0, ji->o[k].lo, ji->o[k].hi - ji->o[k].lo, 0};
        st_sort_job(j);   // what the sort thread does before st_blocked may read the list (st_sorted_wait)
        j->next = st_jobs, st_jobs = j;
    }
}

static void uninstall(const Case *c) {
    for (int p = 0; p < c->np; p++) free(pools[p].own);
    for (int i = 0; i < c->nj; i++) free(jobs[i].w), free(jobs[i].o);
}

// Coverage: what the cases exercised, required to be nonzero at the end.
enum {
    CV_A_INSIDE, CV_A_EDGE, CV_A_OUT, CV_B_INSIDE, CV_B_EDGE, CV_B_OUT, CV_NONE_BLOCKED, CV_ALL_BLOCKED, CV_OVERLAP,
    CV_DUP, CV_SORTED_BY_SORT, CV_EMPTY_IN, CV_ONE_BYTE, CV_AT_0, CV_AT_END, CV_PAGE, CV_BLOCK64K, CV_SKIP_FENCE,
    CV_SKIP_ENDED, CV_NO_FAULT, CV_SEVERAL_LISTS, CV_PARTIAL, CV_SEVERAL_CUTS, CV_N
};
static const char *const cv_name[CV_N] = {
    "query starts inside a blocked range", "query starts on an edge", "query starts outside",
    "query ends inside a blocked range", "query ends on an edge", "query ends outside", "nothing blocked",
    "everything blocked", "pieces of two lists overlap", "the same piece in two lists", "owned list reordered by the sort",
    "empty input range", "single-byte blocked range", "blocked range at offset 0", "blocked range at the pool's end",
    "blocked range across or on a 4 KiB boundary", "blocked range across or on a 64 KiB boundary",
    "job skipped for its fence", "job skipped as not ended", "TOTK_STAGED_WB=fence", "blocked pieces from several lists",
    "partly blocked", "copied in two pieces or more",
};
static uint64_t cov[CV_N];

typedef struct {
    int nb[2], nc[2];
    Piece b[2][MAXPC], c[2][MAXPC];   // [0] the lookup, [1] the old union
    uint64_t total[2], skipped[2];
} Answer;

// Runs query qi of the installed case (ref_build and st_chk_union done): 1 when the lookup and the union agree and
// TOTK_STAGED_BLK_CHECK's comparison says so too; last_same and last_flagged hold the two verdicts.
static int last_same, last_flagged;
static int query(const Case *c, int qi, Answer *an, int count) {
    static struct KeptRange *blk;
    static int nblk, cblk;
    static MRange *cut;
    static int ncut, ccut;
    const In *q = &c->q[qi];
    PoolRec *r = &pools[q->p];
    memset(an->total, 0, sizeof an->total), memset(an->skipped, 0, sizeof an->skipped);
    st_blocked(r, q->lo, q->hi, c->fence, &blk, &nblk, &cblk);
    an->nb[0] = 0;
    for (int k = 0; k < nblk; k++) {   // the blocked bytes as sorted, disjoint pieces
        if (blk[k].lo >= blk[k].hi) continue;
        int n = an->nb[0];
        if (n && blk[k].lo <= an->b[0][n - 1].hi) {
            if (blk[k].hi > an->b[0][n - 1].hi) an->b[0][n - 1].hi = blk[k].hi;
        } else if (n < MAXPC) an->b[0][an->nb[0]++] = (Piece){blk[k].lo, blk[k].hi};
    }
    ncut = 0;
    st_cut(r, q->lo, q->hi, blk, nblk, &cut, &ncut, &ccut, &an->total[0], &an->skipped[0]);
    an->nc[0] = 0;
    for (int k = 0; k < ncut && k < MAXPC; k++) an->c[0][an->nc[0]++] = (Piece){cut[k].off, cut[k].off + cut[k].size};
    ref_query(r, q->lo, q->hi, an->b[1], &an->nb[1], an->c[1], &an->nc[1], &an->total[1], &an->skipped[1]);
    int same = an->nb[0] == an->nb[1] && an->nc[0] == an->nc[1] && an->total[0] == an->total[1] &&
               an->skipped[0] == an->skipped[1] && !memcmp(an->b[0], an->b[1], (size_t)an->nb[0] * sizeof(Piece)) &&
               !memcmp(an->c[0], an->c[1], (size_t)an->nc[0] * sizeof(Piece));
    for (int k = 0; k < ncut; k++)   // the cut pieces lie in [a, b) and are not empty
        if (cut[k].r != r || !cut[k].size || cut[k].off < q->lo || cut[k].off + cut[k].size > q->hi) same = 0;
    int64_t bad0 = chk_bad;
    st_chk_range(r, q->lo, q->hi, c->fence, blk, nblk);
    last_same = same, last_flagged = chk_bad != bad0;
    if (count) {
        uint64_t a = q->lo, b = q->hi;
        int ai = 0, ae = 0, bi = 0, be = 0;
        for (int k = 0; k < nref; k++) {
            if (ref[k].r != r) continue;
            uint64_t x = ref[k].off, y = x + ref[k].size;
            ai |= x < a && a < y, ae |= a == x || a == y, bi |= x < b && b < y, be |= b == x || b == y;
        }
        cov[ai ? CV_A_INSIDE : ae ? CV_A_EDGE : CV_A_OUT]++;
        cov[bi ? CV_B_INSIDE : be ? CV_B_EDGE : CV_B_OUT]++;
        if (!an->nb[1]) cov[CV_NONE_BLOCKED]++;
        if (an->nb[1] == 1 && an->b[1][0].lo == a && an->b[1][0].hi == b) cov[CV_ALL_BLOCKED]++;
        else if (an->nb[1]) cov[CV_PARTIAL]++;
        if (an->nc[1] > 1) cov[CV_SEVERAL_CUTS]++;
        uint64_t reach = 0;
        for (int k = 0; k < nblk; k++) {   // each list is disjoint: an overlap or a repeat is two lists'
            if (k && blk[k].lo < reach) cov[CV_OVERLAP]++;
            if (k && blk[k].lo == blk[k - 1].lo && blk[k].hi == blk[k - 1].hi) cov[CV_DUP]++;
            if (blk[k].hi > reach) reach = blk[k].hi;
        }
        int lists = 0;   // the lists that hold a byte of [a, b)
        if (staged_fault)
            for (int k = 0; k < r->nown; k++)
                if (r->own[k].lo < b && r->own[k].hi > a && r->own[k].lo < r->own[k].hi) { lists++; break; }
        for (const StJob *j = st_jobs; j; j = j->next) {
            if (!j->ended || j->fence >= c->fence) continue;
            for (int k = 0; k < j->n; k++)
                if (j->w[k].r == r && j->w[k].off < b && j->w[k].off + j->w[k].size > a && j->w[k].size) { lists++; break; }
            for (int k = 0; k < j->no; k++)
                if (j->o[k].r == r && j->o[k].off < b && j->o[k].off + j->o[k].size > a && j->o[k].size) { lists++; break; }
        }
        if (lists > 1) cov[CV_SEVERAL_LISTS]++;
    }
    return same && !last_flagged;
}

// Runs every query of a case: the index of the first that disagrees, or -1.
static int run_case(const Case *c, int count) {
    static Answer an;
    install(c);
    ref_build(c->fence);
    st_chk_union(c->fence);
    if (count) {
        for (int k = 0; k < nref; k++) {
            uint64_t x = ref[k].off, y = x + ref[k].size;
            const PoolRec *r = ref[k].r;
            uint64_t lim = r->import_hi - r->import_lo;
            if (y - x == 1) cov[CV_ONE_BYTE]++;
            if (!x) cov[CV_AT_0]++;
            if (y == lim) cov[CV_AT_END]++;
            if ((x >> 12) != ((y - 1) >> 12) || !(x & 0xfff) || !(y & 0xfff)) cov[CV_PAGE]++;
            if ((x >> 16) != ((y - 1) >> 16) || !(x & 0xffff) || !(y & 0xffff)) cov[CV_BLOCK64K]++;
        }
        for (int i = 0; i < c->nj; i++) {
            if (!c->job[i].ended) cov[CV_SKIP_ENDED]++;
            else if (c->job[i].fence >= c->fence) cov[CV_SKIP_FENCE]++;
        }
        if (!c->fault) cov[CV_NO_FAULT]++;
    }
    int bad = -1;
    for (int i = 0; i < c->nq && bad < 0; i++)
        if (!query(c, i, &an, count)) bad = i;
    uninstall(c);
    return bad;
}

// ---- random cases
static uint64_t rs;
static uint64_t rnd(void) {   // splitmix64
    uint64_t z = (rs += 0x9e3779b97f4a7c15ull);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
    return z ^ (z >> 31);
}
static uint64_t below(uint64_t n) { return n ? rnd() % n : 0; }

// An offset in [0, lim]: the ends, page and 64 KiB boundaries and their neighbours, the ends of ranges made already
// (pts) and their neighbours, or anywhere.
static uint64_t point(uint64_t lim, const uint64_t *pts, int npts) {
    uint64_t v;
    switch (below(8)) {
    case 0: v = below(3) ? 0 : lim; break;
    case 1: v = below(lim / 4096 + 1) * 4096; break;
    case 2: v = below(lim / 65536 + 1) * 65536; break;
    case 3: case 4: v = npts ? pts[below((uint64_t)npts)] : below(lim + 1); break;
    default: v = below(lim + 1); break;
    }
    int d = (int)below(5) - 2;   // -2 .. 2 bytes off
    if (below(2)) d = 0;
    if (d < 0 && v < (uint64_t)-d) d = 0;
    v += (uint64_t)(int64_t)d;
    return v > lim ? lim : v;
}

// A range in [0, lim]: lo a point, hi a point above it or lo plus a length (0, 1, a page, 64 KiB, a few bytes off
// them, or a random one).
static Piece range(uint64_t lim, uint64_t *pts, int *npts) {
    uint64_t lo = point(lim, pts, *npts), hi;
    static const uint64_t len[] = {0, 1, 2, 3, 48, 4095, 4096, 4097, 8192, 65535, 65536, 65537, 131072};
    switch (below(6)) {
    case 0: hi = point(lim, pts, *npts); if (hi < lo) { uint64_t t = lo; lo = hi, hi = t; } break;
    case 1: case 2: hi = lo + len[below(sizeof len / sizeof *len)]; break;
    default: hi = lo + below(below(4) ? 600 : 40000); break;
    }
    if (hi > lim) hi = lim;
    if (*npts < 4 * MAXR) pts[(*npts)++] = lo;
    if (*npts < 4 * MAXR) pts[(*npts)++] = hi;
    return (Piece){lo, hi};
}

static int in_less(const In *x, const In *y) { return x->p != y->p ? x->p < y->p : x->lo < y->lo; }
static void sort_in(In *a, int n) {   // insertion sort by pool and offset, stable
    for (int i = 1; i < n; i++) {
        In v = a[i];
        int j = i;
        while (j > 0 && in_less(&v, &a[j - 1])) a[j] = a[j - 1], j--;
        a[j] = v;
    }
}

static void make_case(Case *c, uint64_t seed) {
    static uint64_t pts[MAXP][4 * MAXR];
    int npts[MAXP] = {0};
    rs = seed;
    memset(c, 0, sizeof *c);
    c->np = 1 + (int)below(MAXP);
    c->fault = below(5) != 0;
    static const uint64_t sizes[] = {1, 100, 4096, 4097, 65536, 3 * 65536 + 4096, 5 * 65536, 1 << 20};
    for (int p = 0; p < c->np; p++)
        c->size[p] = below(4) ? sizes[below(sizeof sizes / sizeof *sizes)] : 1 + below(400000);
    int dense = below(4) == 0;   // many ranges a list
    // Own lists, as the fences' own_take (own_merge) and the fetches (own_cut) leave them.
    for (int p = 0; p < c->np; p++) {
        PoolRec *r = &pools[p];
        memset(r, 0, sizeof *r);
        nst_own = 0;
        int ops = (int)below(dense ? 24 : 6);
        for (int op = 0; op < ops; op++) {
            int n = 1 + (int)below(dense ? 12 : 4);
            if (below(4)) {   // own_merge of a sorted, joined list, like a job's sorted owned list
                StRange add[16];
                Piece raw[16];
                for (int k = 0; k < n; k++) raw[k] = range(c->size[p], pts[p], &npts[p]);
                for (int i = 1; i < n; i++)
                    for (int k = i; k > 0 && raw[k].lo < raw[k - 1].lo; k--) { Piece t = raw[k]; raw[k] = raw[k - 1], raw[k - 1] = t; }
                int m = 0;
                for (int k = 0; k < n; k++) {
                    if (m && raw[k].lo <= add[m - 1].off + add[m - 1].size) {
                        if (raw[k].hi > add[m - 1].off + add[m - 1].size) add[m - 1].size = raw[k].hi - add[m - 1].off;
                    } else add[m++] = (StRange){r, 0, 0, raw[k].lo, raw[k].hi - raw[k].lo, 0};
                }
                if (r->nown + m <= MAXR) own_merge(r, add, m);
            } else {   // own_cut of sorted, disjoint pieces, like a fetch's 64 KiB block
                struct KeptRange cut[16];
                uint64_t at = 0;
                int m = 0;
                for (int k = 0; k < n && at <= c->size[p]; k++) {
                    uint64_t lo = at + below(below(2) ? 70000 : 300), hi = lo + below(below(2) ? 70000 : 300) + 1;
                    if (below(3) == 0) lo = lo & ~0xffffull, hi = lo + 65536;   // a fetch's block
                    if (lo < at) lo = at;
                    if (hi > c->size[p]) hi = c->size[p];
                    if (lo >= hi) break;
                    cut[m++] = (struct KeptRange){lo, hi};
                    at = hi + below(2);
                }
                if (r->nown + m <= MAXR) own_cut(r, cut, m);   // each piece splits one range at most
            }
        }
        c->nown[p] = r->nown;
        for (int k = 0; k < r->nown; k++) c->own[p][k] = (Piece){r->own[k].lo, r->own[k].hi};
        free(r->own);
        r->own = NULL, r->nown = r->cown = 0;
    }
    nst_own = 0;
    c->nj = (int)below(MAXJ + 1);
    for (int i = 0; i < c->nj; i++) {
        JobIn *j = &c->job[i];
        j->fence = 1 + below(8);
        j->ended = below(6) != 0;
        // Write-backs: what staged_wb_record makes of the submission's ranges (sorted, joined, clamped to the
        // mirror, the empty ones dropped); sometimes touching ranges left apart.
        int nraw = (int)below(dense ? MAXR / 2 : 8), join_touch = below(2);
        In raw[MAXR];
        for (int k = 0; k < nraw; k++) {
            int p = (int)below((uint64_t)c->np);
            Piece x = range(c->size[p], pts[p], &npts[p]);
            raw[k] = (In){p, x.lo, x.hi};
        }
        sort_in(raw, nraw);
        for (int k = 0; k < nraw;) {
            int p = raw[k].p;
            uint64_t a = raw[k].lo, b = raw[k].hi;
            for (k++; k < nraw && raw[k].p == p && (join_touch ? raw[k].lo <= b : raw[k].lo < b); k++)
                if (raw[k].hi > b) b = raw[k].hi;
            if (a < b) j->w[j->nw++] = (In){p, a, b};
        }
        // Owned: in the order the writes came, overlapping, touching, nested, repeated and empty ranges included;
        // st_sort_job sorts and joins them at install.
        j->no = (int)below(dense ? MAXR - 8 : 10);
        for (int k = 0; k < j->no; k++) {
            int p = (int)below((uint64_t)c->np);
            Piece x = range(c->size[p], pts[p], &npts[p]);
            if (k && below(6) == 0) x = (Piece){j->o[k - 1].lo, j->o[k - 1].hi}, p = j->o[k - 1].p;   // repeated
            if (k && below(6) == 0 && j->o[k - 1].hi > j->o[k - 1].lo + 2)   // nested in the last one
                p = j->o[k - 1].p, x = (Piece){j->o[k - 1].lo + 1, j->o[k - 1].hi - 1};
            j->o[k] = (In){p, x.lo, x.hi};
            if (x.lo == x.hi) cov[CV_EMPTY_IN]++;
        }
        for (int k = 1; k < j->no; k++)
            if (in_less(&j->o[k], &j->o[k - 1])) { cov[CV_SORTED_BY_SORT]++; break; }
        // Duplicates across lists: a range of a pool's own list, or of an earlier job, again in this job's list.
        if (below(3) == 0 && j->no < MAXR) {
            int p = (int)below((uint64_t)c->np);
            if (c->nown[p]) {
                Piece x = c->own[p][below((uint64_t)c->nown[p])];
                j->o[j->no++] = (In){p, x.lo, x.hi};
            }
        }
        if (i && below(3) == 0 && j->no < MAXR) {
            const JobIn *e = &c->job[below((uint64_t)i)];
            if (e->no) j->o[j->no++] = e->o[below((uint64_t)e->no)];
        }
    }
    c->fence = below(11);
    // Queries: per pool, ranges that start and end inside, on the edge of and outside the blocked ranges.
    c->nq = 0;
    for (int p = 0; p < c->np; p++)
        for (int k = 0; k < MAXQ / MAXP; k++) {
            Piece x = range(c->size[p], pts[p], &npts[p]);
            if (below(2)) {   // half of them wide: from one point to another
                uint64_t y = point(c->size[p], pts[p], npts[p]);
                x = y < x.lo ? (Piece){y, x.lo} : (Piece){x.lo, y};
            }
            if (x.lo == x.hi) {   // staged_upload looks up non-empty ranges only
                if (x.hi < c->size[p]) x.hi++;
                else x.lo--;
            }
            c->q[c->nq++] = (In){p, x.lo, x.hi};
        }
}

// ---- shrinking and printing a counterexample
static int still_bad(const Case *c) { return run_case(c, 0) >= 0; }

static void shrink(Case *c) {
    int bad = run_case(c, 0);
    c->q[0] = c->q[bad], c->nq = 1;
    for (int changed = 1; changed;) {
        changed = 0;
        for (int i = 0; i < c->nj; i++) {   // drop a job
            Case t = *c;
            memmove(&t.job[i], &t.job[i + 1], (size_t)(t.nj - i - 1) * sizeof *t.job), t.nj--;
            if (still_bad(&t)) *c = t, changed = 1, i--;
        }
        for (int i = 0; i < c->nj; i++) {   // drop a range of a job's list
            for (int k = 0; k < c->job[i].nw; k++) {
                Case t = *c;
                JobIn *j = &t.job[i];
                memmove(&j->w[k], &j->w[k + 1], (size_t)(j->nw - k - 1) * sizeof *j->w), j->nw--;
                if (still_bad(&t)) *c = t, changed = 1, k--;
            }
            for (int k = 0; k < c->job[i].no; k++) {
                Case t = *c;
                JobIn *j = &t.job[i];
                memmove(&j->o[k], &j->o[k + 1], (size_t)(j->no - k - 1) * sizeof *j->o), j->no--;
                if (still_bad(&t)) *c = t, changed = 1, k--;
            }
        }
        for (int p = 0; p < c->np; p++)   // drop a range of an own list
            for (int k = 0; k < c->nown[p]; k++) {
                Case t = *c;
                memmove(&t.own[p][k], &t.own[p][k + 1], (size_t)(t.nown[p] - k - 1) * sizeof **t.own), t.nown[p]--;
                if (still_bad(&t)) *c = t, changed = 1, k--;
            }
    }
}

static void print_pieces(const char *what, const Piece *x, int n) {
    printf("    %s (%d):", what, n);
    for (int k = 0; k < n && k < 16; k++) printf(" [%#llx, %#llx)", (unsigned long long)x[k].lo, (unsigned long long)x[k].hi);
    printf("%s\n", n > 16 ? " ..." : "");
}

static void print_case(const Case *c) {
    static Answer an;
    printf("  TOTK_STAGED_WB=%s, prologue fence %llu\n", c->fault ? "fault" : "fence", (unsigned long long)c->fence);
    for (int p = 0; p < c->np; p++) {
        int used = c->nown[p] || c->q[0].p == p;
        for (int i = 0; i < c->nj && !used; i++)
            for (int k = 0; k < c->job[i].nw || k < c->job[i].no; k++)
                used |= (k < c->job[i].nw && c->job[i].w[k].p == p) || (k < c->job[i].no && c->job[i].o[k].p == p);
        if (!used) continue;   // the pools no list and no query names
        printf("  pool %d: %#llx bytes, own list:", p, (unsigned long long)c->size[p]);
        for (int k = 0; k < c->nown[p]; k++)
            printf(" [%#llx, %#llx)", (unsigned long long)c->own[p][k].lo, (unsigned long long)c->own[p][k].hi);
        printf("\n");
    }
    for (int i = 0; i < c->nj; i++) {
        const JobIn *j = &c->job[i];
        printf("  job %d: fence %llu, %s; write-backs:", i, (unsigned long long)j->fence, j->ended ? "ended" : "not ended");
        for (int k = 0; k < j->nw; k++)
            printf(" %d:[%#llx, %#llx)", j->w[k].p, (unsigned long long)j->w[k].lo, (unsigned long long)j->w[k].hi);
        printf("; owned, before the sort:");
        for (int k = 0; k < j->no; k++)
            printf(" %d:[%#llx, %#llx)", j->o[k].p, (unsigned long long)j->o[k].lo, (unsigned long long)j->o[k].hi);
        printf("\n");
    }
    install(c);
    ref_build(c->fence);
    chk_bad = 0, show_log = 1;
    st_chk_union(c->fence);
    query(c, 0, &an, 0);
    show_log = 0;
    uninstall(c);
    printf("  upload range: pool %d [%#llx, %#llx)\n", c->q[0].p, (unsigned long long)c->q[0].lo,
           (unsigned long long)c->q[0].hi);
    print_pieces("blocked, lookup   ", an.b[0], an.nb[0]);
    print_pieces("blocked, old union", an.b[1], an.nb[1]);
    print_pieces("copied, lookup    ", an.c[0], an.nc[0]);
    print_pieces("copied, old union ", an.c[1], an.nc[1]);
    printf("    copied/left out bytes: lookup %llu/%llu, old union %llu/%llu\n", (unsigned long long)an.total[0],
           (unsigned long long)an.skipped[0], (unsigned long long)an.total[1], (unsigned long long)an.skipped[1]);
    printf("  the fixture's comparison: %s; TOTK_STAGED_BLK_CHECK's: %s\n", last_same ? "same" : "different",
           last_flagged ? "flagged (log lines above)" : "not flagged");
}

int main(int argc, char **argv) {
    long cases = argc > 1 ? atol(argv[1]) : 20000;
    uint64_t seed0 = argc > 2 ? strtoull(argv[2], NULL, 0) : 1;
    static Case c;
    uint64_t queries = 0;
    for (long i = 0; i < cases; i++) {
        uint64_t seed = seed0 + (uint64_t)i;
        make_case(&c, seed);
        queries += (uint64_t)c.nq;
        int bad = run_case(&c, 1);
        if (bad >= 0) {
            printf("staged-blocked: MISMATCH in case %ld, seed %llu (rerun: staged_blocked 1 %llu); shrunk to:\n", i,
                   (unsigned long long)seed, (unsigned long long)seed);
            shrink(&c);
            print_case(&c);
            return 1;
        }
    }
    int missing = 0;
    for (int k = 0; k < CV_N; k++) {
        printf("  %-48s %llu\n", cv_name[k], (unsigned long long)cov[k]);
        if (!cov[k]) missing++;
    }
    int knob = chk_ranges != (int64_t)queries || chk_uploads != cases || chk_bad;
    show_log = 1;
    st_chk_log("at exit");   // the line TOTK_STAGED_BLK_CHECK ends a game run with
    show_log = 0;
    printf("staged-blocked: %ld cases from seed %llu, %llu upload ranges, 0 mismatches%s%s\n", cases,
           (unsigned long long)seed0, (unsigned long long)queries, missing ? ", but a coverage count is 0" : "",
           knob ? ", but TOTK_STAGED_BLK_CHECK's counts are off" : "");
    return missing || knob ? 1 : 0;
}
