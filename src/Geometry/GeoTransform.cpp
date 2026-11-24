#include "Geometry/GeoTransform.h"

#include <cmath>

namespace cad {

GeoPoint GeoTransform::MirrorPoint(const GeoPoint& point, const GeoPoint& axisStart, const GeoPoint& axisEnd)
{
    const double vx = axisEnd.X() - axisStart.X();
    const double vy = axisEnd.Y() - axisStart.Y();
    const double len2 = vx * vx + vy * vy;
    if (len2 <= 1.0e-12) {
        return point;
    }

    const double wx = point.X() - axisStart.X();
    const double wy = point.Y() - axisStart.Y();
    const double t = (wx * vx + wy * vy) / len2;
    const double px = axisStart.X() + t * vx;
    const double py = axisStart.Y() + t * vy;
    return GeoPoint(2.0 * px - point.X(), 2.0 * py - point.Y(), point.Z());
}

double GeoTransform::MirrorAngle(double angleRadians, const GeoPoint& axisStart, const GeoPoint& axisEnd)
{
    const double axisAngle = std::atan2(axisEnd.Y() - axisStart.Y(), axisEnd.X() - axisStart.X());
    return 2.0 * axisAngle - angleRadians;
}

GeoPoint GeoTransform::RotatePoint(const GeoPoint& point, const GeoPoint& center, double angleRadians)
{
    const double dx = point.X() - center.X();
    const double dy = point.Y() - center.Y();
    const double c = std::cos(angleRadians);
    const double s = std::sin(angleRadians);
    return GeoPoint(center.X() + dx * c - dy * s, center.Y() + dx * s + dy * c, point.Z());
}

GeoPoint GeoTransform::ScalePoint(const GeoPoint& point, const GeoPoint& center, double factor)
{
    return GeoPoint(center.X() + (point.X() - center.X()) * factor,
        center.Y() + (point.Y() - center.Y()) * factor,
        point.Z());
}

} // namespace cad
