#pragma once

#include "Base/ViewDisplaySettings.h"
#include "Geometry/GeoShape.h"
#include <QPen>

class QPainter;
class QPainterPath;

namespace cad {

class GeoShapePainter {
public:
    // Hit-testing / geometry path: prefers true curves unless display forces tessellation.
    static QPainterPath BuildPath(
        const GeoShape& shape,
        const GeoBounds* clipBounds = nullptr,
        const CurveDisplayOptions* display = nullptr);

    // Screen paint: tessellates arcs/circles/ellipses/splines by VIEWRES (vector fitting).
    // Pass useTrueCurves=true for plot-quality output.
    static void Paint(
        QPainter* painter,
        const GeoShape& shape,
        const QPen& pen,
        const GeoBounds* clipBounds = nullptr,
        const CurveDisplayOptions* display = nullptr);
};

} // namespace cad
