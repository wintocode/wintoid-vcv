#include <stdio.h>
#include <stdlib.h>

#include "../src/VortexV2/runtime.h"

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) \
    static void test_##name(); \
    static void run_##name() { \
        tests_run++; \
        printf("  %s ... ", #name); \
        test_##name(); \
        tests_passed++; \
        printf("PASS\n"); \
    } \
    static void test_##name()

#define ASSERT(cond) \
    do { if (!(cond)) { \
        printf("FAIL\n    %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        exit(1); \
    } } while(0)

TEST(disconnected_outputs_are_not_selected_for_set_channels_or_processing)
{
    bool wasConnected = false;
    int setChannelsCalls = 0;
    int processCalls = 0;
    int resetCalls = 0;

    const bool connected = vortex_v2::runtime::select_output_branch(
        false, wasConnected, 4,
        [&](int channels) {
            ASSERT(channels == 4);
            setChannelsCalls++;
        },
        [&]() { resetCalls++; });

    if (connected) {
        for (int lane = 0; lane < 4; ++lane)
            processCalls++;
    }

    ASSERT(!connected);
    ASSERT(!wasConnected);
    ASSERT(setChannelsCalls == 0);
    ASSERT(processCalls == 0);
    ASSERT(resetCalls == 0);
}

TEST(disconnect_transition_resets_once_and_reconnect_enables_processing_again)
{
    bool wasConnected = false;
    int setChannelsCalls = 0;
    int resetCalls = 0;
    int processCalls = 0;

    bool connected = vortex_v2::runtime::select_output_branch(
        true, wasConnected, 3,
        [&](int channels) {
            ASSERT(channels == 3);
            setChannelsCalls++;
        },
        [&]() { resetCalls++; });
    ASSERT(connected);
    ASSERT(wasConnected);
    if (connected) {
        for (int lane = 0; lane < 3; ++lane)
            processCalls++;
    }

    connected = vortex_v2::runtime::select_output_branch(
        false, wasConnected, 3,
        [&](int channels) {
            ASSERT(channels == 3);
            setChannelsCalls++;
        },
        [&]() { resetCalls++; });
    ASSERT(!connected);
    ASSERT(!wasConnected);
    ASSERT(resetCalls == 1);

    connected = vortex_v2::runtime::select_output_branch(
        false, wasConnected, 3,
        [&](int channels) {
            ASSERT(channels == 3);
            setChannelsCalls++;
        },
        [&]() { resetCalls++; });
    ASSERT(!connected);
    ASSERT(resetCalls == 1);

    connected = vortex_v2::runtime::select_output_branch(
        true, wasConnected, 2,
        [&](int channels) {
            ASSERT(channels == 2);
            setChannelsCalls++;
        },
        [&]() { resetCalls++; });
    ASSERT(connected);
    ASSERT(wasConnected);
    if (connected) {
        for (int lane = 0; lane < 2; ++lane)
            processCalls++;
    }

    ASSERT(setChannelsCalls == 2);
    ASSERT(resetCalls == 1);
    ASSERT(processCalls == 5);
}

TEST(channel_count_transitions_reset_only_the_changed_lanes)
{
    bool observed[wintoid::polyphony::MAX_CHANNELS] = {};

    vortex_v2::runtime::prepare_lanes(2, 5, [&](int lane) {
        observed[lane] = true;
    });
    ASSERT(!observed[0]);
    ASSERT(!observed[1]);
    ASSERT(observed[2]);
    ASSERT(observed[3]);
    ASSERT(observed[4]);
    ASSERT(!observed[5]);

    for (int lane = 0; lane < wintoid::polyphony::MAX_CHANNELS; ++lane)
        observed[lane] = false;

    vortex_v2::runtime::prepare_lanes(5, 2, [&](int lane) {
        observed[lane] = true;
    });
    ASSERT(!observed[0]);
    ASSERT(!observed[1]);
    ASSERT(observed[2]);
    ASSERT(observed[3]);
    ASSERT(observed[4]);
    ASSERT(!observed[5]);
}

TEST(mono_and_shorter_polyphonic_cv_values_broadcast_by_module_lane_mapping)
{
    const float monoCv[1] = {2.5f};
    for (int lane = 0; lane < 6; ++lane) {
        ASSERT(vortex_v2::runtime::read_broadcast(
                   lane, 1, [&](int sourceLane) { return monoCv[sourceLane]; })
               == 2.5f);
    }

    const float shortPolyCv[3] = {1.0f, -2.0f, 3.5f};
    const float expected[8] = {1.0f, -2.0f, 3.5f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f};
    for (int lane = 0; lane < 8; ++lane) {
        ASSERT(vortex_v2::runtime::read_broadcast(
                   lane, 3,
                   [&](int sourceLane) { return shortPolyCv[sourceLane]; })
               == expected[lane]);
    }
}

int main()
{
    run_disconnected_outputs_are_not_selected_for_set_channels_or_processing();
    run_disconnect_transition_resets_once_and_reconnect_enables_processing_again();
    run_channel_count_transitions_reset_only_the_changed_lanes();
    run_mono_and_shorter_polyphonic_cv_values_broadcast_by_module_lane_mapping();
    printf("%d/%d tests passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
