#ifndef WINTOID_FOUR_V2_ROUTING_H
#define WINTOID_FOUR_V2_ROUTING_H

#include "model.h"

namespace four_v2 {

static const int ROUTING_MAX_ROWS = 4;
static const int ROUTING_MAX_PATHS = 16;
static const int ROUTING_MAX_POINTS = 6;
static const int ROUTING_OUTPUT = -1;
static const int ROUTING_MERGE = -2;
static const int ROUTING_SPLIT = -3;

struct RoutingPoint {
    float x;
    float y;
};

struct RoutingNode {
    float x;
    float y;
    int row;
    int column;
};

struct RoutingPath {
    int source;
    int destination;
    bool carrier;
    bool arrow;
    int pointCount;
    RoutingPoint points[ROUTING_MAX_POINTS];
};

struct RoutingLayout {
    RoutingNode nodes[OPERATOR_COUNT];
    RoutingPath paths[ROUTING_MAX_PATHS];
    int pathCount;
    RoutingPath displayPaths[ROUTING_MAX_PATHS];
    int displayPathCount;
    int rowCount;
    int columnCount;
};

inline bool routing_path_is_better(
    const int candidate[OPERATOR_COUNT], int candidateLength,
    const int best[OPERATOR_COUNT], int bestLength, bool bestFound)
{
    if (!bestFound || candidateLength != bestLength)
        return !bestFound || candidateLength > bestLength;

    for (int index = 0; index < candidateLength; ++index) {
        if (candidate[index] == best[index])
            continue;
        if (index == 0)
            return candidate[index] < best[index];
        if (index == 1)
            return candidate[index] > best[index];
        return candidate[index] < best[index];
    }
    return false;
}

inline void routing_enumerate_paths(
    const Algorithm& algorithm,
    int node,
    int current[OPERATOR_COUNT], int currentLength,
    bool visited[OPERATOR_COUNT],
    int best[OPERATOR_COUNT], int& bestLength, bool& bestFound)
{
    if (currentLength >= OPERATOR_COUNT || visited[node])
        return;

    current[currentLength] = node;
    visited[node] = true;
    const int nextLength = currentLength + 1;
    bool extended = false;
    for (int destination = 0;
         destination < OPERATOR_COUNT; ++destination) {
        if (!algorithm.mod[node][destination] || visited[destination])
            continue;
        extended = true;
        routing_enumerate_paths(
            algorithm, destination, current, nextLength, visited,
            best, bestLength, bestFound);
    }

    if (!extended && routing_path_is_better(
            current, nextLength, best, bestLength, bestFound)) {
        for (int index = 0; index < nextLength; ++index)
            best[index] = current[index];
        bestLength = nextLength;
        bestFound = true;
    }

    visited[node] = false;
}

inline void routing_columns(const Algorithm& algorithm, int columns[OPERATOR_COUNT])
{
    for (int op = 0; op < OPERATOR_COUNT; ++op)
        columns[op] = 0;

    // The canonical table is acyclic, but repeated relaxation keeps this
    // helper defensive if a future table changes its operator ordering.
    for (int pass = 0; pass < OPERATOR_COUNT; ++pass) {
        for (int source = 0; source < OPERATOR_COUNT; ++source) {
            for (int destination = 0;
                 destination < OPERATOR_COUNT; ++destination) {
                if (algorithm.mod[source][destination]
                    && columns[destination] < columns[source] + 1) {
                    columns[destination] = columns[source] + 1;
                }
            }
        }
    }
}

inline bool routing_row_is_occupied(
    const int rows[OPERATOR_COUNT], const int columns[OPERATOR_COUNT],
    int assignedCount, int row, int column)
{
    for (int op = 0; op < assignedCount; ++op) {
        if (rows[op] == row && columns[op] == column)
            return true;
    }
    return false;
}

inline bool routing_algorithms_match(
    const Algorithm& first, const Algorithm& second)
{
    for (int source = 0; source < OPERATOR_COUNT; ++source) {
        if (first.carrier[source] != second.carrier[source])
            return false;
        for (int destination = 0;
             destination < OPERATOR_COUNT; ++destination) {
            if (first.mod[source][destination]
                != second.mod[source][destination]) {
                return false;
            }
        }
    }
    return true;
}

inline bool routing_uses_staged_branch_layout(const Algorithm& algorithm)
{
    return routing_algorithms_match(algorithm, ALGORITHMS[2]);
}

inline bool routing_uses_merged_split_layout(const Algorithm& algorithm)
{
    return routing_algorithms_match(algorithm, ALGORITHMS[8]);
}

inline void routing_add_path(
    RoutingLayout& layout, int source, int destination, bool carrier,
    float width, float nodeRadius)
{
    if (layout.pathCount >= ROUTING_MAX_PATHS)
        return;

    RoutingPath& path = layout.paths[layout.pathCount++];
    path.source = source;
    path.destination = destination;
    path.carrier = carrier;
    path.arrow = true;

    const RoutingNode& sourceNode = layout.nodes[source];
    const float startX = sourceNode.x + nodeRadius;
    const float startY = sourceNode.y;
    const float endX = carrier
        ? width - nodeRadius
        : layout.nodes[destination].x - nodeRadius;
    const float endY = carrier ? startY : layout.nodes[destination].y;

    path.points[0] = {startX, startY};
    if (carrier || startY == endY) {
        path.points[1] = {endX, endY};
        path.pointCount = 2;
        return;
    }

    const float bendX = startX + (endX - startX) * 0.5f;
    path.points[1] = {bendX, startY};
    path.points[2] = {bendX, endY};
    path.points[3] = {endX, endY};
    path.pointCount = 4;
}

inline void routing_add_display_path(
    RoutingLayout& layout, int source, int destination, bool carrier,
    bool arrow, const RoutingPoint points[], int pointCount)
{
    if (layout.displayPathCount >= ROUTING_MAX_PATHS
        || pointCount < 2 || pointCount > ROUTING_MAX_POINTS) {
        return;
    }

    RoutingPath& path = layout.displayPaths[layout.displayPathCount++];
    path.source = source;
    path.destination = destination;
    path.carrier = carrier;
    path.arrow = arrow;
    path.pointCount = pointCount;
    for (int index = 0; index < pointCount; ++index)
        path.points[index] = points[index];
}

inline void routing_make_merged_split_display(
    RoutingLayout& layout, float height, float nodeRadius)
{
    layout.displayPathCount = 0;

    const float sourceX = layout.nodes[2].x + nodeRadius;
    const float targetX = layout.nodes[1].x - nodeRadius;
    const float mergeX = sourceX + (targetX - sourceX) * 0.32f;
    const float splitX = sourceX + (targetX - sourceX) * 0.68f;
    const float mergeY = height * 0.5f;

    const RoutingPoint sourceThree[] = {
        {sourceX, layout.nodes[2].y},
        {mergeX, layout.nodes[2].y},
        {mergeX, mergeY}
    };
    const RoutingPoint sourceFour[] = {
        {sourceX, layout.nodes[3].y},
        {mergeX, layout.nodes[3].y},
        {mergeX, mergeY}
    };
    const RoutingPoint trunk[] = {
        {mergeX, mergeY},
        {splitX, mergeY}
    };
    const RoutingPoint outputTwo[] = {
        {splitX, mergeY},
        {splitX, layout.nodes[1].y},
        {targetX, layout.nodes[1].y}
    };
    const RoutingPoint outputOne[] = {
        {splitX, mergeY},
        {splitX, layout.nodes[0].y},
        {targetX, layout.nodes[0].y}
    };

    routing_add_display_path(
        layout, 2, ROUTING_MERGE, false, false,
        sourceThree, 3);
    routing_add_display_path(
        layout, 3, ROUTING_MERGE, false, false,
        sourceFour, 3);
    routing_add_display_path(
        layout, ROUTING_MERGE, ROUTING_SPLIT, false, false,
        trunk, 2);
    routing_add_display_path(
        layout, ROUTING_SPLIT, 1, false, true,
        outputTwo, 3);
    routing_add_display_path(
        layout, ROUTING_SPLIT, 0, false, true,
        outputOne, 3);

    for (int pathIndex = 0; pathIndex < layout.pathCount; ++pathIndex) {
        const RoutingPath& path = layout.paths[pathIndex];
        if (path.carrier) {
            routing_add_display_path(
                layout, path.source, path.destination, true, true,
                path.points, path.pointCount);
        }
    }
}

inline void routing_make_staged_branch_display(
    RoutingLayout& layout, float nodeRadius)
{
    layout.displayPathCount = 0;
    for (int pathIndex = 0; pathIndex < layout.pathCount; ++pathIndex) {
        const RoutingPath& path = layout.paths[pathIndex];
        if (path.carrier) {
            routing_add_display_path(
                layout, path.source, path.destination, true, true,
                path.points, path.pointCount);
            continue;
        }

        const bool isBranchIntoOutput = path.destination == 0
            && layout.nodes[path.source].row > layout.nodes[path.destination].row;
        if (!isBranchIntoOutput) {
            routing_add_display_path(
                layout, path.source, path.destination, false, true,
                path.points, path.pointCount);
            continue;
        }

        const float startX = layout.nodes[path.source].x + nodeRadius;
        const float endX = layout.nodes[path.destination].x - nodeRadius;
        const float joinX = startX + (endX - startX) * 0.58f;
        const RoutingPoint branch[] = {
            {startX, layout.nodes[path.source].y},
            {joinX, layout.nodes[path.source].y},
            {joinX, layout.nodes[path.destination].y}
        };
        routing_add_display_path(
            layout, path.source, path.destination, false, true,
            branch, 3);
    }
}

inline RoutingLayout make_routing_layout(
    const Algorithm& algorithm,
    float width, float height, float nodeRadius,
    float horizontalMargin, float verticalMargin)
{
    RoutingLayout layout = {};
    int columns[OPERATOR_COUNT] = {};
    routing_columns(algorithm, columns);

    int edgeCount = 0;
    for (int source = 0; source < OPERATOR_COUNT; ++source) {
        for (int destination = 0;
             destination < OPERATOR_COUNT; ++destination) {
            if (algorithm.mod[source][destination])
                ++edgeCount;
        }
    }

    int rows[OPERATOR_COUNT] = {-1, -1, -1, -1};
    int bestPath[OPERATOR_COUNT] = {};
    int bestLength = 0;
    bool bestFound = false;
    if (edgeCount > 0) {
        for (int start = 0; start < OPERATOR_COUNT; ++start) {
            int current[OPERATOR_COUNT] = {};
            bool visited[OPERATOR_COUNT] = {};
            routing_enumerate_paths(
                algorithm, start, current, 0, visited,
                bestPath, bestLength, bestFound);
        }
        for (int index = 0; index < bestLength; ++index)
            rows[bestPath[index]] = 0;
    } else {
        // With no modulation edges, read the four independent carriers from
        // top to bottom as 4, 3, 2, 1.
        rows[3] = 0;
        rows[2] = 1;
        rows[1] = 2;
        rows[0] = 3;
    }

    if (routing_uses_staged_branch_layout(algorithm)) {
        // Keep the branch source in the stage immediately before its
        // destination. This gives algorithms 3 and 4 the same clear shape
        // as the corresponding hand-drawn two-slice diagrams.
        for (int candidate = 0; candidate < OPERATOR_COUNT; ++candidate) {
            if (rows[candidate] >= 0)
                continue;

            int nearestDestinationColumn = OPERATOR_COUNT;
            for (int destination = 0;
                 destination < OPERATOR_COUNT; ++destination) {
                if (algorithm.mod[candidate][destination]
                    && columns[destination] < nearestDestinationColumn) {
                    nearestDestinationColumn = columns[destination];
                }
            }
            if (nearestDestinationColumn < OPERATOR_COUNT
                && columns[candidate] < nearestDestinationColumn - 1) {
                columns[candidate] = nearestDestinationColumn - 1;
            }
        }
    }

    int nextIndependentRow = 1;
    for (int candidate = OPERATOR_COUNT - 1;
         candidate >= 0; --candidate) {
        if (rows[candidate] >= 0)
            continue;

        int preferredRow = -1;
        for (int source = 0; source < OPERATOR_COUNT; ++source) {
            if (algorithm.mod[source][candidate] && rows[source] >= 0) {
                preferredRow = rows[source];
                break;
            }
        }
        if (preferredRow < 0) {
            for (int destination = 0;
                 destination < OPERATOR_COUNT; ++destination) {
                if (algorithm.mod[candidate][destination]
                    && rows[destination] >= 0) {
                    preferredRow = rows[destination];
                    break;
                }
            }
        }
        if (preferredRow < 0)
            preferredRow = nextIndependentRow++;

        while (preferredRow < ROUTING_MAX_ROWS
               && routing_row_is_occupied(
                   rows, columns, OPERATOR_COUNT,
                   preferredRow, columns[candidate])) {
            ++preferredRow;
        }
        if (preferredRow >= ROUTING_MAX_ROWS)
            preferredRow = 0;
        rows[candidate] = preferredRow;
    }

    int rowCount = 1;
    for (int op = 0; op < OPERATOR_COUNT; ++op) {
        if (rows[op] + 1 > rowCount)
            rowCount = rows[op] + 1;
    }
    if (rowCount > ROUTING_MAX_ROWS)
        rowCount = ROUTING_MAX_ROWS;

    int maximumColumn = 0;
    for (int op = 0; op < OPERATOR_COUNT; ++op) {
        if (columns[op] > maximumColumn)
            maximumColumn = columns[op];
    }

    const float leftX = horizontalMargin + nodeRadius;
    const float rightX = width - horizontalMargin - nodeRadius;
    const float usableHeight = height - 2.f * verticalMargin;
    for (int op = 0; op < OPERATOR_COUNT; ++op) {
        const float columnFraction = maximumColumn > 0
            ? (float)columns[op] / (float)maximumColumn : 0.f;
        const float rowFraction = rowCount > 1
            ? (float)rows[op] / (float)(rowCount - 1) : 0.5f;
        layout.nodes[op] = {
            leftX + (rightX - leftX) * columnFraction,
            rowCount > 1
                ? verticalMargin + usableHeight * rowFraction
                : height * 0.5f,
            rows[op],
            columns[op]
        };
    }
    layout.rowCount = rowCount;
    layout.columnCount = maximumColumn + 1;

    for (int source = 0; source < OPERATOR_COUNT; ++source) {
        for (int destination = 0;
             destination < OPERATOR_COUNT; ++destination) {
            if (algorithm.mod[source][destination])
                routing_add_path(
                    layout, source, destination, false,
                    width, nodeRadius);
        }
        if (algorithm.carrier[source])
            routing_add_path(
                layout, source, ROUTING_OUTPUT, true,
                width, nodeRadius);
    }

    layout.displayPathCount = layout.pathCount;
    for (int pathIndex = 0;
         pathIndex < layout.pathCount; ++pathIndex) {
        layout.displayPaths[pathIndex] = layout.paths[pathIndex];
    }
    if (routing_uses_merged_split_layout(algorithm))
        routing_make_merged_split_display(layout, height, nodeRadius);
    else if (routing_uses_staged_branch_layout(algorithm))
        routing_make_staged_branch_display(layout, nodeRadius);

    return layout;
}

} // namespace four_v2

#endif
