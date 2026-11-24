#include "View/ObjectSnapUtil.h"

#include "Base/CadTypes.h"
#include "Base/Tolerance.h"
#include "DataModel/CadDocument.h"
#include "DataModel/Entity.h"
#include "Geometry/DrawAlgorithms.h"
#include "Geometry/GeoArc.h"
#include "Geometry/GeoCircle.h"
#include "Geometry/GeoLine.h"
#include "Geometry/GeoPoint.h"
#include "Geometry/GeoShape.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace cad {

namespace {

double DistancePointToSegment(const GeoPoint& point, const GeoPoint& start, const GeoPoint& end)
{
    const double vx = end.X() - start.X();
    const double vy = end.Y() - start.Y();
    const double wx = point.X() - start.X();
    const double wy = point.Y() - start.Y();
    const double len2 = vx * vx + vy * vy;
    if (len2 <= Tolerance::Model * Tolerance::Model) {
        return Distance2D(point, start);
    }
    const double t = std::clamp((wx * vx + wy * vy) / len2, 0.0, 1.0);
    return Distance2D(point, GeoPoint(start.X() + t * vx, start.Y() + t * vy));
}

GeoPoint ClosestPointOnSegment(const GeoPoint& point, const GeoPoint& start, const GeoPoint& end)
{
    const double vx = end.X() - start.X();
    const double vy = end.Y() - start.Y();
    const double wx = point.X() - start.X();
    const double wy = point.Y() - start.Y();
    const double len2 = vx * vx + vy * vy;
    if (len2 <= Tolerance::Model * Tolerance::Model) {
        return start;
    }
    const double t = std::clamp((wx * vx + wy * vy) / len2, 0.0, 1.0);
    return GeoPoint(start.X() + t * vx, start.Y() + t * vy);
}

GeoPoint ClosestPointOnInfiniteLine(const GeoPoint& point, const GeoPoint& start, const GeoPoint& end)
{
    const double vx = end.X() - start.X();
    const double vy = end.Y() - start.Y();
    const double len2 = vx * vx + vy * vy;
    if (len2 <= Tolerance::Model * Tolerance::Model) {
        return start;
    }
    const double t = ((point.X() - start.X()) * vx + (point.Y() - start.Y()) * vy) / len2;
    return GeoPoint(start.X() + t * vx, start.Y() + t * vy);
}

double ParameterOnSegment(const GeoPoint& point, const GeoPoint& start, const GeoPoint& end)
{
    const double vx = end.X() - start.X();
    const double vy = end.Y() - start.Y();
    const double len2 = vx * vx + vy * vy;
    if (len2 <= Tolerance::Model * Tolerance::Model) {
        return 0.0;
    }
    return ((point.X() - start.X()) * vx + (point.Y() - start.Y()) * vy) / len2;
}

bool IsOnSegment(const GeoPoint& point, const GeoPoint& start, const GeoPoint& end, double tolerance)
{
    if (DistancePointToSegment(point, start, end) > tolerance) {
        return false;
    }
    const double t = ParameterOnSegment(point, start, end);
    return t >= -Tolerance::Model && t <= 1.0 + Tolerance::Model;
}

std::optional<GeoPoint> InfiniteLineIntersection(const GeoLine& a, const GeoLine& b)
{
    const double x1 = a.Start().X();
    const double y1 = a.Start().Y();
    const double x2 = a.End().X();
    const double y2 = a.End().Y();
    const double x3 = b.Start().X();
    const double y3 = b.Start().Y();
    const double x4 = b.End().X();
    const double y4 = b.End().Y();
    const double denom = (x1 - x2) * (y3 - y4) - (y1 - y2) * (x3 - x4);
    if (std::abs(denom) <= Tolerance::Model) {
        return std::nullopt;
    }
    const double px = ((x1 * y2 - y1 * x2) * (x3 - x4) - (x1 - x2) * (x3 * y4 - y3 * x4)) / denom;
    const double py = ((x1 * y2 - y1 * x2) * (y3 - y4) - (y1 - y2) * (x3 * y4 - y3 * x4)) / denom;
    return GeoPoint(px, py);
}

constexpr double kTwoPi = 2.0 * 3.14159265358979323846;

double NormalizeAnglePositive(double angle)
{
    while (angle < 0.0) {
        angle += kTwoPi;
    }
    while (angle >= kTwoPi) {
        angle -= kTwoPi;
    }
    return angle;
}

double SignedArcSpan(const GeoArc& arc)
{
    return arc.EndAngleRadians() - arc.StartAngleRadians();
}

double PositiveArcSpan(double start, double end)
{
    const double span = end - start;
    return span >= 0.0 ? span : span + kTwoPi;
}

bool IsAngleOnArcSpan(const GeoArc& arc, double angle)
{
    const double start = arc.StartAngleRadians();
    const double span = SignedArcSpan(arc);
    if (std::abs(span) <= Tolerance::Angular) {
        return false;
    }

    if (span > 0.0) {
        // 逆时针弧：end > start
        const double relative = NormalizeAnglePositive(angle - start);
        return relative <= span + Tolerance::Angular;
    }

    // 顺时针弧：end < start（三点画弧常见），旧逻辑会把短弧误判为 270° 大弧。
    double relative = angle - start;
    while (relative > 0.0) {
        relative -= kTwoPi;
    }
    while (relative <= -kTwoPi) {
        relative += kTwoPi;
    }
    return relative >= span - Tolerance::Angular;
}

// Returns 0 at arc start and 1 at arc end when angle lies on the arc span.
std::optional<double> ArcNormalizedParameter(const GeoArc& arc, double angle)
{
    if (!IsAngleOnArcSpan(arc, angle)) {
        return std::nullopt;
    }
    const double start = arc.StartAngleRadians();
    const double span = SignedArcSpan(arc);
    if (std::abs(span) <= Tolerance::Angular) {
        return std::nullopt;
    }
    if (span > 0.0) {
        const double relative = NormalizeAnglePositive(angle - start);
        return std::clamp(relative / span, 0.0, 1.0);
    }
    double relative = angle - start;
    while (relative > 0.0) {
        relative -= kTwoPi;
    }
    while (relative <= -kTwoPi) {
        relative += kTwoPi;
    }
    return std::clamp(relative / span, 0.0, 1.0);
}

GeoPoint ArcPointAt(const GeoArc& arc, double angleRadians)
{
    return GeoPoint(
        arc.Center().X() + std::cos(angleRadians) * arc.Radius(),
        arc.Center().Y() + std::sin(angleRadians) * arc.Radius());
}

double DistancePointToArcCurve(const GeoPoint& point, const GeoArc& arc)
{
    const GeoPoint& center = arc.Center();
    const double dx = point.X() - center.X();
    const double dy = point.Y() - center.Y();
    const double distToCenter = std::hypot(dx, dy);
    if (distToCenter <= Tolerance::Model) {
        return arc.Radius();
    }
    const double angle = std::atan2(dy, dx);
    if (!IsAngleOnArcSpan(arc, angle)) {
        const GeoPoint start = ArcPointAt(arc, arc.StartAngleRadians());
        const GeoPoint end = ArcPointAt(arc, arc.EndAngleRadians());
        return std::min(Distance2D(point, start), Distance2D(point, end));
    }
    return std::abs(distToCenter - arc.Radius());
}

GeoPoint ClosestPointOnArcCurve(const GeoPoint& point, const GeoArc& arc)
{
    const GeoPoint& center = arc.Center();
    const double dx = point.X() - center.X();
    const double dy = point.Y() - center.Y();
    const double distToCenter = std::hypot(dx, dy);
    if (distToCenter <= Tolerance::Model) {
        return ArcPointAt(arc, arc.StartAngleRadians());
    }
    const double angle = std::atan2(dy, dx);
    if (IsAngleOnArcSpan(arc, angle)) {
        return GeoPoint(
            center.X() + dx / distToCenter * arc.Radius(),
            center.Y() + dy / distToCenter * arc.Radius());
    }
    const GeoPoint start = ArcPointAt(arc, arc.StartAngleRadians());
    const GeoPoint end = ArcPointAt(arc, arc.EndAngleRadians());
    return Distance2D(point, start) <= Distance2D(point, end) ? start : end;
}

// Arc-length parameter [0,1] from the closest point on the arc curve (zoom-independent zones).
std::optional<double> ArcNormalizedParameterAtPoint(const GeoArc& arc, const GeoPoint& point)
{
    const GeoPoint onArc = ClosestPointOnArcCurve(point, arc);
    const GeoPoint& center = arc.Center();
    const double cdx = onArc.X() - center.X();
    const double cdy = onArc.Y() - center.Y();
    if (std::hypot(cdx, cdy) <= Tolerance::Model) {
        return std::nullopt;
    }
    return ArcNormalizedParameter(arc, std::atan2(cdy, cdx));
}

bool CursorApertureOverlapsArc(const GeoPoint& cursor, const GeoArc& arc, double aperture)
{
    if (DistancePointToArcCurve(cursor, arc) <= aperture) {
        return true;
    }
    const GeoPoint closest = ClosestPointOnArcCurve(cursor, arc);
    return std::max(
               std::abs(cursor.X() - closest.X()),
               std::abs(cursor.Y() - closest.Y()))
        <= aperture;
}

GeoBounds ModelRectToBounds(const QRectF& rect)
{
    return GeoBounds{rect.left(), rect.top(), rect.right(), rect.bottom(), true};
}

bool EntityIntersectsVisibleRect(const Entity& entity, const QRectF& visibleModelRect, double tolerance)
{
    const GeoShape shape = entity.Shaper();
    GeoBounds clip = ModelRectToBounds(visibleModelRect);
    if (!shape.HasInfiniteLines()) {
        const GeoBounds bounds = shape.Bounds();
        if (bounds.valid) {
            clip = bounds;
        }
    }

    for (const GeoLine& segment : shape.ToSegments(clip)) {
        const QRectF segmentRect(
            std::min(segment.Start().X(), segment.End().X()),
            std::min(segment.Start().Y(), segment.End().Y()),
            std::max(std::abs(segment.End().X() - segment.Start().X()), Tolerance::Model),
            std::max(std::abs(segment.End().Y() - segment.Start().Y()), Tolerance::Model));
        if (segmentRect.normalized().adjusted(-tolerance, -tolerance, tolerance, tolerance).intersects(visibleModelRect)) {
            return true;
        }
    }

    const GeoBounds bounds = entity.Shaper().Bounds();
    if (!bounds.valid) {
        for (const GripInfo& grip : entity.Grips()) {
            const QRectF pointRect(grip.position.X(), grip.position.Y(), 1.0, 1.0);
            if (pointRect.normalized().adjusted(-tolerance, -tolerance, tolerance, tolerance).intersects(visibleModelRect)) {
                return true;
            }
        }
        return true;
    }
    const QRectF entityRect(bounds.minX, bounds.minY, bounds.maxX - bounds.minX, bounds.maxY - bounds.minY);
    return entityRect.normalized().adjusted(-tolerance, -tolerance, tolerance, tolerance).intersects(visibleModelRect);
}

struct SnapSegment {
    GeoPoint start;
    GeoPoint end;
    std::size_t sourceEntityIndex = static_cast<std::size_t>(-1);
};

// 仅收集“真实线性段”用于交点/平行捕捉：直线段、多段线边。
// 圆弧/圆/椭圆的离散段 excluded —— 相邻离散段在共享顶点处的延长线会相交，
// 会在曲线上每个离散点产生伪交点（黄色 X）。
// 构造线（射线/双向线）因需裁剪逻辑，此处暂不参与交点捕捉。
std::vector<SnapSegment> CollectLinearSegmentsFromShape(const GeoShape& shape, const GeoBounds& /*clip*/)
{
    std::vector<SnapSegment> segments;

    for (const GeoLine& segment : shape.Segments()) {
        segments.push_back({segment.Start(), segment.End()});
    }

    for (const auto& polyline : shape.Polylines()) {
        for (std::size_t i = 1; i < polyline.size(); ++i) {
            segments.push_back({polyline[i - 1], polyline[i]});
        }
    }

    return segments;
}

class SnapEvaluator {
public:
    SnapEvaluator(const GeoPoint& cursor, double aperture)
        : cursor_(cursor)
        , aperture_(aperture)
    {
    }

