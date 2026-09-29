#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <thread>

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

#define ASSERT_NEAR(a, b, eps) \
    do { float _a=(a), _b=(b); if (!(fabsf(_a-_b) <= (eps))) { \
        printf("FAIL\n    %s:%d: %f != %f (eps=%f)\n", \
               __FILE__, __LINE__, (double)_a, (double)_b, (double)(eps)); \
        exit(1); \
    } } while(0)

#include "../src/Brink/dsp.h"

static_assert(
    brink::AtomicDisplayFrame::WRITER_BEGIN_ORDER == std::memory_order_acq_rel,
    "the odd sequence marker must precede payload stores");
static_assert(
    brink::AtomicDisplayFrame::WRITER_PAYLOAD_FENCE_ORDER ==
        std::memory_order_release,
    "payload stores must carry the odd marker to validating readers");
static_assert(
    brink::AtomicDisplayFrame::WRITER_END_ORDER == std::memory_order_release,
    "the even sequence marker must publish the payload");
static_assert(
    brink::AtomicDisplayFrame::READER_BEGIN_ORDER == std::memory_order_acquire,
    "payload reads must follow the first sequence read");
static_assert(
    brink::AtomicDisplayFrame::READER_VALIDATE_FENCE_ORDER ==
        std::memory_order_acquire,
    "payload reads must complete before validation");
static_assert(
    brink::AtomicDisplayFrame::READER_END_ORDER == std::memory_order_relaxed,
    "the acquire fence supplies validation ordering");

TEST(window_frame_nominal)
{
    brink::WindowFrame f = brink::make_window(0.f, 1.f, 4.f);
    ASSERT_NEAR(f.lower, -1.f, 1e-6f);
    ASSERT_NEAR(f.upper, 3.f, 1e-6f);
    ASSERT_NEAR(f.position, -2.5f, 1e-6f);
}

TEST(position_is_bipolar_and_clamped)
{
    ASSERT_NEAR(brink::make_window(-2.f, 0.f, 2.f).position, -5.f, 1e-6f);
    ASSERT_NEAR(brink::make_window(0.f, 0.f, 2.f).position, 0.f, 1e-6f);
    ASSERT_NEAR(brink::make_window(2.f, 0.f, 2.f).position, 5.f, 1e-6f);
}

TEST(width_is_sanitized_and_clamped)
{
    ASSERT_NEAR(brink::make_window(0.f, 0.f, -5.f).width, brink::MIN_WIDTH, 1e-7f);
    ASSERT_NEAR(brink::make_window(0.f, 0.f, 50.f).width, brink::MAX_WIDTH, 1e-6f);
    ASSERT_NEAR(brink::make_window(0.f, 0.f, NAN).width, brink::MIN_WIDTH, 1e-7f);
}

TEST(initial_region_includes_exact_boundaries)
{
    ASSERT(brink::classify_initial(brink::make_window(-1.f, 0.f, 2.f)) == brink::INSIDE);
    ASSERT(brink::classify_initial(brink::make_window(1.f, 0.f, 2.f)) == brink::INSIDE);
}

TEST(region_has_one_millivolt_hysteresis)
{
    brink::Region r = brink::BELOW;
    r = brink::advance_region(r, brink::make_window(-0.9995f, 0.f, 2.f));
    ASSERT(r == brink::BELOW);
    r = brink::advance_region(r, brink::make_window(-0.9989f, 0.f, 2.f));
    ASSERT(r == brink::INSIDE);
    r = brink::advance_region(r, brink::make_window(-1.0011f, 0.f, 2.f));
    ASSERT(r == brink::BELOW);
}

TEST(non_finite_signal_and_center_are_safe)
{
    brink::WindowFrame f = brink::make_window(INFINITY, NAN, 2.f);
    ASSERT(isfinite(f.position));
    ASSERT_NEAR(f.signal, 0.f, 1e-6f);
    ASSERT_NEAR(f.center, 0.f, 1e-6f);
}

