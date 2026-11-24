#pragma once

#include "Geometry/GeoArc.h"
#include "Geometry/GeoCircle.h"
#include "Geometry/GeoLine.h"
#include "Geometry/GeoPoint.h"
#include "Geometry/GeoShape.h"
#include "Geometry/GeoSpline.h"
#include <optional>
#include <vector>

namespace cad {

class DrawAlgorithms {
public:
    static std::optional<GeoCircle> CircleFromThreePoints(const GeoPoint& p1, const GeoPoint& p2, const GeoPoint& p3);
    static std::optional<GeoArc> ArcFromThreePoints(const GeoPoint& p1, const GeoPoint& p2, const GeoPoint& p3);
    static std::optional<GeoCircle> CircleFromTwoPoints(const GeoPoint& p1, const GeoPoint& p2);
    static std::optional<GeoCircle> CircleFromCenterRadius(const GeoPoint& center, const GeoPoint& radiusPoint);
    static std::optional<GeoCircle> CircleFromCenterDiameter(const GeoPoint& center, const GeoPoint& diameterPoint);
    static std::optional<GeoCircle> CircleTanTanRadius(
        const GeoLine& line1,
        const GeoLine& line2,
        double radius,
        const GeoPoint& pickNear);

    // AutoCAD TTR：与两对象相切、给定半径。拾取点用于在多解中选择切点最近的圆。
    struct TangentSupport {
        enum class Kind { Line, Circle, Arc } kind = Kind::Line;
        GeoLine line;
        GeoCircle circle;
        GeoArc arc;
        GeoPoint pick;
    };
    static std::optional<GeoCircle> CircleTanTanRadius(
        const TangentSupport& first,
        const TangentSupport& second,
        double radius);
    static std::optional<GeoCircle> CircleTanTanTan(
        const GeoLine& line1,
        const GeoLine& line2,
        const GeoLine& line3,
        const GeoPoint& pickNear);
    static std::optional<GeoCircle> CircleTanTanTan(
        const TangentSupport& first,
        const TangentSupport& second,
        const TangentSupport& third);

    static std::optional<GeoArc> ArcFromCenterStartEnd(
        const GeoPoint& center,
        const GeoPoint& start,
        const GeoPoint& end);
    static std::optional<GeoArc> ArcFromStartCenterEnd(
        const GeoPoint& start,
        const GeoPoint& center,
        const GeoPoint& end);
    // largeArc：同侧优弧（长扫掠）；clockwise：圆心在弦的哪一侧（光标侧）。
    static std::optional<GeoArc> ArcFromStartEndRadius(
        const GeoPoint& start,
        const GeoPoint& end,
        double radius,
        bool largeArc,
        bool clockwise);

    static std::optional<GeoArc> ArcFromStartCenterAngle(
        const GeoPoint& start,
        const GeoPoint& center,
        double angleDegrees);
    // AutoCAD Start-Center-Length / Center-Start-Length：
    // - chordLength：弦长（可负；负值先视为优弧）
    // - sidePick：保留调用兼容；第三点只提供弦长幅值，不切换圆弧侧
    // - ctrlFlip：Ctrl，在劣/优之间切换（同一端点走长弧 / 顺时针优弧）
    static std::optional<GeoArc> ArcFromStartCenterChord(
        const GeoPoint& start,
        const GeoPoint& center,
        double chordLength,
        const GeoPoint& sidePick,
        bool ctrlFlip = false);
    static std::optional<GeoArc> ArcFromStartEndAngle(
        const GeoPoint& start,
        const GeoPoint& end,
        double angleDegrees,
        const GeoPoint& pickNear);
    static std::optional<GeoArc> ArcFromStartEndDirection(
        const GeoPoint& start,
        const GeoPoint& end,
        const GeoPoint& directionPoint);
    static std::optional<GeoArc> ArcFromTangentStartEnd(
        const GeoPoint& start,
        const GeoPoint& tangentUnit,
        const GeoPoint& end);

    // AutoCAD LWPOLYLINE bulge: tan(sweep/4). Positive = CCW from start to end.
    static double BulgeFromArc(const GeoArc& arc);
    static std::optional<GeoArc> ArcFromBulge(
        const GeoPoint& start,
        const GeoPoint& end,
        double bulge);
    static GeoPoint ArcEndTangentUnit(const GeoArc& arc);
    static GeoPoint PolylineEndTangentUnit(
        const std::vector<GeoPoint>& points,
        const std::vector<double>& bulges,
        const GeoPoint* fallbackTangent = nullptr);
    static void AppendPolylineGeometry(
        GeoShape& shape,
        const std::vector<GeoPoint>& points,
        const std::vector<double>& bulges,
        bool closed);

