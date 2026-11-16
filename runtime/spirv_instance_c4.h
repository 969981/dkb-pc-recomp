// spirv_instance_c4.c: a stage that reads uniform block c4 at instance * 256 bytes (vk_exec_draw.c vk_exec_draw_merge).
#pragma once
#include <stdint.h>

// vertex 1: a vertex module, which also writes the instance to a flat int output at Location loc; 0: a fragment
// module, which reads it from that input. set and binding name the c4 block as the module decorates it (set ~0u: the
// stage has no c4; a vertex stage then only passes the instance on). A malloc'd copy, or NULL when the module has a
// layout the rewrite does not know, already reads the instance, uses Location loc, reads c4 at a constant vec4 index
// of 16 or more (past one 256-byte block), or (fragment) has no c4.
// *n_chains: the block reads rewritten.
uint32_t *spirv_instance_c4(const uint32_t *w, uint64_t n, int vertex, uint32_t set, uint32_t binding, uint32_t loc,
                            uint64_t *out_n, unsigned *n_chains);
