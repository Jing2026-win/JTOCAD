#pragma once

#include <cmath>

namespace cad {

class Tolerance {
public:
    // One source of truth for numeric comparisons across the model kernel,
    // Clipper integer conversion and Qt view hit testing.
    static constexpr double Model = 1.0e-6;
    static constexpr double Linear = Model;
    static constexpr double Angular = 1.0e-9;
    static constexpr double Angle = Angular;

    // OCC-facing aliases. There is no direct OCC call site in the current 2D
    // module yet; keep the names explicit so future OCC code does not invent
    // a second tolerance policy.
    static constexpr double OccLinear = Linear;
    static constexpr double OccAngular = Angular;

    // Clipper uses int64 coordinates. Scale is chosen as the reciprocal of the
    // model tolerance so round-trip precision matches model comparisons.
    static constexpr double ClipperScale = 1.0 / Model;
    static constexpr double ClipperArcTolerance = Model;

    // Qt view tolerances are expressed in pixels first, then converted to
    // model units through the current QGraphicsView transform.
    static constexpr double PickTolerancePixels = 12.0;
    static constexpr double GripTolerancePixels = 12.0;
    static constexpr double OsnapAperturePixels = 24.0;
    static constexpr double MinZoom = 1.0e-3;
    static constexpr double MaxZoom = 1.0e3;
    static constexpr double MinGridPixelStep = 6.0;

    static bool IsEqual(double lhs, double rhs, double tolerance = Model)
    {
        return std::abs(lhs - rhs) <= tolerance;
    }

    static bool IsZero(double value, double tolerance = Model)
    {
        return std::abs(value) <= tolerance;
    }

    static double PixelsToModel(double pixels, double viewScale)
    {
        return pixels / (std::abs(viewScale) <= Model ? 1.0 : std::abs(viewScale));
    }
};

} // namespace cad
