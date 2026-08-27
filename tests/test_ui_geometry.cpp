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

#include "../src/ui_geometry.h"

TEST(negative_values_are_treated_as_zero)
{
    ASSERT_NEAR(wintoid::ui::nonnegative(-1.5f), 0.0f, 1e-7f);
    ASSERT_NEAR(wintoid::ui::nonnegative(2.0f), 2.0f, 1e-7f);
    ASSERT_NEAR(wintoid::ui::stroke_inset(-0.5f), 0.0f, 1e-7f);
    ASSERT_NEAR(wintoid::ui::inset_extent(10.0f, -2.0f), 10.0f, 1e-7f);
}

TEST(stroke_inset_returns_half_a_nonnegative_stroke_width)
{
    ASSERT_NEAR(wintoid::ui::stroke_inset(0.5f), 0.25f, 1e-7f);
    ASSERT_NEAR(wintoid::ui::stroke_inset(1.0f), 0.5f, 1e-7f);
    ASSERT_NEAR(wintoid::ui::stroke_inset(0.0f), 0.0f, 1e-7f);
}

TEST(inset_extent_subtracts_the_full_stroke_width)
{
    ASSERT_NEAR(wintoid::ui::inset_extent(10.0f, 0.5f), 9.5f, 1e-7f);
    ASSERT_NEAR(wintoid::ui::inset_extent(10.0f, 0.0f), 10.0f, 1e-7f);
}

TEST(over_wide_strokes_never_produce_a_negative_extent)
{
    ASSERT_NEAR(wintoid::ui::inset_extent(3.0f, 5.0f), 0.0f, 1e-7f);
    ASSERT_NEAR(wintoid::ui::inset_extent(0.0f, 0.5f), 0.0f, 1e-7f);
}

TEST(clamp_stroke_center_keeps_line_centres_inside_the_box)
{
    ASSERT_NEAR(wintoid::ui::clamp_stroke_center(0.0f, 10.0f, 0.5f), 0.25f, 1e-7f);
    ASSERT_NEAR(wintoid::ui::clamp_stroke_center(10.0f, 10.0f, 0.5f), 9.75f, 1e-7f);
    ASSERT_NEAR(wintoid::ui::clamp_stroke_center(5.0f, 10.0f, 0.5f), 5.0f, 1e-7f);
}

TEST(clamp_stroke_center_handles_degenerate_boxes_deterministically)
{
    ASSERT_NEAR(wintoid::ui::clamp_stroke_center(4.0f, 0.0f, 0.5f), 0.0f, 1e-7f);
    ASSERT_NEAR(wintoid::ui::clamp_stroke_center(0.0f, 3.0f, 5.0f), 1.5f, 1e-7f);
    ASSERT_NEAR(wintoid::ui::clamp_stroke_center(2.0f, 3.0f, 5.0f), 1.5f, 1e-7f);
}

int main()
{
    run_negative_values_are_treated_as_zero();
    run_stroke_inset_returns_half_a_nonnegative_stroke_width();
    run_inset_extent_subtracts_the_full_stroke_width();
    run_over_wide_strokes_never_produce_a_negative_extent();
    run_clamp_stroke_center_keeps_line_centres_inside_the_box();
    run_clamp_stroke_center_handles_degenerate_boxes_deterministically();
    printf("%d/%d tests passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
