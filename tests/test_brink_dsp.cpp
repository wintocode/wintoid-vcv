#include <stdio.h>
#include <stdlib.h>
#include <math.h>

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
    do { float _a=(a), _b=(b); if (fabsf(_a-_b) > (eps)) { \
        printf("FAIL\n    %s:%d: %f != %f (eps=%f)\n", \
               __FILE__, __LINE__, (double)_a, (double)_b, (double)(eps)); \
        exit(1); \
    } } while(0)

#include "../src/Brink/dsp.h"

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

int main()
{
    run_window_frame_nominal();
    run_position_is_bipolar_and_clamped();
    run_width_is_sanitized_and_clamped();
    run_initial_region_includes_exact_boundaries();
    run_region_has_one_millivolt_hysteresis();
    run_non_finite_signal_and_center_are_safe();
    printf("%d/%d tests passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
