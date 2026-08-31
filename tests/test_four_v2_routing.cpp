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
    do { float _a=(a), _b=(b); if (!isfinite(_a) || !isfinite(_b) || fabsf(_a-_b) > (eps)) { \
        printf("FAIL\n    %s:%d: %f != %f (eps=%f)\n", \
               __FILE__, __LINE__, (double)_a, (double)_b, (double)(eps)); \
        exit(1); \
    } } while(0)

#include "../src/FourV2/routing.h"
#include "../src/FourV2/layout.h"

static four_v2::RoutingLayout layout_for(int algorithm)
{
    return four_v2::make_routing_layout(
        four_v2::ALGORITHMS[algorithm],
        four_v2_layout::ROUTING_DISPLAY_WIDTH,
        four_v2_layout::ROUTING_DISPLAY_HEIGHT,
        four_v2_layout::ROUTING_NODE_RADIUS,
        four_v2_layout::ROUTING_NODE_HORIZONTAL_MARGIN,
        four_v2_layout::ROUTING_NODE_VERTICAL_MARGIN);
}

static const four_v2::RoutingPath* find_path(
    const four_v2::RoutingLayout& layout, int source, int destination)
{
    for (int index = 0; index < layout.pathCount; ++index) {
        const four_v2::RoutingPath& path = layout.paths[index];
        if (path.source == source && path.destination == destination)
            return &path;
    }
    return nullptr;
}

static const four_v2::RoutingPath* find_display_path(
    const four_v2::RoutingLayout& layout, int source, int destination)
{
    for (int index = 0; index < layout.displayPathCount; ++index) {
        const four_v2::RoutingPath& path = layout.displayPaths[index];
        if (path.source == source && path.destination == destination)
            return &path;
    }
    return nullptr;
}

static bool has_horizontal_overlap(
    const four_v2::RoutingPath& first,
    const four_v2::RoutingPath& second)
{
    for (int firstIndex = 0;
         firstIndex + 1 < first.pointCount; ++firstIndex) {
        const four_v2::RoutingPoint& firstStart = first.points[firstIndex];
        const four_v2::RoutingPoint& firstEnd = first.points[firstIndex + 1];
        if (firstStart.y != firstEnd.y || firstStart.x >= firstEnd.x)
            continue;
        for (int secondIndex = 0;
             secondIndex + 1 < second.pointCount; ++secondIndex) {
            const four_v2::RoutingPoint& secondStart = second.points[secondIndex];
            const four_v2::RoutingPoint& secondEnd = second.points[secondIndex + 1];
            if (secondStart.y != secondEnd.y || secondStart.x >= secondEnd.x)
                continue;
            if (firstStart.y != secondStart.y)
                continue;
            const float overlapStart = firstStart.x > secondStart.x
                ? firstStart.x : secondStart.x;
            const float overlapEnd = firstEnd.x < secondEnd.x
                ? firstEnd.x : secondEnd.x;
            if (overlapEnd - overlapStart > 0.001f)
                return true;
        }
    }
    return false;
}

static bool passes_through_unrelated_node(
    const four_v2::RoutingLayout& layout,
    const four_v2::RoutingPath& path)
{
    for (int op = 0; op < four_v2::OPERATOR_COUNT; ++op) {
        if (op == path.source || op == path.destination)
            continue;
        for (int pointIndex = 0; pointIndex < path.pointCount; ++pointIndex) {
            const four_v2::RoutingPoint& point = path.points[pointIndex];
            const float dx = point.x - layout.nodes[op].x;
            const float dy = point.y - layout.nodes[op].y;
            if (dx * dx + dy * dy < 1.5f * 1.5f)
                return true;
        }
    }
    return false;
}