    const GeoPoint& cursor() const { return cursor_; }
    double aperture() const { return aperture_; }

    double SquareDistanceToPoint(const GeoPoint& point) const
    {
        return std::max(
            std::abs(cursor_.X() - point.X()),
            std::abs(cursor_.Y() - point.Y()));
    }

    void tryAcquire(const GeoPoint& snapPoint, ObjectSnapKind kind, double acquisitionDistance)
    {
        tryAcquireWithin(snapPoint, kind, acquisitionDistance, aperture_);
    }

    void tryAcquireWithin(
        const GeoPoint& snapPoint,
        ObjectSnapKind kind,
        double acquisitionDistance,
        double maxDistance)
    {
        if (acquisitionDistance > maxDistance) {
            return;
        }
        const double rank = acquisitionDistance + KindRankBias(kind);
        if (rank < bestRank_) {
            bestRank_ = rank;
            bestPoint_ = snapPoint;
            bestKind_ = kind;
        }
    }

    void trySnap(const GeoPoint& candidate, bool eligible, ObjectSnapKind kind)
    {
        if (!eligible) {
            return;
        }
        tryAcquire(candidate, kind, Distance2D(cursor_, candidate));
    }

    void tryDirect(const GeoPoint& candidate, ObjectSnapKind kind)
    {
        tryAcquire(candidate, kind, SquareDistanceToPoint(candidate));
    }

