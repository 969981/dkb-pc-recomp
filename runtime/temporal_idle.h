// SPDX-License-Identifier: GPL-3.0-or-later
// When a temporal upscaler (vk_dlss.c, vk_fsr3.c, vk_fsr4.c) lets go of what it holds: the rule only, with nothing of
// Vulkan in it, so tests/temporal_idle_test.py can compile it alone. The rule is the one Longshot's temporal_idle in
// gpu/vk/vk_temporal.cpp applies (commit e314e2b7, "FSR 3.1 frees its images once the chain lets it go"): an engine
// that holds memory and has not been the chosen upscaler for 120 frames (2 s at 60 fps) is let go. An upscaler that is
// still the setting's choice keeps what it holds however long the game skips it: Bananza's loading screens have no
// upscale for seconds, and making the images, DLSS's feature and AMD's context again at each area's first frame would
// be a stall at every entry.
#pragma once

#include <stdint.h>

#define TEMPORAL_IDLE_FRAMES 120u

// last_wanted: the last frame on which the setting chose this upscaler (0: never). now: the current frame.
// wanted: the setting chooses it now. holding: it has images or a context to give back.
static inline int temporal_idle_due(uint64_t now, uint64_t last_wanted, int wanted, int holding) {
    if (!holding || wanted) return 0;
    return now > last_wanted && now - last_wanted > TEMPORAL_IDLE_FRAMES;
}