TEST(serial_algorithm_uses_one_left_to_right_slice)
{
    const four_v2::RoutingLayout layout = layout_for(0);
    ASSERT(layout.rowCount == 1);
    ASSERT(layout.nodes[0].row == 0);
    ASSERT(layout.nodes[1].row == 0);
    ASSERT(layout.nodes[2].row == 0);
    ASSERT(layout.nodes[3].row == 0);
    ASSERT(layout.nodes[3].x < layout.nodes[2].x);
    ASSERT(layout.nodes[2].x < layout.nodes[1].x);
    ASSERT(layout.nodes[1].x < layout.nodes[0].x);
    ASSERT(find_path(layout, 3, 2) != nullptr);
    ASSERT(find_path(layout, 2, 1) != nullptr);
    ASSERT(find_path(layout, 1, 0) != nullptr);
    ASSERT(find_path(layout, 0, four_v2::ROUTING_OUTPUT) != nullptr);
}

TEST(branching_algorithms_keep_the_main_chain_on_top)
{
    const four_v2::RoutingLayout algo2 = layout_for(1);
    ASSERT(algo2.nodes[2].row == 0);
    ASSERT(algo2.nodes[1].row == 0);
    ASSERT(algo2.nodes[0].row == 0);
    ASSERT(algo2.nodes[3].row == 1);
    ASSERT(find_path(algo2, 2, 1) != nullptr);
    ASSERT(find_path(algo2, 1, 0) != nullptr);
    ASSERT(find_path(algo2, 3, 1) != nullptr);
    ASSERT(find_path(algo2, 3, 0) == nullptr);

    const four_v2::RoutingLayout algo3 = layout_for(2);
    ASSERT(algo3.nodes[3].row == 0);
    ASSERT(algo3.nodes[1].row == 0);
    ASSERT(algo3.nodes[0].row == 0);
    ASSERT(algo3.nodes[2].row == 1);
}

TEST(branch_nodes_are_aligned_with_their_destination_stage)
{
    const four_v2::RoutingLayout algo3 = layout_for(2);
    ASSERT_NEAR(algo3.nodes[2].x, algo3.nodes[1].x, 1e-6f);
    ASSERT(algo3.nodes[2].row > algo3.nodes[1].row);
}

TEST(exception_paths_do_not_hit_nodes_or_share_ambiguous_runs)
{
    const int algorithms[] = {2, 8, 14};
    for (int index = 0;
         index < static_cast<int>(sizeof(algorithms) / sizeof(algorithms[0]));
         ++index) {
        const four_v2::RoutingLayout layout = layout_for(algorithms[index]);
        for (int firstIndex = 0;
             firstIndex < layout.displayPathCount; ++firstIndex) {
            const four_v2::RoutingPath& first = layout.displayPaths[firstIndex];
            if (first.carrier)
                continue;
            ASSERT(!passes_through_unrelated_node(layout, first));
            for (int secondIndex = firstIndex + 1;
                 secondIndex < layout.displayPathCount; ++secondIndex) {
                const four_v2::RoutingPath& second =
                    layout.displayPaths[secondIndex];
                if (second.carrier)
                    continue;
                ASSERT(!has_horizontal_overlap(first, second));
            }
        }
    }
}

TEST(non_exception_algorithms_keep_centered_ports_and_direct_lanes)
{
    const int algorithms[] = {0, 1, 3, 4, 5, 6, 7, 9};
    for (int index = 0;
         index < static_cast<int>(sizeof(algorithms) / sizeof(algorithms[0]));
         ++index) {
        const four_v2::RoutingLayout layout = layout_for(algorithms[index]);
        for (int pathIndex = 0;
             pathIndex < layout.pathCount; ++pathIndex) {
            const four_v2::RoutingPath& path = layout.paths[pathIndex];
            if (path.carrier)
                continue;
            ASSERT_NEAR(path.points[0].y,
                        layout.nodes[path.source].y, 1e-6f);
            if (path.pointCount == 4) {
                const float midpoint = path.points[0].x
                    + (path.points[path.pointCount - 1].x
                       - path.points[0].x) * 0.5f;
                ASSERT_NEAR(path.points[1].x, midpoint, 1e-6f);
            }
            ASSERT_NEAR(path.points[path.pointCount - 1].y,
                        layout.nodes[path.destination].y, 1e-6f);
        }
    }
}

