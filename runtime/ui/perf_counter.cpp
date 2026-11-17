// SPDX-License-Identifier: GPL-3.0-or-later
// The counter look of the performance overlay: the frame rate as a large green number outlined in black with a small
// label beside it and nothing behind them.
//
// Adapted from Hookshot's FPS counter at its FPS level (gpu/vk/vk_menu.cpp draw_fps, https://git.notrexed.dev/Hookshot/
// Hookshot @ 186da06e72bb3e110f1726f8e14479abed601f11, GPL-3.0-or-later, Copyright 2026 Hookshot contributors): the
// colour, the sizes, the label's offset and the outline at the eight steps around the text. Hookshot places the digits
// one by one in cells of equal width; here the font's digits already share one width, so the number is drawn as one
// string. The corner, the frame generation line and the breakdown are not part of this file.
#include <float.h>

#include "ui/perf_counter.h"

void perf_counter_draw(ImDrawList *dl, float scale, float width, const char *number, const char *label) {
    ImFont *font = ImGui::GetFont();
    const float big = 28 * scale, small = 13 * scale, margin = 12 * scale, gap = 4 * scale;
    const float o = scale > 1 ? scale : 1;   // the outline's step, at least one pixel
    const ImU32 green = IM_COL32(72, 255, 110, 255), outline = IM_COL32(0, 0, 0, 230);
    const float nw = font->CalcTextSizeA(big, FLT_MAX, 0, number).x;
    const float lw = font->CalcTextSizeA(small, FLT_MAX, 0, label).x;
    const float x = width - margin - (nw + gap + lw), y = margin;   // the label sits low, on the number's baseline
    const float ly = y + 14 * scale;
    for (int dy = -1; dy <= 1; dy++)
        for (int dx = -1; dx <= 1; dx++)
            if (dx || dy) {
                dl->AddText(font, big, ImVec2(x + dx * o, y + dy * o), outline, number);
                dl->AddText(font, small, ImVec2(x + nw + gap + dx * o, ly + dy * o), outline, label);
            }
    dl->AddText(font, big, ImVec2(x, y), green, number);
    dl->AddText(font, small, ImVec2(x + nw + gap, ly), green, label);
}
