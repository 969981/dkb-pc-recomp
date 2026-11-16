// Uniform block c4 read per instance (vk_exec_draw.c vk_exec_draw_merge, TOTK_DRAW_MERGE).
//
// Bananza draws one mesh thousands of times a frame with nothing between two draws but a new c4 range (one 256-byte
// block each). The merge records such a run as one instanced draw and binds the run's blocks one after another as a
// single range. Instance i then has to read c4 at i * 256 bytes: Ryujinx declares every constant buffer as a struct
// of one vec4[4096] array, and every read is an OpAccessChain of the variable with member 0 and an array index, so
// the rewrite adds instance * 16 to that index.
//
// The vertex stage takes the instance from gl_InstanceIndex (the merged draw's first instance is 0) and passes it to
// the fragment stage as a flat int at Location loc; the fragment stage reads it from there. The value is loaded at
// the start of the entry function, so a block read in another function, or any other use of the variable (a whole
// load, a pointer handed to a call), leaves the module as it is (NULL).
//
// A draw by itself binds at most 256 bytes of c4, so a read at vec4 index 16 or above is out of its range, and
// robust buffer access returns zero for it. In the merged range the same index lands in the next instance's block.
// A constant index of 16 or more therefore leaves the module as it is. A computed index cannot be bounded here, so
// one past the block would read the next instance's data where the separate draw read zero.
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "spirv_instance_c4.h"

enum {
    OP_ENTRY_POINT = 15, OP_TYPE_INT = 21, OP_TYPE_POINTER = 32, OP_CONSTANT = 43, OP_FUNCTION = 54,
    OP_VARIABLE = 59, OP_LOAD = 61, OP_STORE = 62, OP_ACCESS_CHAIN = 65, OP_IN_BOUNDS_ACCESS_CHAIN = 66,
    OP_DECORATE = 71, OP_MEMBER_DECORATE = 72, OP_IADD = 128, OP_IMUL = 132, OP_LABEL = 248,
    DEC_BUILTIN = 11, DEC_FLAT = 14, DEC_LOCATION = 30, DEC_BINDING = 33, DEC_DESCRIPTOR_SET = 34,
    BUILTIN_INSTANCE_ID = 6, BUILTIN_INSTANCE_INDEX = 43, BUILTIN_BASE_INSTANCE = 4425, BUILTIN_DRAW_INDEX = 4426,
    SC_INPUT = 1, SC_UNIFORM = 2, SC_OUTPUT = 3, MODEL_VERTEX = 0, MODEL_FRAGMENT = 4,
};

typedef struct {
    uint32_t *o;
    uint64_t n, cap;
    int bad;
} Buf;

static void put(Buf *b, const uint32_t *w, uint64_t n) {
    if (b->bad) return;
    if (b->n + n > b->cap) {
        uint64_t cap = (b->n + n) * 2 + 64;
        uint32_t *p = realloc(b->o, cap * 4);
        if (!p) { b->bad = 1; return; }
        b->o = p, b->cap = cap;
    }
    memcpy(b->o + b->n, w, n * 4);
    b->n += n;
}

#define PUT(b, ...) do { const uint32_t ins_[] = {__VA_ARGS__}; put(b, ins_, sizeof ins_ / 4); } while (0)

// 1 when the instruction takes pointer id p in an operand other than an access chain's base. Only the operand slots
// that can hold a pointer are read: a literal such as an OpExtInst instruction number may equal the id.
static int pointer_use(uint32_t op, const uint32_t *a, uint32_t na, uint32_t p) {
    uint32_t from = 0, to = 0, step = 1;   // operands [from, to) every step
    switch (op) {
    case 61: case 68: case 83: case 67: case 70: from = 2, to = 3; break;   // Load, ArrayLength, CopyObject, PtrAccessChain
    case 62: from = 0, to = 1; break;                                       // Store
    case 63: case 64: from = 0, to = 2; break;                              // CopyMemory, CopyMemorySized
    case 57: from = 3, to = na; break;                                      // FunctionCall arguments
    case 169: from = 3, to = 5; break;                                      // Select
    case 245: from = 2, to = na, step = 2; break;                           // Phi values
    case 401: case 402: case 403: from = 2, to = 4; break;                  // PtrEqual, PtrNotEqual, PtrDiff
    default: return 0;
    }
    for (uint32_t k = from; k < to && k < na; k += step)
        if (a[k] == p) return 1;
    return 0;
}