    void tryAcquireCenterOnArc(const GeoPoint& center)
    {
        // AutoCAD：光标在弧上时圆心捕捉优先于最近点/中点等，标记显示在圆心。
        const double rank = KindRankBias(ObjectSnapKind::Center);
        if (rank < bestRank_) {
            bestRank_ = rank;
            bestPoint_ = center;
            bestKind_ = ObjectSnapKind::Center;
        }
    }

    void tryAcquireEndpointOnArc(const GeoPoint& endpoint)
    {
        // AutoCAD: endpoint wins over center when cursor is in the 25% arc-length end zones.
        const double rank = KindRankBias(ObjectSnapKind::Endpoint);
        if (rank < bestRank_) {
            bestRank_ = rank;
            bestPoint_ = endpoint;
            bestKind_ = ObjectSnapKind::Endpoint;
        }
    }

    bool isNearSegment(const GeoPoint& start, const GeoPoint& end) const
    {
        return DistancePointToSegment(cursor_, start, end) <= aperture_;
    }

    bool isNearPoint(const GeoPoint& candidate) const
    {
        return SquareDistanceToPoint(candidate) <= aperture_;
    }

    std::optional<ObjectSnapHit> result() const
    {
        if (!bestPoint_) {
            return std::nullopt;
        }
        return ObjectSnapHit{*bestPoint_, bestKind_};
    }

private:
    double KindRankBias(ObjectSnapKind kind) const
    {
        switch (kind) {
        case ObjectSnapKind::Endpoint:
            return 0.0;
        case ObjectSnapKind::Center:
            return aperture_ * 1.0e-3;
        case ObjectSnapKind::Intersection:
        case ObjectSnapKind::ApparentIntersection:
            return aperture_ * 2.0e-3;
        case ObjectSnapKind::Midpoint:
            return aperture_ * 3.0e-3;
        case ObjectSnapKind::Extension:
            return aperture_ * 4.0e-3;
        case ObjectSnapKind::Nearest:
            return aperture_ * 1.0e-2;
        default:
            return aperture_ * 5.0e-3;
        }
    }