TEST(display_voltage_uses_fixed_bipolar_range)
{
    ASSERT_NEAR(brink::normalize_display_voltage(-10.f), 0.f, 1e-6f);
    ASSERT_NEAR(brink::normalize_display_voltage(0.f), 0.5f, 1e-6f);
    ASSERT_NEAR(brink::normalize_display_voltage(10.f), 1.f, 1e-6f);
    ASSERT_NEAR(brink::normalize_display_voltage(-20.f), 0.f, 1e-6f);
    ASSERT_NEAR(brink::normalize_display_voltage(20.f), 1.f, 1e-6f);
    ASSERT_NEAR(brink::normalize_display_voltage(NAN), 0.5f, 1e-6f);
}

TEST(window_output_exposes_window_for_display)
{
    brink::WindowState s;
    brink::WindowOutput o = brink::process_window(s, 7.f, 1.f, 4.f,
                                                   1.f / 48000.f);
    ASSERT_NEAR(o.frame.signal, 7.f, 1e-6f);
    ASSERT_NEAR(o.frame.center, 1.f, 1e-6f);
    ASSERT_NEAR(o.frame.lower, -1.f, 1e-6f);
    ASSERT_NEAR(o.frame.upper, 3.f, 1e-6f);
}

TEST(display_frame_snapshot_round_trips_visible_geometry)
{
    brink::WindowFrame source = brink::make_window(7.f, 1.f, 4.f);
    brink::AtomicDisplayFrame snapshot;
    snapshot.store(source);
    brink::WindowDisplayFrame displayed = snapshot.load();
    ASSERT_NEAR(displayed.signal, 7.f, 1e-6f);
    ASSERT_NEAR(displayed.center, 1.f, 1e-6f);
    ASSERT_NEAR(displayed.lower, -1.f, 1e-6f);
    ASSERT_NEAR(displayed.upper, 3.f, 1e-6f);
}

TEST(concurrent_snapshot_never_accepts_torn_frames)
{
    brink::AtomicDisplayFrame snapshot;

    brink::WindowFrame initial;
    initial.signal = 0.f;
    initial.center = 1000000.f;
    initial.lower = 2000000.f;
    initial.upper = 3000000.f;
    snapshot.store(initial);

    const int publications = 200000;
    std::atomic<bool> done(false);
    std::atomic<bool> readerReady(false);
    std::atomic<bool> publicationPhaseActive(false);
    std::atomic<bool> readerObservedPublicationPhase(false);
    std::atomic<int> violations(0);
    std::atomic<int> accepted(0);

    std::thread reader([&snapshot, &done, &readerReady,
                        &publicationPhaseActive,
                        &readerObservedPublicationPhase,
                        &violations, &accepted]() {
        const brink::WindowDisplayFrame initialFrame = snapshot.load();
        ++accepted;
        if (initialFrame.center - initialFrame.signal != 1000000.f
            || initialFrame.lower - initialFrame.signal != 2000000.f
            || initialFrame.upper - initialFrame.signal != 3000000.f) {
            ++violations;
        }
        readerReady.store(true, std::memory_order_release);

        while (!publicationPhaseActive.load(std::memory_order_acquire)) {}

        const brink::WindowDisplayFrame postPhaseFrame = snapshot.load();
        ++accepted;
        if (postPhaseFrame.center - postPhaseFrame.signal != 1000000.f
            || postPhaseFrame.lower - postPhaseFrame.signal != 2000000.f
            || postPhaseFrame.upper - postPhaseFrame.signal != 3000000.f) {
            ++violations;
        }
        readerObservedPublicationPhase.store(true, std::memory_order_release);

        while (!done.load(std::memory_order_acquire)) {
            const brink::WindowDisplayFrame frame = snapshot.load();
            ++accepted;
            if (frame.center - frame.signal != 1000000.f
                || frame.lower - frame.signal != 2000000.f
                || frame.upper - frame.signal != 3000000.f) {
                ++violations;
            }
        }
    });

    while (!readerReady.load(std::memory_order_acquire)) {}

    std::thread writer([&snapshot, &publicationPhaseActive,
                        &readerObservedPublicationPhase]() {
        for (int i = 0; i < publications; ++i) {
            const float value = static_cast<float>(i % 1000000);
            brink::WindowFrame frame;
            frame.signal = value;
            frame.center = value + 1000000.f;
            frame.lower = value + 2000000.f;
            frame.upper = value + 3000000.f;
            snapshot.store(frame);

            if (i == 0) {
                // The first store has entered the publication phase. Keep
                // the loop in progress until the reader acknowledges a
                // subsequent load.
                publicationPhaseActive.store(true, std::memory_order_release);
                while (!readerObservedPublicationPhase.load(
                    std::memory_order_acquire)) {}
            }
        }
        publicationPhaseActive.store(false, std::memory_order_release);
    });

    writer.join();
    ASSERT(readerObservedPublicationPhase.load(std::memory_order_acquire));
    done.store(true, std::memory_order_release);
    reader.join();

    ASSERT(accepted.load() > 0);
    ASSERT(violations.load() == 0);
}