TEST(algorithm_9_display_is_one_merge_then_one_split)
{
    const four_v2::RoutingLayout layout = layout_for(8);
    ASSERT(layout.displayPathCount == 7);
    int mergeInputs = 0;
    int splitOutputs = 0;
    int mergeToSplit = 0;
    int arrowCount = 0;
    four_v2::RoutingPoint mergePoint = {};
    four_v2::RoutingPoint splitPoint = {};
    for (int pathIndex = 0;
         pathIndex < layout.displayPathCount; ++pathIndex) {
        const four_v2::RoutingPath& path = layout.displayPaths[pathIndex];
        if (path.arrow)
            ++arrowCount;
        if ((path.source == 2 || path.source == 3)
            && path.destination == four_v2::ROUTING_MERGE) {
            ++mergeInputs;
            ASSERT(!path.arrow);
            mergePoint = path.points[path.pointCount - 1];
        } else if (path.source == four_v2::ROUTING_SPLIT
                   && (path.destination == 0 || path.destination == 1)) {
            ++splitOutputs;
            ASSERT(path.arrow);
            if (splitOutputs > 1) {
                ASSERT_NEAR(path.points[0].x, splitPoint.x, 1e-6f);
                ASSERT_NEAR(path.points[0].y, splitPoint.y, 1e-6f);
            }
            splitPoint = path.points[0];
        } else if (path.source == four_v2::ROUTING_MERGE
                   && path.destination == four_v2::ROUTING_SPLIT) {
            ++mergeToSplit;
            ASSERT(!path.arrow);
            ASSERT(path.pointCount == 2);
        }
    }
    ASSERT(mergeInputs == 2);
    ASSERT(splitOutputs == 2);
    ASSERT(mergeToSplit == 1);
    ASSERT(arrowCount == 4);
    ASSERT_NEAR(mergePoint.x, layout.displayPaths[2].points[0].x, 1e-6f);
    ASSERT_NEAR(mergePoint.y, layout.displayPaths[2].points[0].y, 1e-6f);
    ASSERT_NEAR(splitPoint.x, layout.displayPaths[2].points[1].x, 1e-6f);
    ASSERT_NEAR(splitPoint.y, layout.displayPaths[2].points[1].y, 1e-6f);
}

TEST(parallel_carrier_chains_get_separate_slices)
{
    const four_v2::RoutingLayout algo4 = layout_for(3);
    ASSERT(algo4.nodes[1].row == 0);
    ASSERT(algo4.nodes[0].row == 0);
    ASSERT(algo4.nodes[3].row == 1);
    ASSERT(algo4.nodes[2].row == 1);
}

TEST(multi_carrier_algorithms_are_ordered_ascending_top_to_bottom)
{
    const int threeCarrierAlgorithms[] = {4, 5, 12};
    for (int index = 0;
         index < static_cast<int>(sizeof(threeCarrierAlgorithms)
                                  / sizeof(threeCarrierAlgorithms[0]));
         ++index) {
        const four_v2::RoutingLayout layout =
            layout_for(threeCarrierAlgorithms[index]);
        ASSERT(layout.rowCount == 3);
        ASSERT(layout.nodes[0].row == 0);
        ASSERT(layout.nodes[1].row == 1);
        ASSERT(layout.nodes[2].row == 2);
        ASSERT(layout.nodes[0].y < layout.nodes[1].y);
        ASSERT(layout.nodes[1].y < layout.nodes[2].y);
    }

    const four_v2::RoutingLayout algo5 = layout_for(4);
    ASSERT(algo5.nodes[3].row == 1);

    const four_v2::RoutingLayout algo6 = layout_for(5);
    ASSERT(algo6.nodes[3].row == 2);

    const four_v2::RoutingLayout algo13 = layout_for(12);
    ASSERT(algo13.nodes[3].row == 1);

    const four_v2::RoutingLayout algo7 = layout_for(6);
    ASSERT(algo7.rowCount == 4);
    for (int op = 0; op < four_v2::OPERATOR_COUNT; ++op) {
        ASSERT(algo7.nodes[op].row == op);
        ASSERT(find_path(algo7, op, four_v2::ROUTING_OUTPUT) != nullptr);
    }
    ASSERT(algo7.nodes[0].y < algo7.nodes[1].y);
    ASSERT(algo7.nodes[1].y < algo7.nodes[2].y);
    ASSERT(algo7.nodes[2].y < algo7.nodes[3].y);
}

