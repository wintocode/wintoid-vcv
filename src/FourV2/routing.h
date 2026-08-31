#ifndef WINTOID_FOUR_V2_ROUTING_H
#define WINTOID_FOUR_V2_ROUTING_H

#include "model.h"

namespace four_v2 {

static const int ROUTING_MAX_ROWS = 4;
static const int ROUTING_MAX_PATHS = 16;
static const int ROUTING_MAX_POINTS = 6;
static const int ROUTING_OUTPUT = -1;
static const float ROUTING_DEFAULT_PORT_GAP = 0.55f;
static const float ROUTING_DEFAULT_ROUTE_GAP = 1.20f;

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
    int pointCount;
    RoutingPoint points[ROUTING_MAX_POINTS];
};

struct RoutingLayout {
    RoutingNode nodes[OPERATOR_COUNT];
    RoutingPath paths[ROUTING_MAX_PATHS];
    int pathCount;
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

inline float routing_centered_port_offset(
    int index, int count, float portGap)
{
    return ((float)index - ((float)count - 1.f) * 0.5f) * portGap;
}

inline float routing_source_port_offset(
    const Algorithm& algorithm, const int rows[OPERATOR_COUNT],
    int source, int destination, float portGap)
{
    int count = 0;
    int index = 0;
    for (int candidate = 0;
         candidate < OPERATOR_COUNT; ++candidate) {
        if (!algorithm.mod[source][candidate])
            continue;
        if (rows[candidate] < rows[destination]
            || (rows[candidate] == rows[destination]
                && candidate < destination)) {
            ++index;
        }
        ++count;
    }
    return routing_centered_port_offset(index, count, portGap);
}

inline float routing_destination_port_offset(
    const Algorithm& algorithm, const int rows[OPERATOR_COUNT],
    int source, int destination, float portGap)
{
    int count = 0;
    int index = 0;
    for (int candidate = 0;
         candidate < OPERATOR_COUNT; ++candidate) {
        if (!algorithm.mod[candidate][destination])
            continue;
        if (rows[candidate] < rows[source]
            || (rows[candidate] == rows[source]
                && candidate < source)) {
            ++index;
        }
        ++count;
    }
    return routing_centered_port_offset(index, count, portGap);
}

inline float routing_route_offset(
    float startY, float endY, int routeIndex, int routeCount,
    int downwardCount, int upwardCount, float routeGap)
{
    if (downwardCount > 0 && upwardCount > 0) {
        const float distance = ((float)routeIndex + 0.5f) * routeGap;
        return endY > startY ? distance : -distance;
    }
    return routing_centered_port_offset(routeIndex, routeCount, routeGap);
}

inline void routing_add_path(
    RoutingLayout& layout, const Algorithm& algorithm,
    const int rows[OPERATOR_COUNT], int source, int destination, bool carrier,
    float width, float nodeRadius, float verticalMargin,
    float portGap, float routeGap, int routeIndex, int routeCount,
    int downwardCount, int upwardCount, bool outerRoute)
{
    if (layout.pathCount >= ROUTING_MAX_PATHS)
        return;

    RoutingPath& path = layout.paths[layout.pathCount++];
    path.source = source;
    path.destination = destination;
    path.carrier = carrier;

    const RoutingNode& sourceNode = layout.nodes[source];
    const float startX = sourceNode.x + nodeRadius;
    const float startY = sourceNode.y + (carrier ? 0.f
        : routing_source_port_offset(
            algorithm, rows, source, destination, portGap));
    const float endX = carrier
        ? width - nodeRadius
        : layout.nodes[destination].x - nodeRadius;
    const float endY = carrier ? sourceNode.y
        : layout.nodes[destination].y + routing_destination_port_offset(
            algorithm, rows, source, destination, portGap);

    path.points[0] = {startX, startY};
    if (carrier || startY == endY) {
        path.points[1] = {endX, endY};
        path.pointCount = 2;
        return;
    }

    if (outerRoute) {
        const float outerY = verticalMargin - nodeRadius;
        const float outerX = endX - routeGap;
        path.points[1] = {startX, outerY};
        path.points[2] = {outerX, outerY};
        path.points[3] = {outerX, endY};
        path.points[4] = {endX, endY};
        path.pointCount = 5;
        return;
    }

    const float routeOffset = routing_route_offset(
        startY, endY, routeIndex, routeCount,
        downwardCount, upwardCount, routeGap);
    const float bendX = startX + (endX - startX) * 0.5f + routeOffset;
    path.points[1] = {bendX, startY};
    path.points[2] = {bendX, endY};
    path.points[3] = {endX, endY};
    path.pointCount = 4;
}

inline RoutingLayout make_routing_layout(
    const Algorithm& algorithm,
    float width, float height, float nodeRadius,
    float horizontalMargin, float verticalMargin,
    float portGap = ROUTING_DEFAULT_PORT_GAP,
    float routeGap = ROUTING_DEFAULT_ROUTE_GAP)
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

