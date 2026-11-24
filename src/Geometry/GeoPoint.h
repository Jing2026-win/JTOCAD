#pragma once

#include <cmath>

namespace cad {

class GeoPoint {
public:
    GeoPoint() = default;
    GeoPoint(double x, double y, double z = 0.0)
        : x_(x)
        , y_(y)
        , z_(z)
    {
    }

    double X() const { return x_; }
    double Y() const { return y_; }
    double Z() const { return z_; }

    void Set(double x, double y, double z = 0.0)
    {
        x_ = x;
        y_ = y;
        z_ = z;
    }

    GeoPoint Translated(double dx, double dy, double dz = 0.0) const
    {
        return GeoPoint(x_ + dx, y_ + dy, z_ + dz);
    }

private:
    double x_ = 0.0;
    double y_ = 0.0;
    double z_ = 0.0;
};

inline double Distance2D(const GeoPoint& first, const GeoPoint& second)
{
    const double dx = first.X() - second.X();
    const double dy = first.Y() - second.Y();
    return std::sqrt(dx * dx + dy * dy);
}

} // namespace cad
