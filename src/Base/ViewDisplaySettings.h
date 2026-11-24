#pragma once

#include <algorithm>
#include <cmath>

namespace cad {

// AutoCAD-compatible VIEWRES (circle zoom percent): controls how finely
// arcs/circles/curves are tessellated for *screen display* only.
// Geometry, object snap, and plot output stay mathematically exact.
struct ViewDisplaySettings {
    static constexpr int kDefaultViewRes = 1000;
    static constexpr int kMinViewRes = 1;
    static constexpr int kMaxViewRes = 20000;

    int viewRes = kDefaultViewRes;

    static int ClampViewRes(int value)
    {
        return std::clamp(value, kMinViewRes, kMaxViewRes);
    }

    // Full-circle display segment count from VIEWRES + on-screen size.
    // viewRes=100  → coarse polygons; 1000 → smooth; 10000+ → very fine when zoomed.
    static int CircleDisplaySegments(double radiusModel, double pixelsPerModelUnit, int viewRes)
    {
        viewRes = ClampViewRes(viewRes);
        const double ppm = std::max(std::abs(pixelsPerModelUnit), 1.0e-12);
        const double screenRadiusPx = std::max(1.0, std::abs(radiusModel) * ppm);
        const double circumferencePx = 2.0 * 3.14159265358979323846 * screenRadiusPx;
        // At VIEWRES=1000 ≈ 1 screen pixel per chord; scales inversely with viewRes.
        // Keep enough segments when zoomed so arcs never look like polygons.
        const double pxPerSegment = 1000.0 / static_cast<double>(viewRes);
        const int segments = static_cast<int>(std::ceil(circumferencePx / std::max(pxPerSegment, 0.35)));
        return std::clamp(segments, 16, 8192);
    }

    static int ArcDisplaySegments(
        double radiusModel,
        double sweepRadians,
        double pixelsPerModelUnit,
        int viewRes)
    {
        const int full = CircleDisplaySegments(radiusModel, pixelsPerModelUnit, viewRes);
        const double ratio = std::abs(sweepRadians) / (2.0 * 3.14159265358979323846);
        return std::clamp(static_cast<int>(std::ceil(full * std::max(ratio, 0.02))), 8, 8192);
    }

    static int EllipseDisplaySegments(
        double majorRadiusModel,
        double pixelsPerModelUnit,
        int viewRes)
    {
        return CircleDisplaySegments(majorRadiusModel, pixelsPerModelUnit, viewRes);
    }

    // AutoCAD-like display chord height: VIEWRES=1000 keeps error ≈ 0.75 screen pixel.
    static double CurveChordToleranceModel(double pixelsPerModelUnit, int viewRes)
    {
        viewRes = ClampViewRes(viewRes);
        const double ppm = std::max(std::abs(pixelsPerModelUnit), 1.0e-12);
        const double pixelTol = 0.75 * (1000.0 / static_cast<double>(viewRes));
        return std::max(pixelTol / ppm, 1.0e-9);
    }
};

struct CurveDisplayOptions {
    int viewRes = ViewDisplaySettings::kDefaultViewRes;
    double pixelsPerModelUnit = 1.0;
    // When true (print / hit-test helpers), use Qt native arcs/ellipses.
    bool useTrueCurves = false;
};

} // namespace cad
