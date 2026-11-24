#pragma once

#include "Geometry/GeoLine.h"

namespace cad {

class GeoAlgorithms {
public:
    static double Distance(const GeoPoint& first, const GeoPoint& second);
    static GeoPoint MidPoint(const GeoLine& line);
};

} // namespace cad