TEST(display_rate_limiter_updates_on_first_sample_and_at_sixty_hz)
{
    brink::DisplayRateLimiter limiter;
    limiter.reset(48000.f);
    ASSERT(limiter.should_publish());
    for (int sample = 1; sample < 800; ++sample)
        ASSERT(!limiter.should_publish());
    ASSERT(limiter.should_publish());
}

TEST(display_rate_limiter_handles_invalid_sample_rate)
{
    brink::DisplayRateLimiter limiter;
    limiter.reset(NAN);
    ASSERT(limiter.should_publish());
    ASSERT(limiter.should_publish());
}

TEST(initial_sample_is_silent)
{
    brink::WindowState s;
    brink::WindowOutput o = brink::process_window(s, -2.f, 0.f, 2.f, 1.f / 48000.f);
    ASSERT(o.region == brink::BELOW);
    for (int i = 0; i < brink::EVENT_COUNT; ++i) ASSERT(!o.eventHigh[i]);
}

TEST(all_four_directional_events)
{
    brink::WindowState s;
    const float dt = 0.002f;
    brink::process_window(s, -2.f, 0.f, 2.f, dt);
    brink::WindowOutput lowUp = brink::process_window(s, 0.f, 0.f, 2.f, dt);
    ASSERT(lowUp.eventHigh[brink::LOW_UP] && !lowUp.eventHigh[brink::HIGH_UP]);
    brink::WindowOutput highUp = brink::process_window(s, 2.f, 0.f, 2.f, dt);
    ASSERT(highUp.eventHigh[brink::HIGH_UP] && !highUp.eventHigh[brink::LOW_UP]);
    brink::WindowOutput highDown = brink::process_window(s, 0.f, 0.f, 2.f, dt);
    ASSERT(highDown.eventHigh[brink::HIGH_DOWN] && !highDown.eventHigh[brink::LOW_DOWN]);
    brink::WindowOutput lowDown = brink::process_window(s, -2.f, 0.f, 2.f, dt);
    ASSERT(lowDown.eventHigh[brink::LOW_DOWN] && !lowDown.eventHigh[brink::HIGH_DOWN]);
}

TEST(full_window_jump_fires_both_boundaries)
{
    brink::WindowState s;
    brink::process_window(s, -2.f, 0.f, 2.f, 1.f / 48000.f);
    brink::WindowOutput up = brink::process_window(s, 2.f, 0.f, 2.f, 1.f / 48000.f);
    ASSERT(up.eventHigh[brink::LOW_UP]);
    ASSERT(up.eventHigh[brink::HIGH_UP]);
    brink::WindowOutput down = brink::process_window(s, -2.f, 0.f, 2.f, 1.f / 48000.f);
    ASSERT(down.eventHigh[brink::HIGH_DOWN]);
    ASSERT(down.eventHigh[brink::LOW_DOWN]);
}

TEST(window_motion_generates_relative_crossing)
{
    brink::WindowState s;
    brink::process_window(s, 0.f, 2.f, 2.f, 1.f / 48000.f);
    brink::WindowOutput o = brink::process_window(s, 0.f, 0.f, 2.f, 1.f / 48000.f);
    ASSERT(o.eventHigh[brink::LOW_UP]);
}

