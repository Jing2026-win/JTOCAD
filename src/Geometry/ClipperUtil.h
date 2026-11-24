#pragma once

#include "Base/Tolerance.h"
#include "Geometry/GeoLine.h"
#include "Geometry/GeoPoint.h"
#include "Geometry/GeoRegion.h"
#include <vector>

namespace cad {

enum class BooleanOp {
    Union,
    Intersection,
    Difference,
    Xor
};

class ClipperUtil {
public:
    static constexpr double Scale = Tolerance::ClipperScale;

    static std::vector<GeoRegion> BooleanOperation(BooleanOp op, const std::vector<GeoRegion>& subjects, const std::vector<GeoRegion>& clips);
    static std::vector<GeoRegion> OffsetRegion(const GeoRegion& region, double delta);
    static std::vector<GeoRegion> ClipRegion(const GeoRegion& region, const GeoRegion& clipRect);
    static std::vector<GeoLine> GenerateHatch(const GeoRegion& region, double spacing, double angleRadians);

    static GeoRegion CircleToRegion(const GeoPoint& center, double radius, int segments = 64);
    static GeoRegion PolylineToRegion(const std::vector<GeoPoint>& points);
};

} // namespace cad
