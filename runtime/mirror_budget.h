// SPDX-License-Identifier: GPL-3.0-or-later
// The automatic ceiling on the VRAM all pool mirrors take together (DKB_MIRROR_BUDGET = auto; vk_memory.c
// mirror_ceiling reads the setting and applies the ceiling in mirror_create). The rule is Longshot's
// (https://git.magicbean.dev/MagicBeanDev/Longshot @ d9d1c3423ec110865059e1bd6e17b12423ade0c5, gpu/vk/mirror_budget.h
// auto_bytes, GPL-3.0-or-later), written again in C: a quarter of the card and at most 3 GiB, or an eighth on a card
// of 4.5 GiB or less, so that the mirrors leave room for the textures on a small card.
#pragma once

#include <stdint.h>

// vram: the card's capacity in bytes as the runtime estimates it (the device-local budget); 0 when unknown.
static inline uint64_t mirror_budget_auto(uint64_t vram) {
    if (!vram) return 3072ull << 20;
    if (vram <= (4ull << 30) + (512ull << 20)) return vram / 8;
    return vram / 4 < (3072ull << 20) ? vram / 4 : 3072ull << 20;
}

// Whether a pool mirror of n bytes fits the device-local budget with used bytes already taken (vk_memory.c
// mirror_create). A mirror keeps an eighth of the budget free for textures and render targets, but a small one only a
// thirty-second: a pool refused here stays imported for good and every GPU read of it crosses the bus, so on a 4 GiB
// card near full a few hundred pools of a few hundred KiB each doubled the GPU time of a frame (7.7 to 16.3 ms with
// DKB_VRAM_LIMIT=4096 in the tutorial cave), for 36.5 MiB of VRAM they would have taken.
#define MIRROR_SMALL (4ull << 20)
static inline int mirror_fits(uint64_t used, uint64_t n, uint64_t budget) {
    uint64_t keep = n < MIRROR_SMALL ? budget / 32 : budget / 8;
    return used <= budget && n <= budget - used && keep <= budget - used - n;
}
