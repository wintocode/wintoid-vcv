#include <stdio.h>
#include <stdlib.h>

#include "../src/polyphony.h"

#define ASSERT(condition) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "%s:%d: assertion failed: %s\n", \
                    __FILE__, __LINE__, #condition); \
            exit(1); \
        } \
    } while (0)

static void test_effective_channels()
{
    ASSERT(wintoid::polyphony::effective_channels(-3) == 1);
    ASSERT(wintoid::polyphony::effective_channels(0) == 1);
    ASSERT(wintoid::polyphony::effective_channels(1) == 1);
    ASSERT(wintoid::polyphony::effective_channels(8) == 8);
    ASSERT(wintoid::polyphony::effective_channels(16) == 16);
    ASSERT(wintoid::polyphony::effective_channels(99) == 16);
}

static void test_broadcast_lane()
{
    ASSERT(wintoid::polyphony::broadcast_lane(7, 0) == 0);
    ASSERT(wintoid::polyphony::broadcast_lane(7, 1) == 0);
    ASSERT(wintoid::polyphony::broadcast_lane(0, 3) == 0);
    ASSERT(wintoid::polyphony::broadcast_lane(1, 3) == 1);
    ASSERT(wintoid::polyphony::broadcast_lane(2, 3) == 2);
    ASSERT(wintoid::polyphony::broadcast_lane(3, 3) == 0);

    const int expected[8] = {0, 1, 2, 0, 0, 0, 0, 0};
    for (int lane = 0; lane < 8; ++lane)
        ASSERT(wintoid::polyphony::broadcast_lane(lane, 3) == expected[lane]);
}

static void assert_transition(int previous, int current,
                              const bool expected[16])
{
    bool observed[16] = {};
    wintoid::polyphony::reset_changed_lanes(
        previous, current, [&](int lane) { observed[lane] = true; });
    for (int lane = 0; lane < 16; ++lane)
        ASSERT(observed[lane] == expected[lane]);
}

static void test_changed_lane_ranges()
{
    bool initial[16] = {};
    initial[0] = true;
    assert_transition(0, 1, initial);

    bool growth[16] = {};
    growth[2] = growth[3] = growth[4] = true;
    assert_transition(2, 5, growth);

    bool shrink[16] = {};
    shrink[2] = shrink[3] = shrink[4] = true;
    assert_transition(5, 2, shrink);

    bool unchanged[16] = {};
    assert_transition(8, 8, unchanged);

    bool upperBoundary[16] = {};
    upperBoundary[15] = true;
    assert_transition(15, 16, upperBoundary);
}

int main()
{
    test_effective_channels();
    test_broadcast_lane();
    test_changed_lane_ranges();
    printf("polyphony helper tests passed\n");
    return 0;
}
