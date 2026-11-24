#pragma once

#include "Geometry/GeoPoint.h"
#include <utility>

namespace cad {

class GeoLine {
public:
    GeoLine() = default;
    GeoLine(GeoPoint start, GeoPoint end)
        : start_(start)
        , end_(end)
    {
    }

    const GeoPoint& Start() const { return start_; }
    const GeoPoint& End() const { return end_; }
    void SetStart(GeoPoint start) { start_ = std::move(start); }
    void SetEnd(GeoPoint end) { end_ = std::move(end); }

    void MoveBy(double dx, double dy)
    {
        start_ = start_.Translated(dx, dy);
        end_ = end_.Translated(dx, dy);
    }

private:
    GeoPoint start_;
    GeoPoint end_;
};

} // namespace cad
