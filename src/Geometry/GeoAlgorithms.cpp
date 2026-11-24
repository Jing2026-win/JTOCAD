#include "Geometry/GeoAlgorithms.h"

#include <cmath>

namespace cad {

double GeoAlgorithms::Distance(const GeoPoint& first, const GeoPoint& second)
{
    const double dx = first.X() - second.X();
    const double dy = first.Y() - second.Y();
    const double dz = first.Z() - second.Z();
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

GeoPoint GeoAlgorithms::MidPoint(const GeoLine& line)
{
    return GeoPoint((line.Start().X() + line.End().X()) * 0.5,
                    (line.Start().Y() + line.End().Y()) * 0.5,
                    (line.Start().Z() + line.End().Z()) * 0.5);
}

} // namespace cad