TEST(algorithms_9_and_10_are_ordered_ascending_top_to_bottom)
{
    const four_v2::RoutingLayout algo9 = layout_for(8);
    ASSERT(algo9.nodes[0].row == 0);
    ASSERT(algo9.nodes[1].row == 1);
    ASSERT(algo9.nodes[2].row == 0);
    ASSERT(algo9.nodes[3].row == 1);
    ASSERT(algo9.nodes[0].y < algo9.nodes[1].y);
    ASSERT(algo9.nodes[2].y < algo9.nodes[3].y);

    const four_v2::RoutingLayout algo10 = layout_for(9);
    ASSERT(algo10.nodes[0].row == 0);
    ASSERT(algo10.nodes[1].row == 0);
    ASSERT(algo10.nodes[2].row == 1);
    ASSERT(algo10.nodes[3].row == 2);
    ASSERT(algo10.nodes[2].y < algo10.nodes[3].y);
}

TEST(algorithm_15_marks_the_branch_join_with_an_arrow)
{
    const four_v2::RoutingLayout layout = layout_for(14);
    const four_v2::RoutingPath* shared = find_display_path(layout, 2, 0);
    const four_v2::RoutingPath* trunk = find_display_path(
        layout, 3, four_v2::ROUTING_SPLIT);
    const four_v2::RoutingPath* privateBranch = find_display_path(
        layout, four_v2::ROUTING_SPLIT, 1);
    const four_v2::RoutingPath* joinedBranch = find_display_path(
        layout, four_v2::ROUTING_SPLIT, four_v2::ROUTING_MERGE);

    ASSERT(shared != nullptr);
    ASSERT(trunk != nullptr);
    ASSERT(privateBranch != nullptr);
    ASSERT(joinedBranch != nullptr);
    ASSERT(shared->arrow);
    ASSERT(!trunk->arrow);
    ASSERT(privateBranch->arrow);
    ASSERT(joinedBranch->arrow);
    ASSERT(joinedBranch->pointCount == 2);
    ASSERT(find_display_path(layout, 3, 0) == nullptr);

    const four_v2::RoutingPoint& beforeJoin =
        joinedBranch->points[joinedBranch->pointCount - 2];
    const four_v2::RoutingPoint& join =
        joinedBranch->points[joinedBranch->pointCount - 1];
    ASSERT_NEAR(beforeJoin.x, join.x, 1e-6f);
    ASSERT(beforeJoin.y > join.y);
    ASSERT_NEAR(join.y, shared->points[0].y, 1e-6f);
    ASSERT(join.x > shared->points[0].x);
    ASSERT(join.x < shared->points[shared->pointCount - 1].x);
}