    // Keep a branch source in the stage immediately before its destination.
    // This gives the branch its own readable slice instead of placing its
    // first bend on top of the main-chain node at that stage.
    for (int candidate = 0; candidate < OPERATOR_COUNT; ++candidate) {
        if (rows[candidate] >= 0)
            continue;

        bool hasIncoming = false;
        bool hasOutgoing = false;
        int nearestDestinationColumn = OPERATOR_COUNT;
        for (int source = 0; source < OPERATOR_COUNT; ++source) {
            if (algorithm.mod[source][candidate])
                hasIncoming = true;
        }
        for (int destination = 0;
             destination < OPERATOR_COUNT; ++destination) {
            if (!algorithm.mod[candidate][destination])
                continue;
            hasOutgoing = true;
            if (columns[destination] < nearestDestinationColumn)
                nearestDestinationColumn = columns[destination];
        }
        if (!hasIncoming && hasOutgoing
            && columns[candidate] < nearestDestinationColumn - 1) {
            columns[candidate] = nearestDestinationColumn - 1;
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

    int bentPathCount = 0;
    int downwardPathCount = 0;
    int upwardPathCount = 0;
    for (int source = 0; source < OPERATOR_COUNT; ++source) {
        for (int destination = 0;
             destination < OPERATOR_COUNT; ++destination) {
            if (!algorithm.mod[source][destination])
                continue;
            const float startY = layout.nodes[source].y
                + routing_source_port_offset(
                    algorithm, rows, source, destination, portGap);
            const float endY = layout.nodes[destination].y
                + routing_destination_port_offset(
                    algorithm, rows, source, destination, portGap);
            if (startY != endY) {
                ++bentPathCount;
                if (endY > startY)
                    ++downwardPathCount;
                else
                    ++upwardPathCount;
            }
        }
    }

    int downwardPathIndex = 0;
    int upwardPathIndex = 0;
    for (int source = 0; source < OPERATOR_COUNT; ++source) {
        for (int destination = 0;
             destination < OPERATOR_COUNT; ++destination) {
            if (algorithm.mod[source][destination]) {
                const float startY = layout.nodes[source].y
                    + routing_source_port_offset(
                        algorithm, rows, source, destination, portGap);
                const float endY = layout.nodes[destination].y
                    + routing_destination_port_offset(
                        algorithm, rows, source, destination, portGap);
                const bool needsBend = startY != endY;
                const bool travelsDownward = endY > startY;
                const int routeIndex = travelsDownward
                    ? downwardPathIndex : upwardPathIndex;
                const int routeCount = travelsDownward
                    ? downwardPathCount : upwardPathCount;
                const bool outerRoute = downwardPathCount > 0
                    && upwardPathCount > 0 && travelsDownward;
                routing_add_path(
                    layout, algorithm, rows, source, destination, false,
                    width, nodeRadius, verticalMargin,
                    portGap, routeGap,
                    needsBend ? routeIndex : -1, routeCount,
                    downwardPathCount, upwardPathCount, outerRoute);
                if (needsBend) {
                    if (travelsDownward)
                        ++downwardPathIndex;
                    else
                        ++upwardPathIndex;
                }
            }
        }
        if (algorithm.carrier[source]) {
            routing_add_path(
                layout, algorithm, rows, source, ROUTING_OUTPUT, true,
                width, nodeRadius, verticalMargin,
                portGap, routeGap, -1, bentPathCount,
                downwardPathCount, upwardPathCount, false);
        }
    }

    return layout;
}

} // namespace four_v2

#endif