TEST(event_pulse_lasts_one_millisecond)
{
    brink::WindowState s;
    const float dt = 0.0005f;
    brink::process_window(s, -2.f, 0.f, 2.f, dt);
    ASSERT(brink::process_window(s, 0.f, 0.f, 2.f, dt).eventHigh[brink::LOW_UP]);
    ASSERT(brink::process_window(s, 0.f, 0.f, 2.f, dt).eventHigh[brink::LOW_UP]);
    ASSERT(!brink::process_window(s, 0.f, 0.f, 2.f, dt).eventHigh[brink::LOW_UP]);
}

TEST(reset_silences_and_reinitializes)
{
    brink::WindowState s;
    brink::process_window(s, -2.f, 0.f, 2.f, 1.f / 48000.f);
    brink::process_window(s, 0.f, 0.f, 2.f, 1.f / 48000.f);
    brink::reset(s);
    brink::WindowOutput o = brink::process_window(s, 2.f, 0.f, 2.f, 1.f / 48000.f);
    for (int i = 0; i < brink::EVENT_COUNT; ++i) ASSERT(!o.eventHigh[i]);
}

TEST(logic_truth_table)
{
    brink::LogicState s;
    brink::LogicOutput a = brink::process_logic(s, false, false);
    ASSERT(!a.andGate && !a.orGate && !a.xorGate);
    brink::LogicOutput b = brink::process_logic(s, true, false);
    ASSERT(!b.andGate && b.orGate && b.xorGate);
    brink::LogicOutput c = brink::process_logic(s, true, true);
    ASSERT(c.andGate && c.orGate && !c.xorGate);
}

TEST(toggle_ignores_initial_xor_and_toggles_on_later_rises)
{
    brink::LogicState s;
    ASSERT(!brink::process_logic(s, true, false).stateGate);
    brink::process_logic(s, false, false);
    ASSERT(brink::process_logic(s, true, false).stateGate);
    brink::process_logic(s, false, false);
    ASSERT(!brink::process_logic(s, false, true).stateGate);
}

TEST(logic_reset_clears_toggle)
{
    brink::LogicState s;
    brink::process_logic(s, false, false);
    brink::process_logic(s, true, false);
    brink::reset(s);
    ASSERT(!brink::process_logic(s, true, false).stateGate);
}

TEST(polyphony_channel_rules)
{
    ASSERT(brink::MAX_CHANNELS == wintoid::polyphony::MAX_CHANNELS);
    ASSERT(brink::effective_channels(0)
           == wintoid::polyphony::effective_channels(0));
    ASSERT(brink::broadcast_lane(9, 8)
           == wintoid::polyphony::broadcast_lane(9, 8));
    ASSERT(brink::effective_channels(0) == 1);
    ASSERT(brink::effective_channels(22) == 16);
    ASSERT(brink::logic_channels(1, 8) == 8);
    ASSERT(brink::broadcast_lane(5, 1) == 0);
    ASSERT(brink::broadcast_lane(2, 8) == 2);
    ASSERT(brink::broadcast_lane(9, 8) == 0);
}

int main()
{
    run_window_frame_nominal();
    run_position_is_bipolar_and_clamped();
    run_width_is_sanitized_and_clamped();
    run_initial_region_includes_exact_boundaries();
    run_region_has_one_millivolt_hysteresis();
    run_non_finite_signal_and_center_are_safe();
    run_display_voltage_uses_fixed_bipolar_range();
    run_window_output_exposes_window_for_display();
    run_display_frame_snapshot_round_trips_visible_geometry();
    run_concurrent_snapshot_never_accepts_torn_frames();
    run_display_rate_limiter_updates_on_first_sample_and_at_sixty_hz();
    run_display_rate_limiter_handles_invalid_sample_rate();
    run_initial_sample_is_silent();
    run_all_four_directional_events();
    run_full_window_jump_fires_both_boundaries();
    run_window_motion_generates_relative_crossing();
    run_event_pulse_lasts_one_millisecond();
    run_reset_silences_and_reinitializes();
    run_logic_truth_table();
    run_toggle_ignores_initial_xor_and_toggles_on_later_rises();
    run_logic_reset_clears_toggle();
    run_polyphony_channel_rules();
    printf("%d/%d tests passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
