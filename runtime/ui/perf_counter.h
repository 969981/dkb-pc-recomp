// The performance overlay's counter look (ui/perf_counter.cpp). Present path only (ImGui).
#pragma once
#include "imgui.h"

// Draw the frame rate Hookshot's way: the number big, green and outlined in black, `label` small beside it, no plate,
// in the top right corner a margin in from the window's edge. `scale` is the menu's scale and `width` the window's.
void perf_counter_draw(ImDrawList *dl, float scale, float width, const char *number, const char *label);
