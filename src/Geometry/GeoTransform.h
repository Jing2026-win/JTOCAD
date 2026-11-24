#pragma once

#include "Geometry/GeoPoint.h"

namespace cad {

class GeoTransform {
public:
    static GeoPoint MirrorPoint(const GeoPoint& point, const GeoPoint& axisStart, const GeoPoint& axisEnd);
    static double MirrorAngle(double angleRadians, const GeoPoint& axisStart, const GeoPoint& axisEnd);
    static GeoPoint RotatePoint(const GeoPoint& point, const GeoPoint& center, double angleRadians);
    static GeoPoint ScalePoint(const GeoPoint& point, const GeoPoint& center, double factor);
};

} // namespace cad
