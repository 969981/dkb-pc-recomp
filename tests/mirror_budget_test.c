// The pool mirror budget rules (runtime/mirror_budget.h): the automatic ceiling and whether one mirror fits.
#include <stdio.h>

#include "mirror_budget.h"

#define MB (1ull << 20)

static int failures;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); failures++; } } while (0)

int main(void) {
    CHECK(mirror_budget_auto(0) == 3072 * MB);
    CHECK(mirror_budget_auto(4096 * MB) == 512 * MB);
    CHECK(mirror_budget_auto(8192 * MB) == 2048 * MB);
    CHECK(mirror_budget_auto(24576 * MB) == 3072 * MB);

    const uint64_t B = 4096 * MB;
    CHECK(mirror_fits(1000 * MB, 64 * MB, B));            // plenty of room
    CHECK(!mirror_fits(3784 * MB, 8 * MB, B));            // large: an eighth (512 MiB) is kept free
    CHECK(mirror_fits(3784 * MB, MB / 4, B));             // small: only a thirty-second (128 MiB)
    CHECK(!mirror_fits(3968 * MB, MB / 4, B));            // small, inside the thirty-second
    CHECK(!mirror_fits(5000 * MB, MB, B));                // already over the budget
    CHECK(!mirror_fits(0, UINT64_MAX, B));                // no overflow

    if (!failures) printf("mirror budget: all checks passed\n");
    return failures ? 1 : 0;
}