    const GeoPoint& cursor_;
    double aperture_;
    double bestRank_ = std::numeric_limits<double>::infinity();
    std::optional<GeoPoint> bestPoint_;
    ObjectSnapKind bestKind_ = ObjectSnapKind::Unknown;
};

void ProcessArcForSnap(
    const GeoArc& arc,
    const ObjectSnapModes& modes,
    SnapEvaluator& evaluator,
    const GeoPoint* perpendicularFrom)
{
    const GeoPoint& cursor = evaluator.cursor();
    const GeoPoint& center = arc.Center();
    const double radius = arc.Radius();
    if (radius <= Tolerance::Model) {
        return;
    }

    const GeoPoint start = ArcPointAt(arc, arc.StartAngleRadians());
    const GeoPoint end = ArcPointAt(arc, arc.EndAngleRadians());

    const double dx = cursor.X() - center.X();
    const double dy = cursor.Y() - center.Y();
    const double distToCenter = std::hypot(dx, dy);
    const double radialGap = std::abs(distToCenter - radius);
    const double angle = distToCenter > Tolerance::Model ? std::atan2(dy, dx) : 0.0;
    const bool onArcSpan = distToCenter <= Tolerance::Model || IsAngleOnArcSpan(arc, angle);
    // "On the arc" uses the screen aperture box; 25% endpoint zones use arc-length
    // parameter from the closest point on the curve (independent of zoom).
    const bool nearArcCurve = CursorApertureOverlapsArc(cursor, arc, evaluator.aperture());

    bool inStartEndpointZone = false;
    bool inEndEndpointZone = false;
    if (modes.endpoint && nearArcCurve && distToCenter > Tolerance::Model) {
        if (const std::optional<double> param = ArcNormalizedParameterAtPoint(arc, cursor)) {
            // Left/right 25% of arc length map to the two endpoints.
            inStartEndpointZone = *param <= 0.25;
            inEndEndpointZone = *param >= 0.75;
        }
    }

    if (modes.endpoint) {
        if (inStartEndpointZone) {
            evaluator.tryAcquireEndpointOnArc(start);
        }
        if (inEndEndpointZone) {
            evaluator.tryAcquireEndpointOnArc(end);
        }
    }

    // Mid-arc (outside the 25% end zones): prefer center, not endpoint.
    const bool onArcBody = nearArcCurve && !inStartEndpointZone && !inEndEndpointZone;

    if (modes.center) {
        if (onArcBody) {
            evaluator.tryAcquireCenterOnArc(center);
        }
        const double centerAcquire = evaluator.SquareDistanceToPoint(center);
        if (centerAcquire <= evaluator.aperture() && !inStartEndpointZone && !inEndEndpointZone) {
            evaluator.tryAcquireWithin(center, ObjectSnapKind::Center, centerAcquire, evaluator.aperture());
        }
    }

    if (modes.nearest && distToCenter > Tolerance::Model && onArcSpan && !onArcBody && !inStartEndpointZone && !inEndEndpointZone) {
        evaluator.tryAcquire(
            ClosestPointOnArcCurve(cursor, arc),
            ObjectSnapKind::Nearest,
            radialGap);
    }

    if (modes.quadrant) {
        constexpr double kQuarterTurn = 1.5707963267948966;
        for (int i = 0; i < 4; ++i) {
            const double quadrantAngle = i * kQuarterTurn;
            if (!IsAngleOnArcSpan(arc, quadrantAngle)) {
                continue;
            }
            const GeoPoint quadrantPoint = ArcPointAt(arc, quadrantAngle);
            const double acquire = evaluator.SquareDistanceToPoint(quadrantPoint);
            if (acquire <= evaluator.aperture() || (radialGap <= evaluator.aperture() && onArcSpan)) {
                evaluator.tryAcquire(quadrantPoint, ObjectSnapKind::Quadrant, std::min(acquire, radialGap));
            }
        }
    }

    if (modes.tangent && perpendicularFrom != nullptr) {
        const double fromDx = perpendicularFrom->X() - center.X();
        const double fromDy = perpendicularFrom->Y() - center.Y();
        const double fromDist = std::hypot(fromDx, fromDy);
        if (fromDist > radius + Tolerance::Model) {
            const double baseAngle = std::atan2(fromDy, fromDx);
            const double spread = std::acos(std::clamp(radius / fromDist, -1.0, 1.0));
            for (const double offset : {-spread, spread}) {
                const double tangentAngle = baseAngle + offset;
                if (!IsAngleOnArcSpan(arc, tangentAngle)) {
                    continue;
                }
                const GeoPoint tangentPoint = ArcPointAt(arc, tangentAngle);
                const double acquire = evaluator.SquareDistanceToPoint(tangentPoint);
                if (acquire <= evaluator.aperture() || (radialGap <= evaluator.aperture() && onArcSpan)) {
                    evaluator.tryAcquire(tangentPoint, ObjectSnapKind::Tangent, std::min(acquire, radialGap));
                }
            }
        }
    }
}

void ProcessCircleForSnap(
    const GeoCircle& circle,
    const ObjectSnapModes& modes,
    SnapEvaluator& evaluator,
    const GeoPoint* perpendicularFrom)
{
    const GeoPoint& center = circle.Center();
    const double dx = evaluator.cursor().X() - center.X();
    const double dy = evaluator.cursor().Y() - center.Y();
    const double distToCenter = std::hypot(dx, dy);
    const double radialGap = std::abs(distToCenter - circle.Radius());

    if (modes.center) {
        if (radialGap <= evaluator.aperture()) {
            evaluator.tryAcquire(center, ObjectSnapKind::Center, radialGap);
        }
        const double centerAcquire = evaluator.SquareDistanceToPoint(center);
        if (centerAcquire <= evaluator.aperture()) {
            evaluator.tryAcquire(center, ObjectSnapKind::Center, centerAcquire);
        }
    }

    if (modes.nearest && distToCenter > Tolerance::Model) {
        const double radius = circle.Radius();
        evaluator.tryAcquire(
            GeoPoint(center.X() + dx / distToCenter * radius, center.Y() + dy / distToCenter * radius),
            ObjectSnapKind::Nearest,
            radialGap);
    }

    if (modes.quadrant) {
        constexpr double kQuarterTurn = 1.5707963267948966;
        for (int i = 0; i < 4; ++i) {
            const double angle = i * kQuarterTurn;
            const GeoPoint quadrantPoint(
                center.X() + std::cos(angle) * circle.Radius(),
                center.Y() + std::sin(angle) * circle.Radius());
            const double acquire = evaluator.SquareDistanceToPoint(quadrantPoint);
            if (acquire <= evaluator.aperture() || radialGap <= evaluator.aperture()) {
                evaluator.tryAcquire(quadrantPoint, ObjectSnapKind::Quadrant, std::min(acquire, radialGap));
            }
        }
    }

    if (modes.tangent && perpendicularFrom != nullptr) {
        const double fromDx = perpendicularFrom->X() - center.X();
        const double fromDy = perpendicularFrom->Y() - center.Y();
        const double fromDist = std::hypot(fromDx, fromDy);
        if (fromDist > circle.Radius() + Tolerance::Model) {
            const double baseAngle = std::atan2(fromDy, fromDx);
            const double spread = std::acos(std::clamp(circle.Radius() / fromDist, -1.0, 1.0));
            for (const double offset : {-spread, spread}) {
                const double tangentAngle = baseAngle + offset;
                const GeoPoint tangentPoint(
                    center.X() + std::cos(tangentAngle) * circle.Radius(),
                    center.Y() + std::sin(tangentAngle) * circle.Radius());
                const double acquire = evaluator.SquareDistanceToPoint(tangentPoint);
                if (acquire <= evaluator.aperture() || radialGap <= evaluator.aperture()) {
                    evaluator.tryAcquire(tangentPoint, ObjectSnapKind::Tangent, std::min(acquire, radialGap));
                }
            }
        }
    }
}

void ProcessShapeForSnap(
    const GeoShape& shape,
    const ObjectSnapModes& modes,
    SnapEvaluator& evaluator,
    const GeoBounds& clip,
    const GeoPoint* perpendicularFrom,
    std::vector<SnapSegment>* allSegments,
    bool collectSegments,
    std::size_t sourceEntityIndex)
{
    const bool hasCurvedGeometry = !shape.Arcs().empty() || !shape.Circles().empty();

    if (collectSegments && allSegments != nullptr) {
        const std::vector<SnapSegment> shapeSegments = CollectLinearSegmentsFromShape(shape, clip);
        for (const SnapSegment& segment : shapeSegments) {
            allSegments->push_back({segment.start, segment.end, sourceEntityIndex});
        }
    }

    if ((modes.endpoint || modes.nearest) && !hasCurvedGeometry) {
        for (const GeoPoint& shapePoint : shape.Points()) {
            if (modes.endpoint) {
                evaluator.tryDirect(shapePoint, ObjectSnapKind::Endpoint);
            }
            if (modes.nearest) {
                evaluator.tryDirect(shapePoint, ObjectSnapKind::Nearest);
            }
        }
        for (const auto& polyline : shape.Polylines()) {
            for (const GeoPoint& vertex : polyline) {
                if (modes.endpoint) {
                    evaluator.tryDirect(vertex, ObjectSnapKind::Endpoint);
                }
                if (modes.nearest) {
                    evaluator.tryDirect(vertex, ObjectSnapKind::Nearest);
                }
            }
        }
    }

    if (modes.geometricCenter && !hasCurvedGeometry) {
        const GeoBounds bounds = shape.Bounds();
        if (bounds.valid) {
            const GeoPoint center(
                (bounds.minX + bounds.maxX) * 0.5,
                (bounds.minY + bounds.maxY) * 0.5);
            const bool nearShape = evaluator.isNearSegment(
                GeoPoint(bounds.minX, bounds.minY),
                GeoPoint(bounds.maxX, bounds.maxY))
                || evaluator.isNearSegment(
                    GeoPoint(bounds.minX, bounds.minY),
                    GeoPoint(bounds.minX, bounds.maxY));
            evaluator.trySnap(center, nearShape || evaluator.isNearPoint(center), ObjectSnapKind::GeometricCenter);
        }
    }

    // Arc/circle geometry is tessellated into short segments for hit testing.
    // Do not treat those segment endpoints as END/NEA/MID snaps or the cursor
    // will stick to arbitrary points on the curve instead of the real center/endpoints.
    const bool skipTessellatedSegmentSnaps =
        !shape.Arcs().empty() || !shape.Circles().empty() || !shape.Splines().empty();

    for (const GeoLine& segment : shape.ToSegments(clip)) {
        const GeoPoint& start = segment.Start();
        const GeoPoint& end = segment.End();
        const bool nearSegment = evaluator.isNearSegment(start, end);
        const GeoPoint midpoint(
            (start.X() + end.X()) * 0.5,
            (start.Y() + end.Y()) * 0.5);

        if (modes.endpoint && !skipTessellatedSegmentSnaps) {
            const double startAcquire = evaluator.SquareDistanceToPoint(start);
            const double endAcquire = evaluator.SquareDistanceToPoint(end);
            if (startAcquire <= evaluator.aperture()) {
                evaluator.tryAcquire(start, ObjectSnapKind::Endpoint, startAcquire);
            }
            if (endAcquire <= evaluator.aperture()) {
                evaluator.tryAcquire(end, ObjectSnapKind::Endpoint, endAcquire);
            }
        }

        if (modes.midpoint && !skipTessellatedSegmentSnaps) {
            evaluator.trySnap(midpoint, nearSegment || evaluator.isNearPoint(midpoint), ObjectSnapKind::Midpoint);
        }

        if (modes.nearest && !skipTessellatedSegmentSnaps) {
            evaluator.trySnap(
                ClosestPointOnSegment(evaluator.cursor(), start, end),
                nearSegment,
                ObjectSnapKind::Nearest);
        }

        if (modes.extension && !skipTessellatedSegmentSnaps) {
            const GeoPoint projected = ClosestPointOnInfiniteLine(evaluator.cursor(), start, end);
            const double t = ParameterOnSegment(projected, start, end);
            const bool onExtension = t < -Tolerance::Model || t > 1.0 + Tolerance::Model;
            const bool nearLine = DistancePointToSegment(evaluator.cursor(), start, end) <= evaluator.aperture()
                || Distance2D(evaluator.cursor(), projected) <= evaluator.aperture();
            evaluator.trySnap(projected, onExtension && nearLine, ObjectSnapKind::Extension);
        }

        if (modes.perpendicular && perpendicularFrom != nullptr && !skipTessellatedSegmentSnaps) {
            const GeoPoint foot = ClosestPointOnInfiniteLine(*perpendicularFrom, start, end);
            evaluator.trySnap(
                foot,
                IsOnSegment(foot, start, end, evaluator.aperture()) && nearSegment,
                ObjectSnapKind::Perpendicular);
        }
    }

    if (modes.center || modes.quadrant || modes.tangent || modes.nearest) {
        for (const GeoCircle& circle : shape.Circles()) {
            ProcessCircleForSnap(circle, modes, evaluator, perpendicularFrom);
        }
    }

    if (modes.center || modes.quadrant || modes.tangent || modes.nearest || modes.endpoint) {
        for (const GeoArc& arc : shape.Arcs()) {
            ProcessArcForSnap(arc, modes, evaluator, perpendicularFrom);
        }
    }

    if (modes.nearest || modes.endpoint) {
        for (const GeoSpline& spline : shape.Splines()) {
            const std::vector<GeoPoint> samples = DrawAlgorithms::SplinePreviewPoints(
                spline.Points(), spline.Mode(), spline.Degree(), spline.Knots());
            if (samples.size() < 2) {
                continue;
            }
            if (modes.endpoint) {
                evaluator.tryDirect(samples.front(), ObjectSnapKind::Endpoint);
                evaluator.tryDirect(samples.back(), ObjectSnapKind::Endpoint);
            }
            if (modes.nearest) {
                for (std::size_t i = 1; i < samples.size(); ++i) {
                    const GeoPoint& start = samples[i - 1];
                    const GeoPoint& end = samples[i];
                    const bool nearSegment = evaluator.isNearSegment(start, end);
                    evaluator.trySnap(
                        ClosestPointOnSegment(evaluator.cursor(), start, end),
                        nearSegment,
                        ObjectSnapKind::Nearest);
                }
            }
        }
    }
}

} // namespace

