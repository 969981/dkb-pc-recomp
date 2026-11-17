// The rule for when a temporal upscaler lets go of its images (runtime/temporal_idle.h).
#include <stdio.h>

#include "temporal_idle.h"

static int failures;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); failures++; } } while (0)

int main(void) {
    // nothing held: nothing to free, however long it has been
    CHECK(!temporal_idle_due(100000, 10, 0, 0));
    // chosen: kept, however long the game has skipped it (a loading screen)
    CHECK(!temporal_idle_due(100000, 10, 1, 1));
    // no longer chosen: kept for TEMPORAL_IDLE_FRAMES frames, so a row stepped through comes back at once
    CHECK(!temporal_idle_due(1000, 1000, 0, 1));
    CHECK(!temporal_idle_due(1000 + TEMPORAL_IDLE_FRAMES, 1000, 0, 1));
    CHECK(temporal_idle_due(1000 + TEMPORAL_IDLE_FRAMES + 1, 1000, 0, 1));
    // chosen again: the clock starts over
    CHECK(!temporal_idle_due(1500, 1500, 0, 1));
    // a frame counter that has not moved past the last choice does not underflow
    CHECK(!temporal_idle_due(5, 10, 0, 1));
    if (!failures) printf("temporal idle: all checks passed\n");
    return failures ? 1 : 0;
}