uint32_t *spirv_instance_c4(const uint32_t *w, uint64_t n, int vertex, uint32_t set, uint32_t binding, uint32_t loc,
                            uint64_t *out_n, unsigned *n_chains) {
    if (n_chains) *n_chains = 0;
    if (n < 5 || w[0] != 0x07230203) return NULL;
    uint32_t bound = w[3];
    if (bound > (1u << 22)) return NULL;
    uint32_t *set_of = malloc((size_t)bound * 4), *bind_of = malloc((size_t)bound * 4);
    uint32_t *ptr_sc = calloc(bound, 4), *ptr_to = calloc(bound, 4), *var_sc = calloc(bound, 4);
    uint8_t *chain = calloc(bound, 1);   // OpAccessChain results into the c4 variable
    uint8_t *num = calloc(bound, 1);     // NUM_INT32: a 32-bit integer type; NUM_CONST: an OpConstant of one, in cval
    uint32_t *cval = malloc((size_t)bound * 4);
    uint32_t *out = NULL;
    enum { NUM_INT32 = 1, NUM_CONST = 2 };
    if (!set_of || !bind_of || !ptr_sc || !ptr_to || !var_sc || !chain || !num || !cval) goto done;
    memset(set_of, 0xff, (size_t)bound * 4), memset(bind_of, 0xff, (size_t)bound * 4);
    uint32_t i32 = 0, entry = 0;
    uint64_t ep_at = 0, first_dec = 0, fn_at = 0;
    // Pass 1: the declarations.
    for (uint64_t i = 5; i < n;) {
        uint32_t op = w[i] & 0xffff, len = w[i] >> 16;
        if (!len || i + len > n) goto done;
        const uint32_t *a = w + i + 1;
        if (op == OP_FUNCTION) { fn_at = i; break; }
        if (op == OP_ENTRY_POINT && len >= 3) {
            if (entry || a[0] != (vertex ? MODEL_VERTEX : MODEL_FRAGMENT)) goto done;
            entry = a[1], ep_at = i;
        } else if ((op == OP_DECORATE || op == OP_MEMBER_DECORATE) && !first_dec) {
            first_dec = i;
        }
        if (op == OP_DECORATE && len >= 3 && a[0] < bound) {
            if (a[1] == DEC_LOCATION && len == 4 && a[2] == loc) goto done;   // the varying's Location is taken
            if (a[1] == DEC_DESCRIPTOR_SET && len == 4) set_of[a[0]] = a[2];
            if (a[1] == DEC_BINDING && len == 4) bind_of[a[0]] = a[2];
            if (a[1] == DEC_BUILTIN && len == 4 &&
                (a[2] == BUILTIN_INSTANCE_INDEX || a[2] == BUILTIN_INSTANCE_ID || a[2] == BUILTIN_BASE_INSTANCE ||
                 a[2] == BUILTIN_DRAW_INDEX))
                goto done;   // the stage reads the instance itself: the merged draw would change what it sees
        } else if (op == OP_MEMBER_DECORATE && len == 5 && a[2] == DEC_LOCATION && a[3] == loc) {
            goto done;
        } else if (op == OP_TYPE_INT && len == 4 && a[1] == 32) {
            if (a[0] < bound) num[a[0]] |= NUM_INT32;
            if (a[2] == 1 && !i32) i32 = a[0];
        } else if (op == OP_CONSTANT && len == 4 && a[0] < bound && a[1] < bound && num[a[0]] & NUM_INT32) {
            num[a[1]] |= NUM_CONST, cval[a[1]] = a[2];
        } else if (op == OP_TYPE_POINTER && len == 4 && a[0] < bound) {
            ptr_sc[a[0]] = a[1], ptr_to[a[0]] = a[2];
        } else if (op == OP_VARIABLE && len >= 4 && a[1] < bound) {
            var_sc[a[1]] = a[2];
        }
        i += len;
    }
    if (!entry || !fn_at || !first_dec || !i32) goto done;
    uint32_t c4 = 0;   // the block's variable; 0: the stage has no c4 (a vertex stage still passes the instance on)
    for (uint32_t id = 1; id < bound && set != ~0u; id++)
        if (var_sc[id] == SC_UNIFORM && set_of[id] == set && bind_of[id] == binding) { c4 = id; break; }
    if (!c4 && !vertex) goto done;   // nothing to change
    // Pass 2: the reads of the block. Each is a chain with member 0 and the array index, in the entry function.
    unsigned chains = 0;
    int in_entry = 0;
    for (uint64_t i = fn_at; i < n && c4;) {
        uint32_t op = w[i] & 0xffff, len = w[i] >> 16;
        if (!len || i + len > n) goto done;
        const uint32_t *a = w + i + 1;
        if (op == OP_FUNCTION && len >= 3) in_entry = a[1] == entry;
        if ((op == OP_ACCESS_CHAIN || op == OP_IN_BOUNDS_ACCESS_CHAIN) && len >= 4 && a[2] == c4) {
            if (len < 6 || !in_entry || a[1] >= bound) goto done;   // a chain to the array alone is indexed later
            // A constant index past the 16 vec4 of one block.
            if (a[4] < bound && num[a[4]] & NUM_CONST && cval[a[4]] >= 16) goto done;
            chain[a[1]] = 1, chains++;
        } else if (pointer_use(op, a, len - 1, c4)) {
            goto done;
        }
        i += len;
    }

    uint32_t next = bound;
    uint32_t ptr_in = 0, ptr_out = 0;
    for (uint32_t id = 1; id < bound; id++) {
        if (!ptr_in && ptr_sc[id] == SC_INPUT && ptr_to[id] == i32) ptr_in = id;
        if (!ptr_out && ptr_sc[id] == SC_OUTPUT && ptr_to[id] == i32) ptr_out = id;
    }
    uint32_t new_ptr_in = ptr_in ? 0 : next++, use_ptr_in = ptr_in ? ptr_in : new_ptr_in;
    uint32_t new_ptr_out = !vertex || ptr_out ? 0 : next++, use_ptr_out = ptr_out ? ptr_out : new_ptr_out;
    uint32_t src = next++;                    // gl_InstanceIndex (vertex) or the flat input (fragment)
    uint32_t dst = vertex ? next++ : 0;       // the flat output (vertex)
    uint32_t k16 = next++, inst = next++, off = next++;

    Buf b = {0};
    put(&b, w, 5);
    int entry_done = 0;
    in_entry = 0;
    for (uint64_t i = 5; i < n;) {
        uint32_t op = w[i] & 0xffff, len = w[i] >> 16;
        const uint32_t *a = w + i + 1;
        if (i == first_dec) {
            if (vertex) {
                PUT(&b, 4u << 16 | OP_DECORATE, src, DEC_BUILTIN, BUILTIN_INSTANCE_INDEX);
                PUT(&b, 4u << 16 | OP_DECORATE, dst, DEC_LOCATION, loc);
                PUT(&b, 3u << 16 | OP_DECORATE, dst, DEC_FLAT);
            } else {
                PUT(&b, 4u << 16 | OP_DECORATE, src, DEC_LOCATION, loc);
                PUT(&b, 3u << 16 | OP_DECORATE, src, DEC_FLAT);
            }
        }
        if (i == fn_at) {   // the end of the declarations
            if (new_ptr_in) PUT(&b, 4u << 16 | OP_TYPE_POINTER, new_ptr_in, SC_INPUT, i32);
            if (new_ptr_out) PUT(&b, 4u << 16 | OP_TYPE_POINTER, new_ptr_out, SC_OUTPUT, i32);
            PUT(&b, 4u << 16 | OP_VARIABLE, use_ptr_in, src, SC_INPUT);
            if (vertex) PUT(&b, 4u << 16 | OP_VARIABLE, use_ptr_out, dst, SC_OUTPUT);
            PUT(&b, 4u << 16 | OP_CONSTANT, i32, k16, 16);
        }
        if (i == ep_at) {   // the entry point lists the new interface variables
            uint32_t extra = vertex ? 2 : 1, hdr = (len + extra) << 16 | OP_ENTRY_POINT;
            put(&b, &hdr, 1);
            put(&b, w + i + 1, len - 1);
            put(&b, &src, 1);
            if (vertex) put(&b, &dst, 1);
            i += len;
            continue;
        }
        if ((op == OP_ACCESS_CHAIN || op == OP_IN_BOUNDS_ACCESS_CHAIN) && len >= 6 && a[1] < bound && chain[a[1]]) {
            uint32_t idx = next++;
            PUT(&b, 5u << 16 | OP_IADD, i32, idx, a[4], off);   // the array index; signedness may differ (OpIAdd allows it)
            put(&b, w + i, 5);   // header, type, result, base, member 0
            put(&b, &idx, 1);
            put(&b, w + i + 6, len - 6);
            i += len;
            continue;
        }
        put(&b, w + i, len);
        if (op == OP_FUNCTION && len >= 3) in_entry = a[1] == entry;
        if (op == OP_LABEL && in_entry && !entry_done) {
            // The entry function's first block: after its OpVariables, the instance and its block offset.
            entry_done = 1;
            uint64_t j = i + len;
            while (j < n && (w[j] & 0xffff) == OP_VARIABLE) put(&b, w + j, w[j] >> 16), j += w[j] >> 16;
            PUT(&b, 4u << 16 | OP_LOAD, i32, inst, src);
            if (vertex) PUT(&b, 3u << 16 | OP_STORE, dst, inst);
            PUT(&b, 5u << 16 | OP_IMUL, i32, off, inst, k16);
            i = j;
            continue;
        }
        i += len;
    }
    if (b.bad || !entry_done) { free(b.o); goto done; }
    b.o[3] = next;
    *out_n = b.n;
    if (n_chains) *n_chains = chains;
    out = b.o;
done:
    free(set_of), free(bind_of), free(ptr_sc), free(ptr_to), free(var_sc), free(chain), free(num), free(cval);
    return out;
}