std::optional<ObjectSnapHit> FindBestObjectSnapPoint(
    const CadDocument& document,
    const GeoPoint& point,
    const ObjectSnapModes& modes,
    double tolerance,
    const QRectF& visibleModelRect,
    const GeoPoint* perpendicularFrom,
    const std::vector<GeoShape>* extraShapes)
{
    if (!modes.AnyEnabled()) {
        return std::nullopt;
    }

    SnapEvaluator evaluator(point, tolerance);

    std::vector<SnapSegment> allSegments;
    const bool needSegments = modes.intersection || modes.apparentIntersection || modes.parallel;
    if (needSegments) {
        allSegments.reserve(document.Entities().size() * 4);
    }

    const std::size_t entityCount = document.Entities().size();
    for (std::size_t entityIndex = 0; entityIndex < entityCount; ++entityIndex) {
        const auto& entity = document.Entities()[entityIndex];
        if (!EntityIntersectsVisibleRect(*entity, visibleModelRect, tolerance)) {
            continue;
        }

        const GeoShape shape = entity->Shaper();
        GeoBounds clip = ModelRectToBounds(visibleModelRect);
        if (!shape.HasInfiniteLines()) {
            const GeoBounds bounds = shape.Bounds();
            if (bounds.valid) {
                clip = bounds;
            }
        }

        const bool arcOrCircleEntity = entity->Type() == EntityType::Arc
            || entity->Type() == EntityType::Circle;

        if (modes.endpoint && !arcOrCircleEntity) {
            for (const GripInfo& grip : entity->Grips()) {
                if (grip.kind == GripKind::Vertex) {
                    evaluator.tryDirect(grip.position, ObjectSnapKind::Endpoint);
                }
            }
        }

        if (modes.center && !arcOrCircleEntity) {
            for (const GripInfo& grip : entity->Grips()) {
                if (grip.kind == GripKind::Center) {
                    evaluator.tryDirect(grip.position, ObjectSnapKind::Center);
                }
            }
        }

        if (modes.node && entity->Type() == EntityType::Point) {
            for (const GripInfo& grip : entity->Grips()) {
                evaluator.tryDirect(grip.position, ObjectSnapKind::Node);
            }
        }

        if (modes.insertion) {
            if (entity->Type() == EntityType::Text || entity->Type() == EntityType::MText) {
                for (const GripInfo& grip : entity->Grips()) {
                    evaluator.tryDirect(grip.position, ObjectSnapKind::Insertion);
                }
            }
        }

        ProcessShapeForSnap(
            shape,
            modes,
            evaluator,
            clip,
            perpendicularFrom,
            needSegments ? &allSegments : nullptr,
            needSegments,
            entityIndex);
    }

    if (extraShapes != nullptr) {
        const GeoBounds clip = ModelRectToBounds(visibleModelRect);
        std::size_t transientIndex = entityCount;
        for (const GeoShape& shape : *extraShapes) {
            ProcessShapeForSnap(
                shape,
                modes,
                evaluator,
                clip,
                perpendicularFrom,
                needSegments ? &allSegments : nullptr,
                needSegments,
                transientIndex++);
        }
    }

    if (modes.intersection || modes.apparentIntersection) {
        for (std::size_t i = 0; i < allSegments.size(); ++i) {
            const GeoLine lineA(allSegments[i].start, allSegments[i].end);
            for (std::size_t j = i + 1; j < allSegments.size(); ++j) {
                // 同一实体上的线段（如多段线离散边）不应产生“自交点”捕捉。
                if (allSegments[i].sourceEntityIndex == allSegments[j].sourceEntityIndex) {
                    continue;
                }
                // 相邻离散段（如圆弧/圆的离散片段、多段线相邻段）共享端点，
                // 其“交点”只是共享顶点，并非真正的交点捕捉目标，必须跳过，
                // 否则圆弧上每个离散顶点都会被误判为交点，盖过圆心捕捉。
                const bool sharesEndpoint =
                    Distance2D(allSegments[i].start, allSegments[j].start) <= tolerance
                    || Distance2D(allSegments[i].start, allSegments[j].end) <= tolerance
                    || Distance2D(allSegments[i].end, allSegments[j].start) <= tolerance
                    || Distance2D(allSegments[i].end, allSegments[j].end) <= tolerance;
                if (sharesEndpoint) {
                    continue;
                }
                const GeoLine lineB(allSegments[j].start, allSegments[j].end);
                const std::optional<GeoPoint> intersection = InfiniteLineIntersection(lineA, lineB);
                if (!intersection) {
                    continue;
                }
                const bool nearIntersection = evaluator.isNearPoint(*intersection);
                const bool nearBothSegments = evaluator.isNearSegment(allSegments[i].start, allSegments[i].end)
                    && evaluator.isNearSegment(allSegments[j].start, allSegments[j].end);
                if (modes.intersection
                    && IsOnSegment(*intersection, allSegments[i].start, allSegments[i].end, tolerance)
                    && IsOnSegment(*intersection, allSegments[j].start, allSegments[j].end, tolerance)) {
                    evaluator.trySnap(
                        *intersection,
                        nearIntersection || nearBothSegments,
                        ObjectSnapKind::Intersection);
                }
                if (modes.apparentIntersection) {
                    evaluator.trySnap(
                        *intersection,
                        nearIntersection || nearBothSegments,
                        ObjectSnapKind::ApparentIntersection);
                }
            }
        }
    }

    if (modes.parallel && perpendicularFrom != nullptr) {
        for (const SnapSegment& segment : allSegments) {
            const GeoPoint foot = ClosestPointOnInfiniteLine(*perpendicularFrom, segment.start, segment.end);
            evaluator.trySnap(
                foot,
                evaluator.isNearSegment(segment.start, segment.end),
                ObjectSnapKind::Parallel);
        }
    }

    return evaluator.result();
}

} // namespace cad