    static GeoPoint ArcPointAt(const GeoPoint& center, double radius, double angleRadians);

    // 将带符号角度换算到 [0, 360)。负角按加 360° 的整数倍化为正角。
    static double ToPositiveAngleDegrees(double angleDegrees);

    static std::vector<GeoPoint> RegularPolygonPoints(
        const GeoPoint& center,
        double radius,
        int sides,
        double rotationRadians = 0.0);
    static void RegularPolygonFromCenterPick(
        const GeoPoint& center,
        const GeoPoint& pick,
        int sides,
        bool circumscribed,
        double& vertexRadius,
        double& rotationRadians);
    static bool RegularPolygonFromEdge(
        const GeoPoint& edgeStart,
        const GeoPoint& edgeEnd,
        int sides,
        GeoPoint& center,
        double& vertexRadius,
        double& rotationRadians);
    static std::vector<GeoPoint> RectanglePoints(const GeoPoint& corner1, const GeoPoint& corner2);
    static std::vector<GeoPoint> RectangleFromCenter(const GeoPoint& center, const GeoPoint& corner);

    static GeoShapeEllipse EllipseFromCenterAxis(
        const GeoPoint& center,
        const GeoPoint& majorAxisEnd,
        const GeoPoint& minorAxisPoint);
    static GeoShapeEllipse EllipseFromAxisEndpoints(
        const GeoPoint& majorStart,
        const GeoPoint& majorEnd,
        const GeoPoint& minorPoint);

    static std::vector<GeoPoint> DiscretizeParabola(
        const GeoPoint& vertex,
        const GeoPoint& focus,
        int segments = 64,
        double extent = 100.0);
    static std::vector<GeoPoint> DiscretizeHyperbola(
        const GeoPoint& center,
        const GeoPoint& vertex,
        double semiMinor,
        int segments = 64,
        double extent = 100.0);

    static std::vector<GeoPoint> EvaluateBSpline(
        const std::vector<GeoPoint>& controlPoints,
        int degree = 3,
        int samplesPerSpan = 16,
        const std::vector<double>& knots = {});
    static std::vector<GeoPoint> EvaluateFitPointSpline(
        const std::vector<GeoPoint>& fitPoints,
        int samplesPerSegment = 64);
    // Adaptive flattening: keep max chord height ≤ chordToleranceModel (AutoCAD VIEWRES).
    static std::vector<GeoPoint> FlattenBSpline(
        const std::vector<GeoPoint>& controlPoints,
        int degree,
        double chordToleranceModel,
        const std::vector<double>& knots = {});
    static std::vector<GeoPoint> FlattenFitPointSpline(
        const std::vector<GeoPoint>& fitPoints,
        double chordToleranceModel);
    static std::vector<GeoPoint> SplineDisplayPoints(
        const std::vector<GeoPoint>& points,
        SplineMode mode,
        int degree,
        double chordToleranceModel,
        const std::vector<double>& knots = {});
    static std::vector<GeoPoint> SplinePreviewPoints(
        const std::vector<GeoPoint>& points,
        SplineMode mode,
        int degree = 3,
        const std::vector<double>& knots = {});

    // Piecewise cubic Bézier matching the spline (fit cubics / degree-3 B-spline spans).
    // Qt/OpenGL rasterizes cubicTo much more smoothly than a polyline of chords.
    struct CubicBezierSegment {
        GeoPoint p0;
        GeoPoint p1;
        GeoPoint p2;
        GeoPoint p3;
    };
    static std::vector<CubicBezierSegment> SplineCubicBeziers(
        const std::vector<GeoPoint>& points,
        SplineMode mode,
        int degree = 3,
        const std::vector<double>& knots = {},
        const GeoPoint* startTangent = nullptr,
        const GeoPoint* endTangent = nullptr);

    // Fit points → interpolating cubic B-spline control vertices (curve through fits).
    static std::vector<GeoPoint> InterpolateBSplineControls(
        const std::vector<GeoPoint>& fitPoints,
        int degree = 3,
        std::vector<double>* knotsOut = nullptr);
    // Evaluate a B-spline at Greville / stored knots (Fit-grip samples for a CV spline).
    static std::vector<GeoPoint> SampleBSpline(
        const std::vector<GeoPoint>& controlPoints,
        int degree = 3,
        const std::vector<double>& knots = {});
};

} // namespace cad
