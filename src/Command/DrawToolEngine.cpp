#include "Command/DrawToolEngine.h"

#include "Base/IdGenerator.h"
#include "Base/Tolerance.h"
#include "Command/EditKernel.h"
#include "DataModel/ArcEntity.h"
#include "DataModel/CadDocument.h"
#include "DataModel/CircleEntity.h"
#include "DataModel/ConicEntity.h"
#include "DataModel/Entity.h"
#include "DataModel/LineEntity.h"
#include "DataModel/PointEntity.h"
#include "DataModel/PolylineEntity.h"
#include "DataModel/RayEntity.h"
#include "DataModel/RectangleEntity.h"
#include "DataModel/RegionEntity.h"
#include "Geometry/ClipperUtil.h"
#include "Geometry/GeoPolyline.h"
#include <algorithm>
#include <cmath>
#include <optional>

namespace cad {

namespace {
DrawToolResult NeedPoints(int count, const std::string& prompt)
{
    DrawToolResult result;
    result.needsMorePoints = true;
    result.requiredPoints = count;
    result.prompt = prompt;
    return result;
}

DrawToolResult Complete(std::unique_ptr<Entity> entity)
{
    DrawToolResult result;
    result.needsMorePoints = false;
    result.entity = std::move(entity);
    return result;
}

DrawToolResult CompleteAndRepeatBase(std::unique_ptr<Entity> entity, const std::string& nextPrompt)
{
    DrawToolResult result;
    result.needsMorePoints = true;
    result.requiredPoints = 2;
    result.entity = std::move(entity);
    result.keepBasePoint = true;
    result.nextPrompt = nextPrompt;
    result.prompt = nextPrompt;
    return result;
}

DrawToolResult CompleteAndContinueFromLast(std::unique_ptr<Entity> entity, const std::string& nextPrompt)
{
    DrawToolResult result;
    result.needsMorePoints = true;
    result.requiredPoints = 2;
    result.entity = std::move(entity);
    result.continueFromLastPoint = true;
    result.nextPrompt = nextPrompt;
    result.prompt = nextPrompt;
    return result;
}

std::string PolylineNextPrompt(std::size_t vertexCount, bool arcMode)
{
    const bool canClose = vertexCount >= 2;
    if (arcMode) {
        return canClose
            ? "指定圆弧的端点或 [角度(A)/圆心(CE)/闭合(CL)/方向(D)/半宽(H)/直线(L)/半径(R)/第二个点(S)/放弃(U)/宽度(W)]"
            : "指定圆弧的端点或 [角度(A)/圆心(CE)/方向(D)/半宽(H)/直线(L)/半径(R)/第二个点(S)/放弃(U)/宽度(W)]";
    }
    if (vertexCount == 0) {
        return "指定起点";
    }
    return canClose
        ? "指定下一个点或 [圆弧(A)/闭合(C)/半宽(H)/长度(L)/放弃(U)/宽度(W)]"
        : "指定下一个点或 [圆弧(A)/半宽(H)/长度(L)/放弃(U)/宽度(W)]";
}

void AppendPlusMarker(GeoShape& shape, const GeoPoint& point, double halfSize)
{
    const double half = std::max(halfSize, Tolerance::Model * 10.0);
    shape.AddSegment(GeoPoint(point.X() - half, point.Y()), GeoPoint(point.X() + half, point.Y()));
    shape.AddSegment(GeoPoint(point.X(), point.Y() - half), GeoPoint(point.X(), point.Y() + half));
}

std::vector<const Entity*> CollectEntities(const CadDocument* document)
{
    std::vector<const Entity*> entities;
    if (!document) {
        return entities;
    }
    for (const auto& entity : document->Entities()) {
        entities.push_back(entity.get());
    }
    return entities;
}

GeoPoint Normalize(const GeoPoint& vector)
{
    const double len = std::hypot(vector.X(), vector.Y());
    if (len <= Tolerance::Model) {
        return GeoPoint(1.0, 0.0);
    }
    return GeoPoint(vector.X() / len, vector.Y() / len);
}

std::optional<GeoLine> NearestLineEntity(const GeoPoint& point, const DrawContext& context)
{
    if (!context.entities) {
        return std::nullopt;
    }
    double bestDistance = context.pickTolerance;
    std::optional<GeoLine> bestLine;
    for (const Entity* entity : *context.entities) {
        if (!entity || entity->Type() != EntityType::Line) {
            continue;
        }
        const GeoLine line = static_cast<const LineEntity*>(entity)->Line();
        const double vx = line.End().X() - line.Start().X();
        const double vy = line.End().Y() - line.Start().Y();
        const double wx = point.X() - line.Start().X();
        const double wy = point.Y() - line.Start().Y();
        const double len2 = vx * vx + vy * vy;
        const double t = len2 <= Tolerance::Model ? 0.0 : std::clamp((wx * vx + wy * vy) / len2, 0.0, 1.0);
        const GeoPoint closest(line.Start().X() + t * vx, line.Start().Y() + t * vy);
        const double distance = Distance2D(point, closest);
        if (distance < bestDistance) {
            bestDistance = distance;
            bestLine = line;
        }
    }
    return bestLine;
}

double DistanceToSegment(const GeoLine& line, const GeoPoint& point)
{
    const double vx = line.End().X() - line.Start().X();
    const double vy = line.End().Y() - line.Start().Y();
    const double wx = point.X() - line.Start().X();
    const double wy = point.Y() - line.Start().Y();
    const double len2 = vx * vx + vy * vy;
    const double t = len2 <= Tolerance::Model ? 0.0 : std::clamp((wx * vx + wy * vy) / len2, 0.0, 1.0);
    return Distance2D(point, GeoPoint(line.Start().X() + t * vx, line.Start().Y() + t * vy));
}

double DistanceToRay(const GeoRay& ray, const GeoPoint& point)
{
    const double vx = ray.ThroughPoint().X() - ray.Origin().X();
    const double vy = ray.ThroughPoint().Y() - ray.Origin().Y();
    const double wx = point.X() - ray.Origin().X();
    const double wy = point.Y() - ray.Origin().Y();
    const double len2 = vx * vx + vy * vy;
    const double t = len2 <= Tolerance::Model ? 0.0 : std::max(0.0, (wx * vx + wy * vy) / len2);
    return Distance2D(point, GeoPoint(ray.Origin().X() + t * vx, ray.Origin().Y() + t * vy));
}

double DistanceToXLine(const GeoXLine& xline, const GeoPoint& point)
{
    const double vx = xline.ThroughPoint().X() - xline.Anchor().X();
    const double vy = xline.ThroughPoint().Y() - xline.Anchor().Y();
    const double wx = point.X() - xline.Anchor().X();
    const double wy = point.Y() - xline.Anchor().Y();
    const double len2 = vx * vx + vy * vy;
    const double t = len2 <= Tolerance::Model ? 0.0 : (wx * vx + wy * vy) / len2;
    return Distance2D(point, GeoPoint(xline.Anchor().X() + t * vx, xline.Anchor().Y() + t * vy));
}

double DistanceToCircleCurve(const GeoCircle& circle, const GeoPoint& point)
{
    return std::abs(Distance2D(point, circle.Center()) - circle.Radius());
}

double DistanceToArcCurve(const GeoArc& arc, const GeoPoint& point)
{
    const GeoPoint center = arc.Center();
    const double radius = arc.Radius();
    if (radius <= Tolerance::Model) {
        return Distance2D(point, center);
    }
    const double ang = std::atan2(point.Y() - center.Y(), point.X() - center.X());
    double sweep = arc.EndAngleRadians() - arc.StartAngleRadians();
    while (sweep > 2.0 * 3.14159265358979323846) {
        sweep -= 2.0 * 3.14159265358979323846;
    }
    while (sweep < -2.0 * 3.14159265358979323846) {
        sweep += 2.0 * 3.14159265358979323846;
    }
    auto relative = [](double angle, double base) {
        double r = angle - base;
        while (r < 0.0) {
            r += 2.0 * 3.14159265358979323846;
        }
        while (r >= 2.0 * 3.14159265358979323846) {
            r -= 2.0 * 3.14159265358979323846;
        }
        return r;
    };
    bool onSpan = false;
    if (std::abs(sweep) <= 1.0e-9) {
        const double t = relative(ang, arc.StartAngleRadians());
        onSpan = t <= 1.0e-6 || t >= 2.0 * 3.14159265358979323846 - 1.0e-6;
    } else if (sweep >= 0.0) {
        onSpan = relative(ang, arc.StartAngleRadians()) <= sweep + 1.0e-6;
    } else {
        onSpan = relative(arc.StartAngleRadians(), ang) <= -sweep + 1.0e-6;
    }
    if (onSpan) {
        return DistanceToCircleCurve(GeoCircle(center, radius), point);
    }
    const GeoPoint start = DrawAlgorithms::ArcPointAt(center, radius, arc.StartAngleRadians());
    const GeoPoint end = DrawAlgorithms::ArcPointAt(center, radius, arc.EndAngleRadians());
    return std::min(Distance2D(point, start), Distance2D(point, end));
}

DrawAlgorithms::TangentSupport MakeLineSupport(const GeoLine& line, const GeoPoint& pick)
{
    DrawAlgorithms::TangentSupport support;
    support.kind = DrawAlgorithms::TangentSupport::Kind::Line;
    support.line = line;
    support.pick = pick;
    return support;
}

void ConsiderTangentSupport(
    double distance,
    DrawAlgorithms::TangentSupport support,
    double& bestDistance,
    std::optional<DrawAlgorithms::TangentSupport>& best)
{
    if (distance < bestDistance) {
        bestDistance = distance;
        best = std::move(support);
    }
}

void ConsiderPolylineSupports(
    const std::vector<GeoPoint>& points,
    bool closed,
    const GeoPoint& pick,
    double& bestDistance,
    std::optional<DrawAlgorithms::TangentSupport>& best,
    const std::vector<double>* bulges = nullptr)
{
    if (points.size() < 2) {
        return;
    }
    const std::size_t count = points.size();
    const std::size_t segments = closed ? count : count - 1;
    for (std::size_t i = 0; i < segments; ++i) {
        const GeoPoint& start = points[i];
        const GeoPoint& end = points[(i + 1) % count];
        const double bulge = (bulges && i < bulges->size()) ? (*bulges)[i] : 0.0;
        if (auto arc = DrawAlgorithms::ArcFromBulge(start, end, bulge)) {
            DrawAlgorithms::TangentSupport support;
            support.kind = DrawAlgorithms::TangentSupport::Kind::Arc;
            support.arc = *arc;
            support.pick = pick;
            ConsiderTangentSupport(DistanceToArcCurve(support.arc, pick), support, bestDistance, best);
            continue;
        }
        const GeoLine line(start, end);
        if (Distance2D(line.Start(), line.End()) <= Tolerance::Model) {
            continue;
        }
        ConsiderTangentSupport(DistanceToSegment(line, pick), MakeLineSupport(line, pick), bestDistance, best);
    }
}

std::optional<DrawAlgorithms::TangentSupport> NearestTangentSupport(
    const GeoPoint& point,
    const DrawContext& context)
{
    if (!context.entities) {
        return std::nullopt;
    }
    double bestDistance = context.pickTolerance;
    std::optional<DrawAlgorithms::TangentSupport> best;
    for (const Entity* entity : *context.entities) {
        if (!entity) {
            continue;
        }
        switch (entity->Type()) {
        case EntityType::Line: {
            const GeoLine line = static_cast<const LineEntity*>(entity)->Line();
            ConsiderTangentSupport(DistanceToSegment(line, point), MakeLineSupport(line, point), bestDistance, best);
            break;
        }
        case EntityType::Ray: {
            const GeoRay& ray = static_cast<const RayEntity*>(entity)->Ray();
            const GeoLine line(ray.Origin(), ray.ThroughPoint());
            ConsiderTangentSupport(DistanceToRay(ray, point), MakeLineSupport(line, point), bestDistance, best);
            break;
        }
        case EntityType::XLine: {
            const GeoXLine& xline = static_cast<const XLineEntity*>(entity)->XLine();
            const GeoLine line(xline.Anchor(), xline.ThroughPoint());
            ConsiderTangentSupport(DistanceToXLine(xline, point), MakeLineSupport(line, point), bestDistance, best);
            break;
        }
        case EntityType::Circle: {
            DrawAlgorithms::TangentSupport support;
            support.kind = DrawAlgorithms::TangentSupport::Kind::Circle;
            support.circle = static_cast<const CircleEntity*>(entity)->Circle();
            support.pick = point;
            ConsiderTangentSupport(DistanceToCircleCurve(support.circle, point), support, bestDistance, best);
            break;
        }
        case EntityType::Arc: {
            DrawAlgorithms::TangentSupport support;
            support.kind = DrawAlgorithms::TangentSupport::Kind::Arc;
            support.arc = static_cast<const ArcEntity*>(entity)->Arc();
            support.pick = point;
            ConsiderTangentSupport(DistanceToArcCurve(support.arc, point), support, bestDistance, best);
            break;
        }
        case EntityType::Polyline: {
            const auto& polyline = static_cast<const PolylineEntity*>(entity)->Polyline();
            ConsiderPolylineSupports(
                polyline.Points(),
                polyline.Closed(),
                point,
                bestDistance,
                best,
                &polyline.Bulges());
            break;
        }
        case EntityType::Rectangle: {
            const auto& rect = static_cast<const RectangleEntity*>(entity)->Rectangle();
            ConsiderPolylineSupports(
                DrawAlgorithms::RectanglePoints(rect.Corner1(), rect.Corner2()),
                true,
                point,
                bestDistance,
                best);
            break;
        }
        case EntityType::Polygon: {
            const auto& polygon = static_cast<const PolygonEntity*>(entity)->Polygon();
            ConsiderPolylineSupports(
                DrawAlgorithms::RegularPolygonPoints(
                    polygon.Center(),
                    polygon.Radius(),
                    polygon.Sides(),
                    polygon.RotationRadians()),
                true,
                point,
                bestDistance,
                best);
            break;
        }
        default:
            break;
        }
    }
    return best;
}

GeoPoint ArcStartPoint(const GeoArc& arc)
{
    return DrawAlgorithms::ArcPointAt(arc.Center(), arc.Radius(), arc.StartAngleRadians());
}

GeoPoint ArcEndPoint(const GeoArc& arc)
{
    return DrawAlgorithms::ArcPointAt(arc.Center(), arc.Radius(), arc.EndAngleRadians());
}

void AppendArcConstructionPreview(
    GeoShape& preview,
    const GeoArc& arc,
    bool showFullCircle = true,
    const GeoPoint* chordStart = nullptr,
    const GeoPoint* chordEnd = nullptr,
    const GeoPoint* tangentFrom = nullptr,
    const GeoPoint* tangentTo = nullptr)
{
    preview.AddArc(arc);
    const GeoPoint center = arc.Center();
    const GeoPoint start = ArcStartPoint(arc);
    const GeoPoint end = ArcEndPoint(arc);
    preview.AddSegment(center, start);
    preview.AddSegment(center, end);
    if (showFullCircle) {
        preview.AddCircle(GeoCircle(center, arc.Radius()));
    }
    if (chordStart && chordEnd) {
        preview.AddSegment(*chordStart, *chordEnd);
    }
    if (tangentFrom && tangentTo) {
        preview.AddSegment(*tangentFrom, *tangentTo);
    }
}

void AppendCenterGuides(
    GeoShape& preview,
    const GeoPoint& center,
    const GeoPoint& start,
    const GeoPoint& cursorOrEnd,
    bool showFullCircle)
{
    preview.AddSegment(center, start);
    preview.AddSegment(center, cursorOrEnd);
    if (showFullCircle) {
        const double radius = Distance2D(center, start);
        if (radius > Tolerance::Model) {
            preview.AddCircle(GeoCircle(center, radius));
        }
    }
}

// AutoCAD SCA: included angle from start radial to pick radial, CCW in (0, 360°].
double IncludedAngleDegreesFromPick(
    const GeoPoint& start,
    const GeoPoint& center,
    const GeoPoint& pick)
{
    constexpr double kPi = 3.14159265358979323846;
    constexpr double kTwoPi = 6.28318530717958647692;
    const double a0 = std::atan2(start.Y() - center.Y(), start.X() - center.X());
    const double a1 = std::atan2(pick.Y() - center.Y(), pick.X() - center.X());
    double sweep = a1 - a0;
    while (sweep <= 0.0) {
        sweep += kTwoPi;
    }
    while (sweep > kTwoPi) {
        sweep -= kTwoPi;
    }
    if (sweep >= kTwoPi - 1.0e-9) {
        return 0.0;
    }
    return sweep * 180.0 / kPi;
}

// AutoCAD「圆心/起点、角度」：解析包含角（度，带符号）。
// - 键入正值 = 逆时针；键入负值 = 顺时针；Ctrl 再翻转方向。
// - 鼠标拖动：默认量取起点径向→光标径向的逆时针包含角；Ctrl 时量取顺时针包含角。
double ResolveCenterStartIncludedAngleDegrees(
    const GeoPoint& start,
    const GeoPoint& center,
    const GeoPoint& pick,
    bool ctrlFlip,
    bool hasExplicit,
    double explicitDegrees)
{
    constexpr double kPi = 3.14159265358979323846;
    constexpr double kTwoPi = 6.28318530717958647692;

    if (hasExplicit) {
        double magnitude = DrawAlgorithms::ToPositiveAngleDegrees(std::abs(explicitDegrees));
        if (magnitude <= Tolerance::Model || magnitude >= 360.0 - Tolerance::Model) {
            return 0.0;
        }
        bool clockwise = explicitDegrees < 0.0;
        if (ctrlFlip) {
            clockwise = !clockwise;
        }
        return clockwise ? -magnitude : magnitude;
    }

    const double a0 = std::atan2(start.Y() - center.Y(), start.X() - center.X());
    const double a1 = std::atan2(pick.Y() - center.Y(), pick.X() - center.X());
    double delta = a1 - a0;

    if (ctrlFlip) {
        // 顺时针：归一化到 (-2π, 0]，返回负角度。
        while (delta > 0.0) {
            delta -= kTwoPi;
        }
        while (delta <= -kTwoPi) {
            delta += kTwoPi;
        }
        if (delta >= -1.0e-9 || delta <= -kTwoPi + 1.0e-9) {
            return 0.0;
        }
        return delta * 180.0 / kPi;
    }

    // 逆时针：归一化到 (0, 2π]。
    while (delta <= 0.0) {
        delta += kTwoPi;
    }
    while (delta > kTwoPi) {
        delta -= kTwoPi;
    }
    if (delta >= kTwoPi - 1.0e-9) {
        return 0.0;
    }
    return delta * 180.0 / kPi;
}

// Side-relative included angle in (-180, 180], matching DYN 0..180 display.
double SignedIncludedAngleDegreesShortest(
    const GeoPoint& start,
    const GeoPoint& center,
    const GeoPoint& pick)
{
    constexpr double kPi = 3.14159265358979323846;
    constexpr double kTwoPi = 6.28318530717958647692;
    const double a0 = std::atan2(start.Y() - center.Y(), start.X() - center.X());
    const double a1 = std::atan2(pick.Y() - center.Y(), pick.X() - center.X());
    double delta = a1 - a0;
    while (delta <= -kPi) {
        delta += kTwoPi;
    }
    while (delta > kPi) {
        delta -= kTwoPi;
    }
    return delta * 180.0 / kPi;
}

// AutoCAD SEA: included angle of the arc start→end that passes near pick (positive degrees).
double IncludedAngleDegreesFromStartEndPick(
    const GeoPoint& start,
    const GeoPoint& end,
    const GeoPoint& pick)
{
    constexpr double kPi = 3.14159265358979323846;
    if (auto arc = DrawAlgorithms::ArcFromThreePoints(start, pick, end)) {
        const double signedDeg =
            (arc->EndAngleRadians() - arc->StartAngleRadians()) * 180.0 / kPi;
        return DrawAlgorithms::ToPositiveAngleDegrees(signedDeg);
    }
    return 0.0;
}

// AutoCAD SEA：同一幅值 θ，用「起点→光标」极角所在半平面自动切换优/劣弧。
// - 光标在起点 +X 轴上方 (atan2≥0)：圆心角 = θ（劣弧，若 θ≤180）
// - 光标在起点 +X 轴下方 (atan2<0)：圆心角 = 360°-θ（优弧）——对应图1
// Ctrl 再翻转一次。半径始终由 θ 决定（sin 对称）。
double ResolveSeaSweepDegrees(
    const GeoPoint& start,
    const GeoPoint& /*end*/,
    const GeoPoint& sidePick,
    double magnitudeDegrees,
    bool ctrlFlip)
{
    double magnitude = DrawAlgorithms::ToPositiveAngleDegrees(std::abs(magnitudeDegrees));
    if (magnitude <= Tolerance::Model || magnitude >= 360.0 - Tolerance::Model) {
        return 0.0;
    }

    const double dx = sidePick.X() - start.X();
    const double dy = sidePick.Y() - start.Y();
    bool useComplement = false;
    if (std::hypot(dx, dy) > Tolerance::Model) {
        useComplement = std::atan2(dy, dx) < 0.0;
    }
    if (ctrlFlip) {
        useComplement = !useComplement;
    }

    double sweep = useComplement ? (360.0 - magnitude) : magnitude;
    if (sweep <= Tolerance::Model || sweep >= 360.0 - Tolerance::Model) {
        return 0.0;
    }
    return sweep;
}
} // namespace

DrawMethod DrawToolEngine::DefaultMethod(ToolMode tool)
{
    switch (tool) {
    case ToolMode::DrawPoint:
        return DrawMethod::PointSingle;
    case ToolMode::DrawLine:
        return DrawMethod::LineTwoPoints;
    case ToolMode::DrawRay:
        return DrawMethod::RayTwoPoints;
    case ToolMode::DrawXLine:
        return DrawMethod::XLineTwoPoints;
    case ToolMode::DrawRectangle:
        return DrawMethod::RectTwoCorners;
    case ToolMode::DrawPolygon:
        return DrawMethod::PolygonInscribed;
    case ToolMode::DrawCircle:
        return DrawMethod::CircleCenterRadius;
    case ToolMode::DrawArc:
        return DrawMethod::ArcThreePoints;
    case ToolMode::DrawEllipse:
        return DrawMethod::EllipseCenterAxis;
    case ToolMode::DrawParabola:
        return DrawMethod::ParabolaVertexFocus;
    case ToolMode::DrawHyperbola:
        return DrawMethod::HyperbolaCenterVertexMinor;
    case ToolMode::DrawSpline:
        return DrawMethod::SplineFitPoints;
    case ToolMode::DrawPolyline:
        return DrawMethod::PolylineContinuous;
    case ToolMode::DrawText:
        return DrawMethod::TextInsertionPoint;
    case ToolMode::DrawMText:
        return DrawMethod::MTextCornerWidth;
    case ToolMode::DrawSolidFill:
    case ToolMode::DrawHatchFill:
        return DrawMethod::FillFromSelection;
    default:
        return DrawMethod::LineTwoPoints;
    }
}

std::vector<DrawMethod> DrawToolEngine::AvailableMethods(ToolMode tool)
{
    switch (tool) {
    case ToolMode::DrawPoint:
        return {DrawMethod::PointSingle};
    case ToolMode::DrawLine:
        return {DrawMethod::LineTwoPoints, DrawMethod::LinePointLengthAngle};
    case ToolMode::DrawRay:
        return {DrawMethod::RayStartDirection, DrawMethod::RayTwoPoints};
    case ToolMode::DrawXLine:
        return {DrawMethod::XLineTwoPoints, DrawMethod::XLinePointAngle};
    case ToolMode::DrawRectangle:
        return {DrawMethod::RectTwoCorners, DrawMethod::RectCenterCorner};
    case ToolMode::DrawPolygon:
        return {DrawMethod::PolygonInscribed, DrawMethod::PolygonCircumscribed, DrawMethod::PolygonEdge};
    case ToolMode::DrawCircle:
        return {
            DrawMethod::CircleCenterRadius,
            DrawMethod::CircleCenterDiameter,
            DrawMethod::CircleTwoPoints,
            DrawMethod::CircleThreePoints,
            DrawMethod::CircleTanTanRadius,
            DrawMethod::CircleTanTanTan};
    case ToolMode::DrawArc:
        return {
            DrawMethod::ArcThreePoints,
            DrawMethod::ArcStartCenterEnd,
            DrawMethod::ArcStartCenterAngle,
            DrawMethod::ArcStartCenterChord,
            DrawMethod::ArcCenterStartEnd,
            DrawMethod::ArcCenterStartAngle,
            DrawMethod::ArcCenterStartChord,
            DrawMethod::ArcStartEndAngle,
            DrawMethod::ArcStartEndRadius,
            DrawMethod::ArcStartEndDirection,
            DrawMethod::ArcContinue};
    case ToolMode::DrawEllipse:
        return {DrawMethod::EllipseCenterAxis, DrawMethod::EllipseAxisEndpoints};
    case ToolMode::DrawParabola:
        return {DrawMethod::ParabolaVertexFocus, DrawMethod::ParabolaThreePoints};
    case ToolMode::DrawHyperbola:
        return {DrawMethod::HyperbolaCenterVertexMinor};
    case ToolMode::DrawSpline:
        return {DrawMethod::SplineFitPoints, DrawMethod::SplineControlPoints};
    case ToolMode::DrawPolyline:
        return {DrawMethod::PolylineContinuous};
    case ToolMode::DrawText:
        return {DrawMethod::TextInsertionPoint};
    case ToolMode::DrawMText:
        return {DrawMethod::MTextCornerWidth, DrawMethod::MTextFixedWidth};
    case ToolMode::DrawSolidFill:
    case ToolMode::DrawHatchFill:
        return {DrawMethod::FillFromSelection};
    default:
        return {};
    }
}

std::string DrawToolEngine::MethodLabel(DrawMethod method)
{
    switch (method) {
    case DrawMethod::PointSingle:
        return "单点";
    case DrawMethod::LineTwoPoints:
        return "连续线段";
    case DrawMethod::LinePointLengthAngle:
        return "点-长度-角度";
    case DrawMethod::RayStartDirection:
        return "起点-方向";
    case DrawMethod::RayTwoPoints:
        return "两点";
    case DrawMethod::XLineTwoPoints:
        return "两点";
    case DrawMethod::XLinePointAngle:
        return "点-角度";
    case DrawMethod::RectTwoCorners:
        return "两角点";
    case DrawMethod::RectCenterCorner:
        return "中心-角点";
    case DrawMethod::PolygonCenterRadius:
    case DrawMethod::PolygonInscribed:
        return "内接于圆";
    case DrawMethod::PolygonCircumscribed:
        return "外切于圆";
    case DrawMethod::PolygonEdge:
        return "边";
    case DrawMethod::CircleCenterRadius:
        return "圆心-半径";
    case DrawMethod::CircleCenterDiameter:
        return "圆心-直径";
    case DrawMethod::CircleTwoPoints:
        return "两点(直径)";
    case DrawMethod::CircleThreePoints:
        return "三点";
    case DrawMethod::CircleTanTanRadius:
        return "相切-相切-半径";
    case DrawMethod::CircleTanTanTan:
        return "相切-相切-相切";
    case DrawMethod::ArcThreePoints:
        return "三点";
    case DrawMethod::ArcCenterStartEnd:
        return "圆心-起点-端点";
    case DrawMethod::ArcCenterStartAngle:
        return "圆心-起点-角度";
    case DrawMethod::ArcCenterStartChord:
        return "圆心-起点-长度";
    case DrawMethod::ArcStartCenterEnd:
        return "起点-圆心-端点";
    case DrawMethod::ArcStartCenterAngle:
        return "起点-圆心-角度";
    case DrawMethod::ArcStartCenterChord:
        return "起点-圆心-长度";
    case DrawMethod::ArcStartEndAngle:
        return "起点-端点-角度";
    case DrawMethod::ArcStartEndRadius:
        return "起点-端点-半径";
    case DrawMethod::ArcStartEndDirection:
        return "起点-端点-方向";
    case DrawMethod::ArcContinue:
        return "连续";
    case DrawMethod::EllipseCenterAxis:
        return "中心-轴端点";
    case DrawMethod::EllipseAxisEndpoints:
        return "轴端点";
    case DrawMethod::ParabolaVertexFocus:
        return "顶点-焦点";
    case DrawMethod::ParabolaThreePoints:
        return "三点";
    case DrawMethod::HyperbolaCenterVertexMinor:
        return "中心-顶点-短轴";
    case DrawMethod::SplineControlPoints:
        return "控制点";
    case DrawMethod::SplineFitPoints:
        return "拟合点";
    case DrawMethod::PolylineContinuous:
        return "连续多段线";
    case DrawMethod::TextInsertionPoint:
        return "插入点";
    case DrawMethod::MTextCornerWidth:
        return "两点定宽";
    case DrawMethod::MTextFixedWidth:
        return "固定宽度";
    case DrawMethod::FillFromSelection:
        return "选择闭合区域";
    default:
        return "默认";
    }
}

DrawToolResult DrawToolEngine::ProcessInput(
    ToolMode tool,
    const DrawToolOptions& options,
    const std::vector<GeoPoint>& inputPoints,
    const GeoPoint& cursorPoint,
    const CadDocument* document)
{
    const DrawMethod method = options.method;
    DrawToolResult result;

    if (tool == ToolMode::DrawPoint) {
        if (inputPoints.empty()) {
            return NeedPoints(1, "指定点位置");
        }
        return Complete(std::make_unique<PointEntity>(IdGenerator::Next(), inputPoints.front()));
    }

    if (tool == ToolMode::DrawLine) {
        // AutoCAD LINE: continuous segments. Each pair creates a LineEntity, then
        // prompting continues from the last endpoint until Enter / right-click.
        if (method == DrawMethod::LineTwoPoints) {
            if (inputPoints.empty()) {
                return NeedPoints(2, "指定第一点");
            }
            if (inputPoints.size() < 2) {
                result = NeedPoints(2, "指定下一点或 [放弃(U)/闭合(C)]");
                result.preview.AddSegment(inputPoints.front(), cursorPoint);
                return result;
            }
            if (Distance2D(inputPoints[0], inputPoints[1]) <= Tolerance::Model) {
                result = NeedPoints(2, "指定下一点或 [放弃(U)/闭合(C)]");
                result.preview.AddSegment(inputPoints.front(), cursorPoint);
                result.prompt = "点重合，请重新指定下一点";
                return result;
            }
            return CompleteAndContinueFromLast(
                std::make_unique<LineEntity>(IdGenerator::Next(), GeoLine(inputPoints[0], inputPoints[1])),
                "指定下一点或 [放弃(U)/闭合(C)]");
        }
        // Point + length + direction (second pick defines both length and angle).
        if (inputPoints.empty()) {
            return NeedPoints(2, "指定直线起点");
        }
        if (inputPoints.size() < 2) {
            result = NeedPoints(2, "指定长度和方向点");
            result.preview.AddSegment(inputPoints.front(), cursorPoint);
            return result;
        }
        if (Distance2D(inputPoints[0], inputPoints[1]) <= Tolerance::Model) {
            result = NeedPoints(2, "指定长度和方向点");
            result.preview.AddSegment(inputPoints.front(), cursorPoint);
            result.prompt = "点重合，请重新指定长度和方向点";
            return result;
        }
        return Complete(std::make_unique<LineEntity>(
            IdGenerator::Next(), GeoLine(inputPoints[0], inputPoints[1])));
    }

    if (tool == ToolMode::DrawRay) {
        const bool directionMode = method == DrawMethod::RayStartDirection;
        const std::string startPrompt = "指定射线起点";
        const std::string throughPrompt = directionMode ? "指定方向" : "指定通过点";

        if (inputPoints.size() < 1) {
            return NeedPoints(2, startPrompt);
        }
        result = NeedPoints(2, throughPrompt);
        result.preview.AddRay(inputPoints.front(), cursorPoint);
        if (inputPoints.size() >= 2) {
            const GeoPoint& origin = inputPoints[0];
            GeoPoint throughPoint = inputPoints[1];
            if (directionMode) {
                const double dx = throughPoint.X() - origin.X();
                const double dy = throughPoint.Y() - origin.Y();
                const double len = std::hypot(dx, dy);
                if (len <= 1.0e-12) {
                    return result;
                }
                throughPoint = GeoPoint(origin.X() + dx / len, origin.Y() + dy / len);
            } else if (Distance2D(origin, throughPoint) <= 1.0e-12) {
                return result;
            }
            return CompleteAndRepeatBase(
                std::make_unique<RayEntity>(IdGenerator::Next(), GeoRay(origin, throughPoint)),
                throughPrompt);
        }
        return result;
    }

    if (tool == ToolMode::DrawXLine) {
        if (inputPoints.size() < 1) {
            return NeedPoints(2, "指定构造线上一点");
        }
        result = NeedPoints(2, "指定方向点");
        result.preview.AddXLine(inputPoints.front(), cursorPoint);
        if (inputPoints.size() >= 2) {
            return Complete(std::make_unique<XLineEntity>(IdGenerator::Next(), GeoXLine(inputPoints[0], inputPoints[1])));
        }
        return result;
    }

    if (tool == ToolMode::DrawRectangle) {
        if (method == DrawMethod::RectCenterCorner) {
            if (inputPoints.size() < 1) {
                return NeedPoints(2, "指定矩形中心");
            }
            if (inputPoints.size() < 2) {
                const auto points = DrawAlgorithms::RectangleFromCenter(inputPoints.front(), cursorPoint);
                result = NeedPoints(2, "指定角点");
                result.preview.AddPolyline(points, true);
                return result;
            }
            const auto points = DrawAlgorithms::RectangleFromCenter(inputPoints[0], inputPoints[1]);
            return Complete(std::make_unique<RectangleEntity>(IdGenerator::Next(), GeoRectangle(points.front(), points[2])));
        }
        if (inputPoints.size() < 1) {
            return NeedPoints(2, "指定第一个角点");
        }
        if (inputPoints.size() < 2) {
            result = NeedPoints(2, "指定对角点");
            result.preview.AddPolyline(DrawAlgorithms::RectanglePoints(inputPoints.front(), cursorPoint), true);
            return result;
        }
        return Complete(std::make_unique<RectangleEntity>(IdGenerator::Next(), GeoRectangle(inputPoints[0], inputPoints[1])));
    }

    if (tool == ToolMode::DrawPolygon) {
        const int sides = std::max(3, options.polygonSides);
        const bool byEdge = method == DrawMethod::PolygonEdge;
        const bool circumscribed = method == DrawMethod::PolygonCircumscribed;
        if (byEdge) {
            if (inputPoints.empty()) {
                return NeedPoints(2, "指定边的第一个端点");
            }
            const GeoPoint second = inputPoints.size() >= 2 ? inputPoints[1] : cursorPoint;
            GeoPoint center;
            double vertexRadius = 0.0;
            double rotation = 0.0;
            if (DrawAlgorithms::RegularPolygonFromEdge(
                    inputPoints.front(), second, sides, center, vertexRadius, rotation)) {
                result.preview.AddPolyline(
                    DrawAlgorithms::RegularPolygonPoints(center, vertexRadius, sides, rotation),
                    true);
            }
            if (inputPoints.size() < 2) {
                result.needsMorePoints = true;
                result.requiredPoints = 2;
                result.prompt = "指定边的第二个端点";
                return result;
            }
            if (vertexRadius <= Tolerance::Model) {
                return result;
            }
            return Complete(std::make_unique<PolygonEntity>(
                IdGenerator::Next(),
                GeoPolygon(center, vertexRadius, sides, rotation)));
        }
        if (inputPoints.empty()) {
            return NeedPoints(2, "指定正多边形的中心点或 [边(E)]");
        }
        const GeoPoint radiusPick = inputPoints.size() >= 2 ? inputPoints[1] : cursorPoint;
        double vertexRadius = 0.0;
        double rotation = 0.0;
        DrawAlgorithms::RegularPolygonFromCenterPick(
            inputPoints.front(), radiusPick, sides, circumscribed, vertexRadius, rotation);
        result.preview.AddPolyline(
            DrawAlgorithms::RegularPolygonPoints(inputPoints.front(), vertexRadius, sides, rotation),
            true);
        if (inputPoints.size() < 2) {
            result.needsMorePoints = true;
            result.requiredPoints = 2;
            result.prompt = "指定圆的半径";
            return result;
        }
        if (vertexRadius <= Tolerance::Model) {
            return result;
        }
        return Complete(std::make_unique<PolygonEntity>(
            IdGenerator::Next(),
            GeoPolygon(inputPoints.front(), vertexRadius, sides, rotation)));
    }

    if (tool == ToolMode::DrawCircle) {
        if (method == DrawMethod::CircleTwoPoints) {
            if (inputPoints.size() < 2) {
                if (inputPoints.empty()) {
                    return NeedPoints(2, "指定圆直径的第一个端点");
                }
                result = NeedPoints(2, "指定圆直径的第二个端点");
                if (auto circle = DrawAlgorithms::CircleFromTwoPoints(inputPoints.front(), cursorPoint)) {
                    result.preview.AddCircle(*circle);
                }
                return result;
            }
            if (auto circle = DrawAlgorithms::CircleFromTwoPoints(inputPoints[0], inputPoints[1])) {
                return Complete(std::make_unique<CircleEntity>(IdGenerator::Next(), *circle));
            }
            return result;
        }
        if (method == DrawMethod::CircleThreePoints) {
            if (inputPoints.size() < 3) {
                if (inputPoints.size() == 2) {
                    result = NeedPoints(3, "指定圆上的第三个点");
                    if (auto circle = DrawAlgorithms::CircleFromThreePoints(inputPoints[0], inputPoints[1], cursorPoint)) {
                        result.preview.AddCircle(*circle);
                    }
                    return result;
                }
                if (inputPoints.size() == 1) {
                    return NeedPoints(3, "指定圆上的第二个点");
                }
                return NeedPoints(3, "指定圆上的第一个点");
            }
            if (auto circle = DrawAlgorithms::CircleFromThreePoints(inputPoints[0], inputPoints[1], inputPoints[2])) {
                return Complete(std::make_unique<CircleEntity>(IdGenerator::Next(), *circle));
            }
            return result;
        }
        if (method == DrawMethod::CircleTanTanRadius) {
            const std::vector<const Entity*> entities = CollectEntities(document);
            const double pickTolerance = options.pickTolerance > Tolerance::Model ? options.pickTolerance : 20.0;
            DrawContext context{&entities, pickTolerance};
            if (inputPoints.size() < 3) {
                if (inputPoints.size() == 2) {
                    result = NeedPoints(3, "指定圆的半径");
                    const auto first = NearestTangentSupport(inputPoints[0], context);
                    const auto second = NearestTangentSupport(inputPoints[1], context);
                    const double radius = Distance2D(inputPoints[1], cursorPoint);
                    if (first && second && radius > Tolerance::Model) {
                        if (auto circle = DrawAlgorithms::CircleTanTanRadius(*first, *second, radius)) {
                            result.preview.AddCircle(*circle);
                        }
                    }
                    return result;
                }
                if (inputPoints.size() == 1) {
                    return NeedPoints(3, "指定圆的第二条切线");
                }
                return NeedPoints(3, "指定圆的第一条切线");
            }
            const auto first = NearestTangentSupport(inputPoints[0], context);
            const auto second = NearestTangentSupport(inputPoints[1], context);
            if (!first || !second) {
                result = NeedPoints(3, "圆不存在");
                return result;
            }
            const double radius = Distance2D(inputPoints[1], inputPoints[2]);
            if (auto circle = DrawAlgorithms::CircleTanTanRadius(*first, *second, radius)) {
                return Complete(std::make_unique<CircleEntity>(IdGenerator::Next(), *circle));
            }
            result = NeedPoints(3, "圆不存在");
            return result;
        }
        if (method == DrawMethod::CircleTanTanTan) {
            const std::vector<const Entity*> entities = CollectEntities(document);
            const double pickTolerance = options.pickTolerance > Tolerance::Model ? options.pickTolerance : 20.0;
            DrawContext context{&entities, pickTolerance};
            if (inputPoints.size() < 3) {
                if (inputPoints.size() == 2) {
                    result = NeedPoints(3, "指定圆的第三条切线");
                    const auto first = NearestTangentSupport(inputPoints[0], context);
                    const auto second = NearestTangentSupport(inputPoints[1], context);
                    const auto third = NearestTangentSupport(cursorPoint, context);
                    if (first && second && third) {
                        if (auto circle = DrawAlgorithms::CircleTanTanTan(*first, *second, *third)) {
                            result.preview.AddCircle(*circle);
                        }
                    }
                    return result;
                }
                if (inputPoints.size() == 1) {
                    return NeedPoints(3, "指定圆的第二条切线");
                }
                return NeedPoints(3, "指定圆的第一条切线");
            }
            const auto first = NearestTangentSupport(inputPoints[0], context);
            const auto second = NearestTangentSupport(inputPoints[1], context);
            const auto third = NearestTangentSupport(inputPoints[2], context);
            if (!first || !second || !third) {
                result = NeedPoints(3, "圆不存在");
                return result;
            }
            if (auto circle = DrawAlgorithms::CircleTanTanTan(*first, *second, *third)) {
                return Complete(std::make_unique<CircleEntity>(IdGenerator::Next(), *circle));
            }
            result = NeedPoints(3, "圆不存在");
            return result;
        }
        if (method == DrawMethod::CircleCenterDiameter) {
            if (inputPoints.size() < 1) {
                return NeedPoints(2, "指定圆心");
            }
            if (inputPoints.size() < 2) {
                result = NeedPoints(2, "指定圆的直径");
                if (auto circle = DrawAlgorithms::CircleFromCenterDiameter(inputPoints.front(), cursorPoint)) {
                    result.preview.AddCircle(*circle);
                }
                return result;
            }
            if (auto circle = DrawAlgorithms::CircleFromCenterDiameter(inputPoints[0], inputPoints[1])) {
                return Complete(std::make_unique<CircleEntity>(IdGenerator::Next(), *circle));
            }
            return result;
        }
        if (inputPoints.size() < 1) {
            return NeedPoints(2, "指定圆心");
        }
        if (inputPoints.size() < 2) {
            result = NeedPoints(2, "指定圆的半径或");
            if (auto circle = DrawAlgorithms::CircleFromCenterRadius(inputPoints.front(), cursorPoint)) {
                result.preview.AddCircle(*circle);
            }
            return result;
        }
        if (auto circle = DrawAlgorithms::CircleFromCenterRadius(inputPoints[0], inputPoints[1])) {
            return Complete(std::make_unique<CircleEntity>(IdGenerator::Next(), *circle));
        }
        return result;
    }

    if (tool == ToolMode::DrawArc) {
        const auto completeArc = [&](const GeoArc& arc) {
            return Complete(std::make_unique<ArcEntity>(IdGenerator::Next(), arc));
        };

        if (method == DrawMethod::ArcThreePoints) {
            if (inputPoints.size() < 1) {
                return NeedPoints(3, "指定圆弧起点");
            }
            if (inputPoints.size() < 2) {
                result = NeedPoints(3, "指定圆弧第二点");
                result.preview.AddSegment(inputPoints[0], cursorPoint);
                result.preview.AddPoint(inputPoints[0]);
                return result;
            }
            if (inputPoints.size() < 3) {
                result = NeedPoints(3, "指定圆弧端点");
                result.preview.AddSegment(inputPoints[0], inputPoints[1]);
                result.preview.AddSegment(inputPoints[1], cursorPoint);
                result.preview.AddPoint(inputPoints[0]);
                result.preview.AddPoint(inputPoints[1]);
                if (auto arc = DrawAlgorithms::ArcFromThreePoints(inputPoints[0], inputPoints[1], cursorPoint)) {
                    result.preview.AddArc(*arc);
                }
                return result;
            }
            if (auto arc = DrawAlgorithms::ArcFromThreePoints(inputPoints[0], inputPoints[1], inputPoints[2])) {
                return completeArc(*arc);
            }
            return result;
        }

        if (method == DrawMethod::ArcCenterStartEnd) {
            if (inputPoints.size() < 1) {
                return NeedPoints(3, "指定圆弧的圆心");
            }
            if (inputPoints.size() < 2) {
                // AutoCAD 图2：半径橡胶筋/0°基线/角度弧由 CadView 极轴层绘制，此处不重复。
                return NeedPoints(3, "指定圆弧的起点");
            }
            if (inputPoints.size() < 3) {
                // AutoCAD 图3：实线弧；虚线整圆由极轴引导层绘制。
                result = NeedPoints(3, "指定圆弧的端点");
                const double signedIncluded = SignedIncludedAngleDegreesShortest(
                    inputPoints[1], inputPoints[0], cursorPoint);
                if (auto arc = DrawAlgorithms::ArcFromStartCenterAngle(
                        inputPoints[1], inputPoints[0], signedIncluded)) {
                    result.preview.AddArc(*arc);
                }
                return result;
            }
            const double signedIncluded = SignedIncludedAngleDegreesShortest(
                inputPoints[1], inputPoints[0], inputPoints[2]);
            if (auto arc = DrawAlgorithms::ArcFromStartCenterAngle(
                    inputPoints[1], inputPoints[0], signedIncluded)) {
                return completeArc(*arc);
            }
            return result;
        }

        if (method == DrawMethod::ArcCenterStartAngle) {
            if (inputPoints.size() < 1) {
                return NeedPoints(3, "指定圆弧的圆心");
            }
            if (inputPoints.size() < 2) {
                // 第 2 点起点：半径 = |圆心→起点|；极轴引导由 CadView 绘制。
                return NeedPoints(3, "指定圆弧的起点");
            }
            if (inputPoints.size() < 3) {
                // AutoCAD 圆心-起点-角度：定中心 → 定半径与起始角 → 定包含角（正=逆时针，负/Ctrl=顺时针）。
                result = NeedPoints(3, "指定夹角(按住 Ctrl 键以切换方向)");
                const double angleDeg = ResolveCenterStartIncludedAngleDegrees(
                    inputPoints[1],
                    inputPoints[0],
                    cursorPoint,
                    options.clockwise,
                    options.hasExplicitIncludedAngle,
                    options.explicitIncludedAngleDegrees);
                if (auto arc = DrawAlgorithms::ArcFromStartCenterAngle(
                        inputPoints[1], inputPoints[0], angleDeg)) {
                    result.preview.AddArc(*arc);
                }
                return result;
            }
            {
                const double angleDeg = ResolveCenterStartIncludedAngleDegrees(
                    inputPoints[1],
                    inputPoints[0],
                    inputPoints[2],
                    options.clockwise,
                    options.hasExplicitIncludedAngle,
                    options.explicitIncludedAngleDegrees);
                if (auto arc = DrawAlgorithms::ArcFromStartCenterAngle(
                        inputPoints[1], inputPoints[0], angleDeg)) {
                    return completeArc(*arc);
                }
            }
            result = NeedPoints(3, "指定夹角(按住 Ctrl 键以切换方向)");
            result.prompt = "夹角无效，请重新指定";
            return result;
        }

        if (method == DrawMethod::ArcCenterStartChord) {
            if (inputPoints.size() < 1) {
                return NeedPoints(3, "指定圆弧的圆心");
            }
            if (inputPoints.size() < 2) {
                // 第 2 点起点：半径 = |圆心→起点|；极轴引导由 CadView 绘制。
                return NeedPoints(3, "指定圆弧的起点");
            }

            // AutoCAD 圆心-起点-长度：与起点-圆心-长度几何相同，仅输入顺序不同。
            // - 长度 = |起点→光标|（弦长，不是弧长）
            // - 第三点只提供弦长幅值；上下移动不切换圆弧侧
            // - 正数：该侧劣弧；负数 / Ctrl：同一端点改走优弧
            auto resolveChordLength = [&](const GeoPoint& pick) -> double {
                if (options.hasExplicitChordLength
                    && std::abs(options.explicitChordLength) > Tolerance::Model) {
                    return options.explicitChordLength;
                }
                return Distance2D(inputPoints[1], pick);
            };

            if (inputPoints.size() < 3) {
                result = NeedPoints(3, "指定弦长(按住 Ctrl 键以切换方向)");
                const GeoPoint& center = inputPoints[0];
                const GeoPoint& start = inputPoints[1];
                const double chordLen = resolveChordLength(cursorPoint);
                if (auto arc = DrawAlgorithms::ArcFromStartCenterChord(
                        start, center, chordLen, cursorPoint, options.clockwise)) {
                    result.preview.AddArc(*arc);
                } else {
                    result.preview.AddSegment(center, start);
                }
                return result;
            }
            const double chordLen = resolveChordLength(inputPoints[2]);
            if (auto arc = DrawAlgorithms::ArcFromStartCenterChord(
                    inputPoints[1],
                    inputPoints[0],
                    chordLen,
                    inputPoints[2],
                    options.clockwise)) {
                return completeArc(*arc);
            }
            result = NeedPoints(3, "指定弦长(按住 Ctrl 键以切换方向)");
            result.prompt = "长度无效，请重新指定";
            return result;
        }

        if (method == DrawMethod::ArcStartCenterEnd) {
            if (inputPoints.size() < 1) {
                return NeedPoints(3, "指定圆弧的起点");
            }
            if (inputPoints.size() < 2) {
                result = NeedPoints(3, "指定圆弧的圆心");
                result.preview.AddSegment(inputPoints[0], cursorPoint);
                AppendCenterGuides(result.preview, cursorPoint, inputPoints[0], cursorPoint, true);
                return result;
            }
            if (inputPoints.size() < 3) {
                // AutoCAD：半径锁定为 |圆心→起点|；第三点只提供方向。
                // 预览只画实线弧，不画整圆/半径三角形/弦。
                result = NeedPoints(3, "指定圆弧的端点(按住 Ctrl 键以切换方向)");
                const double angleDeg = ResolveCenterStartIncludedAngleDegrees(
                    inputPoints[0],
                    inputPoints[1],
                    cursorPoint,
                    options.clockwise,
                    options.hasExplicitIncludedAngle,
                    options.explicitIncludedAngleDegrees);
                if (auto arc = DrawAlgorithms::ArcFromStartCenterAngle(
                        inputPoints[0], inputPoints[1], angleDeg)) {
                    result.preview.AddArc(*arc);
                }
                return result;
            }
            {
                const double angleDeg = ResolveCenterStartIncludedAngleDegrees(
                    inputPoints[0],
                    inputPoints[1],
                    inputPoints[2],
                    options.clockwise,
                    options.hasExplicitIncludedAngle,
                    options.explicitIncludedAngleDegrees);
                if (auto arc = DrawAlgorithms::ArcFromStartCenterAngle(
                        inputPoints[0], inputPoints[1], angleDeg)) {
                    return completeArc(*arc);
                }
            }
            result = NeedPoints(3, "指定圆弧的端点(按住 Ctrl 键以切换方向)");
            result.prompt = "端点无效，请重新指定";
            return result;
        }

        if (method == DrawMethod::ArcStartCenterAngle) {
            if (inputPoints.size() < 1) {
                return NeedPoints(3, "指定圆弧的起点");
            }
            if (inputPoints.size() < 2) {
                result = NeedPoints(3, "指定圆弧的圆心");
                result.preview.AddSegment(inputPoints[0], cursorPoint);
                AppendCenterGuides(result.preview, cursorPoint, inputPoints[0], cursorPoint, true);
                return result;
            }
            if (inputPoints.size() < 3) {
                // AutoCAD 起点-圆心-角度：与圆心-起点-角度几何相同，仅输入顺序不同。
                result = NeedPoints(3, "指定夹角(按住 Ctrl 键以切换方向)");
                const double angleDeg = ResolveCenterStartIncludedAngleDegrees(
                    inputPoints[0],
                    inputPoints[1],
                    cursorPoint,
                    options.clockwise,
                    options.hasExplicitIncludedAngle,
                    options.explicitIncludedAngleDegrees);
                if (auto arc = DrawAlgorithms::ArcFromStartCenterAngle(
                        inputPoints[0], inputPoints[1], angleDeg)) {
                    result.preview.AddArc(*arc);
                }
                return result;
            }
            {
                const double angleDeg = ResolveCenterStartIncludedAngleDegrees(
                    inputPoints[0],
                    inputPoints[1],
                    inputPoints[2],
                    options.clockwise,
                    options.hasExplicitIncludedAngle,
                    options.explicitIncludedAngleDegrees);
                if (auto arc = DrawAlgorithms::ArcFromStartCenterAngle(
                        inputPoints[0], inputPoints[1], angleDeg)) {
                    return completeArc(*arc);
                }
            }
            result = NeedPoints(3, "指定夹角(按住 Ctrl 键以切换方向)");
            result.prompt = "夹角无效，请重新指定";
            return result;
        }

        if (method == DrawMethod::ArcStartCenterChord) {
            if (inputPoints.size() < 1) {
                return NeedPoints(3, "指定圆弧起点");
            }
            if (inputPoints.size() < 2) {
                result = NeedPoints(3, "指定圆心");
                result.preview.AddSegment(inputPoints[0], cursorPoint);
                AppendCenterGuides(result.preview, cursorPoint, inputPoints[0], cursorPoint, true);
                return result;
            }

            // AutoCAD Start-Center-Length：
            // - 长度 = |起点→光标|（弦长）
            // - 第三点只提供弦长幅值；上下移动不切换圆弧侧
            // - 正数：该侧劣弧；负数 / Ctrl：同一端点改走优弧
            // 端点落在圆上，不跟自由光标。
            auto resolveChordLength = [&](const GeoPoint& pick) -> double {
                if (options.hasExplicitChordLength
                    && std::abs(options.explicitChordLength) > Tolerance::Model) {
                    return options.explicitChordLength;
                }
                return Distance2D(inputPoints[0], pick);
            };

            if (inputPoints.size() < 3) {
                result = NeedPoints(3, "指定弦长(按住 Ctrl 键以切换方向)");
                const GeoPoint& start = inputPoints[0];
                const GeoPoint& center = inputPoints[1];
                const double chordLen = resolveChordLength(cursorPoint);
                if (auto arc = DrawAlgorithms::ArcFromStartCenterChord(
                        start, center, chordLen, cursorPoint, options.clockwise)) {
                    result.preview.AddArc(*arc);
                } else {
                    result.preview.AddSegment(center, start);
                }
                return result;
            }
            const double chordLen = resolveChordLength(inputPoints[2]);
            if (auto arc = DrawAlgorithms::ArcFromStartCenterChord(
                    inputPoints[0],
                    inputPoints[1],
                    chordLen,
                    inputPoints[2],
                    options.clockwise)) {
                return completeArc(*arc);
            }
            result = NeedPoints(3, "指定弦长(按住 Ctrl 键以切换方向)");
            result.prompt = "长度无效，请重新指定";
            return result;
        }

        if (method == DrawMethod::ArcStartEndAngle) {
            if (inputPoints.size() < 1) {
                return NeedPoints(3, "指定圆弧起点");
            }
            if (inputPoints.size() < 2) {
                result = NeedPoints(3, "指定圆弧端点");
                result.preview.AddSegment(inputPoints[0], cursorPoint);
                return result;
            }
            if (inputPoints.size() < 3) {
                // AutoCAD SEA：
                // - DYN 显示幅值 θ（如 135°）
                // - 光标在起点上方 → θ 劣弧；下方 → 360°-θ 优弧
                // - Ctrl 再翻转优/劣
                result = NeedPoints(3, "指定夹角(按住 Ctrl 键以切换方向)");
                const GeoPoint& startPt = inputPoints[0];
                const GeoPoint& endPt = inputPoints[1];
                double magnitude = 0.0;
                if (options.hasExplicitIncludedAngle) {
                    magnitude = std::abs(options.explicitIncludedAngleDegrees);
                } else {
                    const double dx = cursorPoint.X() - startPt.X();
                    const double dy = cursorPoint.Y() - startPt.Y();
                    if (std::hypot(dx, dy) > Tolerance::Model) {
                        magnitude = std::abs(std::atan2(dy, dx) * 180.0 / 3.14159265358979323846);
                    }
                }
                const double angleDeg = ResolveSeaSweepDegrees(
                    startPt, endPt, cursorPoint, magnitude, options.clockwise);
                if (auto arc = DrawAlgorithms::ArcFromStartEndAngle(
                        startPt, endPt, angleDeg, cursorPoint)) {
                    result.preview.AddArc(*arc);
                }
                return result;
            }
            {
                const GeoPoint& startPt = inputPoints[0];
                const GeoPoint& endPt = inputPoints[1];
                const GeoPoint& pick = inputPoints[2];
                double magnitude = 0.0;
                if (options.hasExplicitIncludedAngle) {
                    magnitude = std::abs(options.explicitIncludedAngleDegrees);
                } else {
                    const double dx = pick.X() - startPt.X();
                    const double dy = pick.Y() - startPt.Y();
                    if (std::hypot(dx, dy) > Tolerance::Model) {
                        magnitude = std::abs(std::atan2(dy, dx) * 180.0 / 3.14159265358979323846);
                    }
                }
                const double angleDeg = ResolveSeaSweepDegrees(
                    startPt, endPt, pick, magnitude, options.clockwise);
                if (auto arc = DrawAlgorithms::ArcFromStartEndAngle(
                        startPt, endPt, angleDeg, pick)) {
                    return completeArc(*arc);
                }
            }
            result = NeedPoints(3, "指定夹角(按住 Ctrl 键以切换方向)");
            result.prompt = "包含角无效，请重新指定";
            return result;
        }

        if (method == DrawMethod::ArcStartEndRadius) {
            // AutoCAD Start-End-Radius：1) 起点 2) 端点 3) 半径。
            // 第三点极轴基点为端点；半径 = 端点→指定点距离；Ctrl 翻转方向。
            if (inputPoints.size() < 1) {
                return NeedPoints(3, "指定圆弧起点");
            }
            if (inputPoints.size() < 2) {
                result = NeedPoints(3, "指定圆弧端点");
                result.preview.AddSegment(inputPoints[0], cursorPoint);
                return result;
            }

            auto buildSer = [&](const GeoPoint& pick) -> std::optional<GeoArc> {
                const GeoPoint& start = inputPoints[0];
                const GeoPoint& end = inputPoints[1];
                const double chord = Distance2D(start, end);
                double radiusMag = Distance2D(end, pick);
                if (radiusMag <= Tolerance::Model
                    && std::abs(options.numericValue) > Tolerance::Model) {
                    radiusMag = std::abs(options.numericValue);
                }
                if (radiusMag + Tolerance::Model < chord * 0.5) {
                    return std::nullopt;
                }
                // 光标相对弦的侧向 → 圆心所在侧（默认劣弧）。
                // Ctrl / 负半径 / largeArc → 同侧优弧（长扫掠），不把圆心翻到对侧。
                const double dx = end.X() - start.X();
                const double dy = end.Y() - start.Y();
                const double cross =
                    dx * (pick.Y() - start.Y()) - dy * (pick.X() - start.X());
                const bool clockwise = cross < 0.0;
                const bool largeArc = options.clockwise
                    || options.numericValue < 0.0
                    || options.largeArc;
                return DrawAlgorithms::ArcFromStartEndRadius(
                    start, end, radiusMag, largeArc, clockwise);
            };

            if (inputPoints.size() < 3) {
                result = NeedPoints(3, "指定圆弧的半径(按住 Ctrl 键以切换方向)");
                if (auto arc = buildSer(cursorPoint)) {
                    result.preview.AddArc(*arc);
                }
                return result;
            }
            if (auto arc = buildSer(inputPoints[2])) {
                return completeArc(*arc);
            }
            result = NeedPoints(3, "指定圆弧的半径(按住 Ctrl 键以切换方向)");
            result.prompt = "半径无效，请重新指定";
            return result;
        }

        if (method == DrawMethod::ArcStartEndDirection) {
            // AutoCAD Start-End-Direction：
            // 1) 起点 2) 端点 3) 起点处切线方向。
            // 第三点方向点直接以起点为基点给出切线方向。
            if (inputPoints.size() < 1) {
                return NeedPoints(3, "指定圆弧起点");
            }
            if (inputPoints.size() < 2) {
                result = NeedPoints(3, "指定圆弧的端点");
                result.preview.AddSegment(inputPoints[0], cursorPoint);
                return result;
            }

            auto buildSed = [&](const GeoPoint& pick) -> std::optional<GeoArc> {
                const GeoPoint& start = inputPoints[0];
                const GeoPoint& end = inputPoints[1];
                return DrawAlgorithms::ArcFromStartEndDirection(
                    start, end, pick);
            };

            if (inputPoints.size() < 3) {
                result = NeedPoints(3, "指定起点切线方向");
                if (auto arc = buildSed(cursorPoint)) {
                    result.preview.AddArc(*arc);
                }
                return result;
            }
            if (auto arc = buildSed(inputPoints[2])) {
                return completeArc(*arc);
            }
            return result;
        }

        if (method == DrawMethod::ArcContinue) {
            if (!options.arcContinue.valid) {
                result.needsMorePoints = true;
                result.prompt = "连续：没有可衔接的直线、多段线或圆弧";
                return result;
            }

            const GeoPoint& start = options.arcContinue.start;
            const GeoPoint& tangentUnit = options.arcContinue.tangentDirection;

            if (inputPoints.empty()) {
                result = NeedPoints(1, "指定圆弧的端点");
                if (auto arc = DrawAlgorithms::ArcFromTangentStartEnd(start, tangentUnit, cursorPoint)) {
                    result.preview.AddArc(*arc);
                }
                return result;
            }

            const GeoPoint& end = inputPoints.back();
            if (Distance2D(start, end) <= Tolerance::Model) {
                result = NeedPoints(1, "指定圆弧的端点");
                result.prompt = "端点不能与起点重合";
                if (auto arc = DrawAlgorithms::ArcFromTangentStartEnd(start, tangentUnit, cursorPoint)) {
                    result.preview.AddArc(*arc);
                }
                return result;
            }

            if (auto arc = DrawAlgorithms::ArcFromTangentStartEnd(start, tangentUnit, end)) {
                return CompleteAndContinueFromLast(
                    std::make_unique<ArcEntity>(IdGenerator::Next(), *arc),
                    "指定圆弧的端点");
            }
            result.needsMorePoints = true;
            result.prompt = "无法生成相切圆弧，请重新指定端点";
            return result;
        }

        return result;
    }

    if (tool == ToolMode::DrawEllipse) {
        if (method == DrawMethod::EllipseAxisEndpoints) {
            if (inputPoints.size() < 3) {
                if (inputPoints.size() == 2) {
                    result = NeedPoints(3, "指定短轴点");
                    result.preview.AddEllipse(DrawAlgorithms::EllipseFromAxisEndpoints(
                        inputPoints[0], inputPoints[1], cursorPoint));
                    return result;
                }
                return NeedPoints(3, "指定长轴端点");
            }
            return Complete(std::make_unique<EllipseEntity>(
                0,
                GeoEllipse(DrawAlgorithms::EllipseFromAxisEndpoints(inputPoints[0], inputPoints[1], inputPoints[2]))));
        }
        if (inputPoints.size() < 3) {
            if (inputPoints.size() == 1) {
                result = NeedPoints(3, "指定长轴端点");
                return result;
            }
            result = NeedPoints(3, "指定短轴点");
            result.preview.AddEllipse(DrawAlgorithms::EllipseFromCenterAxis(inputPoints[0], inputPoints[1], cursorPoint));
            return result;
        }
        return Complete(std::make_unique<EllipseEntity>(
            0,
            GeoEllipse(DrawAlgorithms::EllipseFromCenterAxis(inputPoints[0], inputPoints[1], inputPoints[2]))));
    }

    if (tool == ToolMode::DrawParabola) {
        if (method == DrawMethod::ParabolaThreePoints) {
            if (inputPoints.size() < 3) {
                return NeedPoints(3, "指定抛物线三点");
            }
            const GeoPoint vertex = inputPoints[0];
            const GeoPoint focus((inputPoints[1].X() + inputPoints[2].X()) * 0.5, (inputPoints[1].Y() + inputPoints[2].Y()) * 0.5);
            return Complete(std::make_unique<ParabolaEntity>(IdGenerator::Next(), GeoParabola(vertex, focus)));
        }
        if (inputPoints.size() < 2) {
            if (inputPoints.empty()) {
                return NeedPoints(2, "指定抛物线顶点");
            }
            result = NeedPoints(2, "指定焦点");
            result.preview.AddPolyline(DrawAlgorithms::DiscretizeParabola(inputPoints.front(), cursorPoint), false);
            return result;
        }
        return Complete(std::make_unique<ParabolaEntity>(IdGenerator::Next(), GeoParabola(inputPoints[0], inputPoints[1])));
    }

    if (tool == ToolMode::DrawHyperbola) {
        if (inputPoints.size() < 3) {
            if (inputPoints.size() == 2) {
                result = NeedPoints(3, "指定短半轴点");
                result.preview.AddPolyline(
                    DrawAlgorithms::DiscretizeHyperbola(inputPoints[0], inputPoints[1], Distance2D(inputPoints[1], cursorPoint)),
                    false);
                return result;
            }
            return NeedPoints(3, "指定中心、顶点");
        }
        return Complete(std::make_unique<HyperbolaEntity>(
            0,
            GeoHyperbola(inputPoints[0], inputPoints[1], Distance2D(inputPoints[1], inputPoints[2]))));
    }

    if (tool == ToolMode::DrawSpline) {
        result.needsMorePoints = true;
        if (inputPoints.empty()) {
            result.prompt = "指定第一个点或 [方式(M)/节点(K)/对象(O)]";
        } else if (inputPoints.size() == 1) {
            result.prompt = "输入下一个点或 [起点切向(T)/公差(L)]";
        } else if (inputPoints.size() == 2) {
            result.prompt = "输入下一个点或 [起点切向(T)/公差(L)/放弃(U)]";
        } else {
            result.prompt = "输入下一个点或 [闭合(C)/起点切向(T)/公差(L)/放弃(U)]";
        }
        if (!inputPoints.empty()) {
            if (Distance2D(inputPoints.back(), cursorPoint) <= Tolerance::Model) {
                result.prompt = "点重合，请重新指定下一个点";
                result.preview.AddSpline(GeoSpline(
                    inputPoints,
                    options.method == DrawMethod::SplineFitPoints
                        ? SplineMode::FitPoints
                        : SplineMode::ControlPoints));
                return result;
            }
            std::vector<GeoPoint> previewPoints = inputPoints;
            previewPoints.push_back(cursorPoint);
            const SplineMode mode = options.method == DrawMethod::SplineFitPoints
                ? SplineMode::FitPoints
                : SplineMode::ControlPoints;
            result.preview.AddSpline(GeoSpline(previewPoints, mode));
            if (mode == SplineMode::ControlPoints) {
                result.preview.AddPolyline(previewPoints, false);
            }
        }
        return result;
    }

    if (tool == ToolMode::DrawPolyline) {
        result.needsMorePoints = true;
        result.requiredPoints = 2;
        result.prompt = PolylineNextPrompt(inputPoints.size(), options.polylineArcMode);
        if (inputPoints.empty()) {
            return result;
        }

        DrawAlgorithms::AppendPolylineGeometry(
            result.preview,
            inputPoints,
            options.polylineBulges,
            false);
        AppendPlusMarker(result.preview, inputPoints.front(), options.pickTolerance * 0.35);

        if (inputPoints.size() >= 2
            && Distance2D(inputPoints.back(), inputPoints[inputPoints.size() - 2]) <= Tolerance::Model) {
            result.prompt = "点重合，请重新指定下一点";
            return result;
        }

        const GeoPoint& last = inputPoints.back();
        if (Distance2D(last, cursorPoint) <= Tolerance::Model) {
            return result;
        }

        if (options.polylineArcMode) {
            const GeoPoint* fallback = options.arcContinue.valid
                ? &options.arcContinue.tangentDirection
                : nullptr;
            GeoPoint tangent = DrawAlgorithms::PolylineEndTangentUnit(
                inputPoints, options.polylineBulges, fallback);
            if (options.clockwise) {
                tangent = GeoPoint(-tangent.X(), -tangent.Y());
            }
            if (auto arc = DrawAlgorithms::ArcFromTangentStartEnd(last, tangent, cursorPoint)) {
                result.preview.AddArc(*arc);
            } else {
                result.preview.AddSegment(last, cursorPoint);
            }
        } else {
            result.preview.AddSegment(last, cursorPoint);
        }
        return result;
    }

    if (tool == ToolMode::DrawSolidFill || tool == ToolMode::DrawHatchFill) {
        if (!document || document->SelectedEntityIds().empty()) {
            result.prompt = "请先选择闭合图元";
            return result;
        }
        const auto regions = EditKernel::CollectRegions(*document, document->SelectedEntityIds());
        if (regions.empty()) {
            result.prompt = "所选图元无法形成填充区域";
            return result;
        }
        auto entity = std::make_unique<RegionEntity>(IdGenerator::Next(), regions.front(), false);
        entity->SetFillMode(tool == ToolMode::DrawSolidFill ? FillMode::Solid : FillMode::Hatch);
        entity->SetHatchSpacing(std::max(options.hatchSpacing, Tolerance::Model * 10.0));
        entity->SetHatchAngleRadians(options.hatchAngleRadians);
        return Complete(std::move(entity));
    }

    return result;
}

bool DrawToolEngine::IsContinuousDrawTool(ToolMode tool)
{
    return tool == ToolMode::DrawPolyline || tool == ToolMode::DrawSpline;
}

bool DrawToolEngine::IsRepeatingBasePointTool(ToolMode tool)
{
    return tool == ToolMode::DrawRay;
}

std::unique_ptr<Entity> DrawToolEngine::FinalizeContinuousDraw(
    ToolMode tool,
    const DrawToolOptions& options,
    const std::vector<GeoPoint>& inputPoints)
{
    if (inputPoints.size() < 2) {
        return nullptr;
    }
    if (tool == ToolMode::DrawPolyline) {
        return std::make_unique<PolylineEntity>(
            IdGenerator::Next(),
            GeoPolyline(inputPoints, options.polylineBulges, options.polylineClosed));
    }
    if (tool == ToolMode::DrawSpline) {
        const SplineMode mode = options.method == DrawMethod::SplineFitPoints
            ? SplineMode::FitPoints
            : SplineMode::ControlPoints;
        return std::make_unique<SplineEntity>(IdGenerator::Next(), GeoSpline(inputPoints, mode));
    }
    return nullptr;
}

bool DrawToolEngine::ArcMethodUsesNumericParameter(DrawMethod method)
{
    switch (method) {
    case DrawMethod::ArcStartEndRadius:
        return true;
    default:
        return false;
    }
}

void DrawToolEngine::UpdateArcContinueInfo(const Entity& entity, ArcContinueInfo& info)
{
    info.valid = false;
    switch (entity.Type()) {
    case EntityType::Line: {
        const auto& lineEntity = static_cast<const LineEntity&>(entity);
        const GeoLine& line = lineEntity.Line();
        info.start = line.End();
        info.tangentDirection = Normalize(GeoPoint(line.End().X() - line.Start().X(), line.End().Y() - line.Start().Y()));
        info.valid = true;
        break;
    }
    case EntityType::Arc: {
        const auto& arcEntity = static_cast<const ArcEntity&>(entity);
        const GeoArc& arc = arcEntity.Arc();
        info.start = arcEntity.EndPoint();
        const GeoPoint radial = Normalize(GeoPoint(
            info.start.X() - arc.Center().X(),
            info.start.Y() - arc.Center().Y()));
        const double span = arc.EndAngleRadians() - arc.StartAngleRadians();
        const bool ccw = std::abs(span) <= Tolerance::Angular || span > 0.0;
        info.tangentDirection = ccw ? GeoPoint(-radial.Y(), radial.X()) : GeoPoint(radial.Y(), -radial.X());
        info.valid = true;
        break;
    }
    case EntityType::Polyline: {
        const auto& polylineEntity = static_cast<const PolylineEntity&>(entity);
        const std::vector<GeoPoint>& points = polylineEntity.Polyline().Points();
        if (points.size() >= 2) {
            info.start = points.back();
            info.tangentDirection = DrawAlgorithms::PolylineEndTangentUnit(
                points, polylineEntity.Polyline().Bulges(), nullptr);
            info.valid = true;
        }
        break;
    }
    default:
        break;
    }
}

} // namespace cad
