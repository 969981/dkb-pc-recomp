// SPDX-License-Identifier: GPL-3.0-or-later
// The automatic ceiling on the VRAM all pool mirrors take together (TOTK_MIRROR_BUDGET = auto; vk_memory.c
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
