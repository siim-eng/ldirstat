#pragma once

#include <algorithm>
#include <limits>

// Keep layout policy in each treemap implementation.

namespace ldirstat {

struct TreeMapRect {
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;

    float right() const { return x + width; }
    float bottom() const { return y + height; }
    float area() const { return width * height; }
    bool empty() const { return width <= 0.0f || height <= 0.0f; }

    bool contains(float px, float py) const { return px >= x && py >= y && px < right() && py < bottom(); }
};

// Worst aspect ratio of a squarify row: `rowArea` is the total area of the
// row, `minArea` / `maxArea` its smallest / largest item, and `side` the
// length of the edge the row is laid along. Lower is better (1 == squares).
inline double squarifyWorstAspect(double rowArea, double minArea, double maxArea, double side) {
    if (rowArea <= 0.0 || minArea <= 0.0 || side <= 0.0) return std::numeric_limits<double>::infinity();

    const double sideSq = side * side;
    const double rowAreaSq = rowArea * rowArea;
    return std::max((sideSq * maxArea) / rowAreaSq, rowAreaSq / (sideSq * minArea));
}

} // namespace ldirstat
