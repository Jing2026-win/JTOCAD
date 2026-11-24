#pragma once

#include "Geometry/GeoPoint.h"
#include <utility>

namespace cad {

class GeoCircle {
public:
    GeoCircle() = default;
    GeoCircle(GeoPoint center, double radius)
        : center_(center)
        , radius_(radius)
    {
    }

    const GeoPoint& Center() const { return center_; }
    double Radius() const { return radius_; }
    void SetCenter(GeoPoint center) { center_ = std::move(center); }
    void SetRadius(double radius) { radius_ = radius; }

    void MoveBy(double dx, double dy)
    {
        center_ = center_.Translated(dx, dy);
    }

private:
    GeoPoint center_;
    double radius_ = 0.0;
};

} // namespace cad
