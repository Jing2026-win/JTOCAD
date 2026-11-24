#pragma once

#include "Geometry/GeoPoint.h"

namespace cad {

enum class GripKind {
    Vertex,
    MidPoint,
    Center,
    Radius,
    Dynamic, // dynamic-block parameter grip
    ControlVertex, // AutoCAD spline CV: circular grip
    MethodToggle // AutoCAD spline Fit / CV switch triangle
};

struct GripInfo {
    int index = 0;
    GripKind kind = GripKind::Vertex;
    GeoPoint position;
};

} // namespace cad