TEST(all_paths_stay_inside_the_display_and_flow_forward)
{
    for (int algorithm = 0;
         algorithm < four_v2::ALGORITHM_COUNT; ++algorithm) {
        const four_v2::RoutingLayout layout = layout_for(algorithm);
        ASSERT(layout.rowCount >= 1);
        ASSERT(layout.rowCount <= four_v2::ROUTING_MAX_ROWS);
        for (int op = 0; op < four_v2::OPERATOR_COUNT; ++op) {
            ASSERT(layout.nodes[op].x >= 0.f);
            ASSERT(layout.nodes[op].x <= 48.f);
            ASSERT(layout.nodes[op].y >= 0.f);
            ASSERT(layout.nodes[op].y <= 21.f);
        }
        for (int pathIndex = 0;
             pathIndex < layout.pathCount; ++pathIndex) {
            const four_v2::RoutingPath& path = layout.paths[pathIndex];
            ASSERT(path.pointCount >= 2);
            for (int pointIndex = 0;
                 pointIndex < path.pointCount; ++pointIndex) {
                const four_v2::RoutingPoint& point = path.points[pointIndex];
                ASSERT(point.x >= 0.f);
                ASSERT(point.x <= 48.f);
                ASSERT(point.y >= 0.f);
                ASSERT(point.y <= 21.f);
                if (pointIndex > 0)
                    ASSERT(point.x >= path.points[pointIndex - 1].x);
            }
            if (path.carrier)
                ASSERT(path.points[path.pointCount - 1].x
                       > path.points[0].x);
        }
        for (int pathIndex = 0;
             pathIndex < layout.displayPathCount; ++pathIndex) {
            const four_v2::RoutingPath& path = layout.displayPaths[pathIndex];
            ASSERT(path.pointCount >= 2);
            for (int pointIndex = 0;
                 pointIndex < path.pointCount; ++pointIndex) {
                const four_v2::RoutingPoint& point = path.points[pointIndex];
                ASSERT(point.x >= 0.f);
                ASSERT(point.x <= 48.f);
                ASSERT(point.y >= 0.f);
                ASSERT(point.y <= 21.f);
                if (pointIndex > 0)
                    ASSERT(point.x >= path.points[pointIndex - 1].x);
            }
        }
    }
}

TEST(path_table_matches_every_canonical_edge_and_carrier)
{
    for (int algorithm = 0;
         algorithm < four_v2::ALGORITHM_COUNT; ++algorithm) {
        const four_v2::Algorithm& source = four_v2::ALGORITHMS[algorithm];
        const four_v2::RoutingLayout layout = layout_for(algorithm);
        int expectedPathCount = 0;
        for (int src = 0; src < four_v2::OPERATOR_COUNT; ++src) {
            for (int dst = 0; dst < four_v2::OPERATOR_COUNT; ++dst) {
                if (!source.mod[src][dst])
                    continue;
                expectedPathCount++;
                const four_v2::RoutingPath* path = find_path(layout, src, dst);
                ASSERT(path != nullptr);
                ASSERT(!path->carrier);
            }
            if (source.carrier[src]) {
                expectedPathCount++;
                const four_v2::RoutingPath* path = find_path(
                    layout, src, four_v2::ROUTING_OUTPUT);
                ASSERT(path != nullptr);
                ASSERT(path->carrier);
            }
        }
        ASSERT(layout.pathCount == expectedPathCount);
    }
}

int main()
{
    printf("Four V2 routing layout tests:\n");
    run_serial_algorithm_uses_one_left_to_right_slice();
    run_branching_algorithms_keep_the_main_chain_on_top();
    run_branch_nodes_are_aligned_with_their_destination_stage();
    run_exception_paths_do_not_hit_nodes_or_share_ambiguous_runs();
    run_non_exception_algorithms_keep_centered_ports_and_direct_lanes();
    run_algorithm_9_display_is_one_merge_then_one_split();
    run_parallel_carrier_chains_get_separate_slices();
    run_multi_carrier_algorithms_are_ordered_ascending_top_to_bottom();
    run_algorithms_9_and_10_are_ordered_ascending_top_to_bottom();
    run_algorithm_15_marks_the_branch_join_with_an_arrow();
    run_all_paths_stay_inside_the_display_and_flow_forward();
    run_path_table_matches_every_canonical_edge_and_carrier();

    printf("\n%d/%d tests passed.\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
