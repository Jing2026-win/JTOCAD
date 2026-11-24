#include "Command/EditKernel.h"

#include "Base/IdGenerator.h"
#include "Base/Tolerance.h"
#include "DataModel/ArcEntity.h"
#include "DataModel/CadDocument.h"
#include "DataModel/CircleEntity.h"
#include "DataModel/ConicEntity.h"
#include "DataModel/Entity.h"
#include "DataModel/LineEntity.h"
#include "DataModel/PolylineEntity.h"
#include "DataModel/RayEntity.h"
#include "DataModel/RectangleEntity.h"
#include "DataModel/RegionEntity.h"
#include "DataModel/BlockReferenceEntity.h"
#include "Geometry/ClipperUtil.h"
#include "Geometry/DrawAlgorithms.h"
#include "Geometry/GeoShape.h"
#include "Geometry/GeoBreak.h"
#include "Geometry/GeoFilletChamfer.h"
#include "Geometry/GeoPolyline.h"
#include "Geometry/GeoTransform.h"
#include "Geometry/GeoTrimExtend.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>

namespace cad {

namespace {

constexpr double kCutLineExtension = 1.0e6;

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

std::optional<GeoLine> LineFromDirection(const GeoPoint& anchor, const GeoPoint& through, bool bidirectional)
{
    const double dx = through.X() - anchor.X();
    const double dy = through.Y() - anchor.Y();
    const double len = std::hypot(dx, dy);
    if (len <= Tolerance::Model) {
        return std::nullopt;
    }
    const double ux = dx / len;
    const double uy = dy / len;
    if (bidirectional) {
        return GeoLine(
            GeoPoint(anchor.X() - ux * kCutLineExtension, anchor.Y() - uy * kCutLineExtension),
            GeoPoint(anchor.X() + ux * kCutLineExtension, anchor.Y() + uy * kCutLineExtension));
    }
    return GeoLine(anchor, GeoPoint(anchor.X() + ux * kCutLineExtension, anchor.Y() + uy * kCutLineExtension));
}

std::optional<GeoLine> NearestPolylineSegment(
    const std::vector<GeoPoint>& points,
    const GeoPoint& nearPoint,
    bool closed = false)
{
    if (points.size() < 2) {
        return std::nullopt;
    }
    const std::size_t count = points.size();
    const std::size_t segments = closed ? count : count - 1;
    double bestDistance = std::numeric_limits<double>::max();
    std::optional<GeoLine> bestLine;
    for (std::size_t i = 0; i < segments; ++i) {
        const GeoLine segment(points[i], points[(i + 1) % count]);
        const double distance = DistancePointToSegment(nearPoint, segment.Start(), segment.End());
        if (distance < bestDistance) {
            bestDistance = distance;
            bestLine = segment;
        }
    }
    return bestLine;
}

GeoRegion RegionFromEntity(const Entity& entity)
{
    if (entity.Type() == EntityType::Region) {
        return static_cast<const RegionEntity&>(entity).Region();
    }
    if (entity.Type() == EntityType::Circle) {
        const auto& circle = static_cast<const CircleEntity&>(entity).Circle();
        return ClipperUtil::CircleToRegion(circle.Center(), circle.Radius());
    }
    if (entity.Type() == EntityType::Rectangle) {
        const auto& rect = static_cast<const RectangleEntity&>(entity).Rectangle();
        return ClipperUtil::PolylineToRegion(DrawAlgorithms::RectanglePoints(rect.Corner1(), rect.Corner2()));
    }
    if (entity.Type() == EntityType::Polygon) {
        const auto& polygon = static_cast<const PolygonEntity&>(entity).Polygon();
        return ClipperUtil::PolylineToRegion(DrawAlgorithms::RegularPolygonPoints(
            polygon.Center(),
            polygon.Radius(),
            polygon.Sides(),
            polygon.RotationRadians()));
    }
    if (entity.Type() == EntityType::Polyline) {
        const auto& polyline = static_cast<const PolylineEntity&>(entity).Polyline();
        std::vector<GeoPoint> points = polyline.Points();
        if (polyline.Closed() && points.size() >= 2
            && Distance2D(points.front(), points.back()) > Tolerance::Model) {
            points.push_back(points.front());
        }
        if (points.size() >= 3) {
            return ClipperUtil::PolylineToRegion(points);
        }
    }
    if (entity.Type() == EntityType::Arc) {
        const auto& arc = static_cast<const ArcEntity&>(entity).Arc();
        GeoShape shape;
        shape.AddArc(arc);
        const GeoBounds bounds = shape.Bounds();
        const std::vector<GeoLine> segments = shape.ToSegments(bounds);
        std::vector<GeoPoint> points;
        for (const GeoLine& segment : segments) {
            if (points.empty() || Distance2D(points.back(), segment.Start()) > Tolerance::Model) {
                points.push_back(segment.Start());
            }
            points.push_back(segment.End());
        }
        if (points.size() >= 3) {
            return ClipperUtil::PolylineToRegion(points);
        }
    }
    if (entity.Type() == EntityType::Ellipse) {
        const GeoShapeEllipse shape = static_cast<const EllipseEntity&>(entity).Ellipse().Shape();
        GeoShape geoShape;
        geoShape.AddEllipse(shape);
        const std::vector<GeoLine> segments = geoShape.ToSegments(geoShape.Bounds());
        std::vector<GeoPoint> points;
        for (const GeoLine& segment : segments) {
            if (points.empty() || Distance2D(points.back(), segment.Start()) > Tolerance::Model) {
                points.push_back(segment.Start());
            }
            points.push_back(segment.End());
        }
        if (points.size() >= 3) {
            return ClipperUtil::PolylineToRegion(points);
        }
    }
    return {};
}

void CopyEntityProperties(const Entity& source, Entity& target)
{
    target.SetLayerName(source.LayerName());
    target.SetStyle(source.Style());
}

constexpr double kArrayPi = 3.14159265358979323846;

void AppendArcSamples(
    std::vector<GeoPoint>& points,
    const GeoArc& arc,
    int segments,
    bool includeFirst = true)
{
    segments = std::max(2, segments);
    const double start = arc.StartAngleRadians();
    const double sweep = arc.EndAngleRadians() - start;
    for (int i = includeFirst ? 0 : 1; i <= segments; ++i) {
        const double t = static_cast<double>(i) / static_cast<double>(segments);
        const double angle = start + sweep * t;
        points.emplace_back(
            arc.Center().X() + std::cos(angle) * arc.Radius(),
            arc.Center().Y() + std::sin(angle) * arc.Radius());
    }
}

std::vector<GeoPoint> ArrayPathPoints(const Entity& entity, bool* closedOut)
{
    bool closed = false;
    std::vector<GeoPoint> points;
    switch (entity.Type()) {
    case EntityType::Line: {
        const GeoLine& line = static_cast<const LineEntity&>(entity).Line();
        points = {line.Start(), line.End()};
        break;
    }
    case EntityType::Arc:
        AppendArcSamples(points, static_cast<const ArcEntity&>(entity).Arc(), 128);
        break;
    case EntityType::Circle: {
        const GeoCircle& circle = static_cast<const CircleEntity&>(entity).Circle();
        AppendArcSamples(
            points,
            GeoArc(circle.Center(), circle.Radius(), 0.0, 2.0 * kArrayPi),
            256);
        closed = true;
        break;
    }
    case EntityType::Ellipse: {
        const auto shape = static_cast<const EllipseEntity&>(entity).Ellipse().Shape();
        GeoShape helper;
        points = helper.DiscretizeEllipse(shape, 256);
        if (!points.empty()) {
            points.push_back(points.front());
        }
        closed = true;
        break;
    }
    case EntityType::Rectangle: {
        const auto& rect = static_cast<const RectangleEntity&>(entity).Rectangle();
        points = DrawAlgorithms::RectanglePoints(rect.Corner1(), rect.Corner2());
        if (!points.empty()) {
            points.push_back(points.front());
        }
        closed = true;
        break;
    }
    case EntityType::Polygon: {
        const auto& polygon = static_cast<const PolygonEntity&>(entity).Polygon();
        points = DrawAlgorithms::RegularPolygonPoints(
            polygon.Center(), polygon.Radius(), polygon.Sides(), polygon.RotationRadians());
        if (!points.empty()) {
            points.push_back(points.front());
        }
        closed = true;
        break;
    }
    case EntityType::Polyline: {
        const GeoPolyline& polyline = static_cast<const PolylineEntity&>(entity).Polyline();
        const auto& vertices = polyline.Points();
        const std::size_t segmentCount = polyline.SegmentCount();
        for (std::size_t i = 0; i < segmentCount; ++i) {
            const GeoPoint& start = vertices[i];
            const GeoPoint& end = vertices[(i + 1) % vertices.size()];
            const double bulge = polyline.SegmentBulge(i);
            if (std::abs(bulge) <= 1.0e-12) {
                if (points.empty()) {
                    points.push_back(start);
                }
                points.push_back(end);
            } else if (const auto arc = DrawAlgorithms::ArcFromBulge(start, end, bulge)) {
                const double sweep = std::abs(arc->EndAngleRadians() - arc->StartAngleRadians());
                const int segments = std::max(8, static_cast<int>(std::ceil(sweep * 32.0)));
                AppendArcSamples(points, *arc, segments, points.empty());
            }
        }
        closed = polyline.Closed();
        break;
    }
    case EntityType::Spline: {
        const GeoSpline& spline = static_cast<const SplineEntity&>(entity).Spline();
        points = DrawAlgorithms::SplinePreviewPoints(
            spline.Points(), spline.Mode(), spline.Degree(), spline.Knots());
        break;
    }
    default: {
        const GeoShape shape = entity.Shaper();
        if (!shape.Polylines().empty()) {
            points = shape.Polylines().front();
            closed = !shape.PolylineClosed().empty() && shape.PolylineClosed().front();
            if (closed && !points.empty()) {
                points.push_back(points.front());
            }
        }
        break;
    }
    }
    if (closedOut) {
        *closedOut = closed;
    }
    return points;
}

struct ArrayPathMeasure {
    std::vector<GeoPoint> points;
    std::vector<double> cumulative;
    double length = 0.0;
    bool closed = false;
};

ArrayPathMeasure MeasureArrayPath(const Entity& entity)
{
    ArrayPathMeasure result;
    result.points = ArrayPathPoints(entity, &result.closed);
    if (result.points.size() < 2) {
        result.points.clear();
        return result;
    }
    result.cumulative.reserve(result.points.size());
    result.cumulative.push_back(0.0);
    for (std::size_t i = 1; i < result.points.size(); ++i) {
        result.length += Distance2D(result.points[i - 1], result.points[i]);
        result.cumulative.push_back(result.length);
    }
    if (result.length <= Tolerance::Model) {
        result.points.clear();
        result.cumulative.clear();
        result.length = 0.0;
    }
    return result;
}

std::pair<GeoPoint, double> PointAndTangentAt(const ArrayPathMeasure& path, double distance)
{
    if (path.points.size() < 2 || path.length <= Tolerance::Model) {
        return {GeoPoint(), 0.0};
    }
    distance = std::clamp(distance, 0.0, path.length);
    auto upper = std::upper_bound(path.cumulative.begin(), path.cumulative.end(), distance);
    std::size_t segment = upper == path.cumulative.begin()
        ? 0
        : static_cast<std::size_t>(std::distance(path.cumulative.begin(), upper) - 1);
    segment = std::min(segment, path.points.size() - 2);
    const GeoPoint& start = path.points[segment];
    const GeoPoint& end = path.points[segment + 1];
    const double segmentLength = path.cumulative[segment + 1] - path.cumulative[segment];
    const double t = segmentLength <= Tolerance::Model
        ? 0.0
        : (distance - path.cumulative[segment]) / segmentLength;
    return {
        GeoPoint(
            start.X() + (end.X() - start.X()) * t,
            start.Y() + (end.Y() - start.Y()) * t),
        std::atan2(end.Y() - start.Y(), end.X() - start.X())};
}

GeoPoint ArraySourceBase(const CadDocument& document, const std::vector<EntityId>& sourceIds)
{
    GeoBounds total;
    for (EntityId id : sourceIds) {
        const Entity* entity = document.FindEntity(id);
        if (!entity) {
            continue;
        }
        const GeoBounds bounds = entity->Shaper().Bounds();
        if (!bounds.valid) {
            continue;
        }
        if (!total.valid) {
            total = bounds;
        } else {
            total.minX = std::min(total.minX, bounds.minX);
            total.minY = std::min(total.minY, bounds.minY);
            total.maxX = std::max(total.maxX, bounds.maxX);
            total.maxY = std::max(total.maxY, bounds.maxY);
        }
    }
    if (!total.valid) {
        return GeoPoint();
    }
    return GeoPoint((total.minX + total.maxX) * 0.5, (total.minY + total.maxY) * 0.5);
}

std::unique_ptr<Entity> CloneArrayEntity(const Entity& source)
{
    std::unique_ptr<Entity> clone = source.Clone();
    clone->SetId(IdGenerator::Next());
    clone->SetSelected(false);
    return clone;
}

} // namespace

std::optional<GeoLine> EditKernel::EntityAsCutLine(const Entity& entity, const GeoPoint& nearPoint)
{
    switch (entity.Type()) {
    case EntityType::Line:
        return static_cast<const LineEntity&>(entity).Line();
    case EntityType::Ray:
        return LineFromDirection(
            static_cast<const RayEntity&>(entity).Ray().Origin(),
            static_cast<const RayEntity&>(entity).Ray().ThroughPoint(),
            false);
    case EntityType::XLine:
        return LineFromDirection(
            static_cast<const XLineEntity&>(entity).XLine().Anchor(),
            static_cast<const XLineEntity&>(entity).XLine().ThroughPoint(),
            true);
    case EntityType::Polyline:
        return NearestPolylineSegment(
            static_cast<const PolylineEntity&>(entity).Polyline().Points(),
            nearPoint,
            static_cast<const PolylineEntity&>(entity).Polyline().Closed());
    case EntityType::Circle: {
        const auto& circle = static_cast<const CircleEntity&>(entity).Circle();
        const GeoPoint center = circle.Center();
        const double angle = std::atan2(nearPoint.Y() - center.Y(), nearPoint.X() - center.X());
        const GeoPoint through(center.X() + std::cos(angle), center.Y() + std::sin(angle));
        return LineFromDirection(center, through, true);
    }
    case EntityType::Rectangle: {
        const GeoRegion region = RegionFromEntity(entity);
        if (const auto segment = NearestPolylineSegment(region.Outer(), nearPoint)) {
            return segment;
        }
        return std::nullopt;
    }
    default:
        return std::nullopt;
    }
}

double EditKernel::ComputeOffsetDistance(const GeoPoint& from, const GeoPoint& to, const GeoRegion& region)
{
    const double distance = Distance2D(from, to);
    if (distance <= Tolerance::Model || region.Outer().empty()) {
        return 0.0;
    }

    double cx = 0.0;
    double cy = 0.0;
    for (const GeoPoint& point : region.Outer()) {
        cx += point.X();
        cy += point.Y();
    }
    const double count = static_cast<double>(region.Outer().size());
    cx /= count;
    cy /= count;

    const double vx = to.X() - from.X();
    const double vy = to.Y() - from.Y();
    const double wx = from.X() - cx;
    const double wy = from.Y() - cy;
    const double cross = wx * vy - wy * vx;
    return (cross >= 0.0 ? 1.0 : -1.0) * distance;
}

std::vector<EntityId> EditKernel::MirrorEntities(CadDocument& document, const std::vector<EntityId>& ids, const GeoPoint& axisStart, const GeoPoint& axisEnd)
{
    for (EntityId id : ids) {
        if (Entity* entity = document.FindEntity(id)) {
            entity->Mirror(axisStart, axisEnd);
        }
    }
    document.NotifyEntitiesUpdated(ids);
    return ids;
}

std::vector<std::unique_ptr<Entity>> EditKernel::CloneMirroredEntities(
    const CadDocument& document,
    const std::vector<EntityId>& ids,
    const GeoPoint& axisStart,
    const GeoPoint& axisEnd)
{
    std::vector<std::unique_ptr<Entity>> copies;
    copies.reserve(ids.size());
    for (EntityId id : ids) {
        const Entity* entity = document.FindEntity(id);
        if (!entity) {
            continue;
        }
        std::unique_ptr<Entity> clone = entity->Clone();
        clone->SetId(IdGenerator::Next());
        clone->SetSelected(false);
        clone->Mirror(axisStart, axisEnd);
        copies.push_back(std::move(clone));
    }
    return copies;
}

std::vector<EntityId> EditKernel::RotateEntities(CadDocument& document, const std::vector<EntityId>& ids, const GeoPoint& center, double angleRadians)
{
    for (EntityId id : ids) {
        if (Entity* entity = document.FindEntity(id)) {
            entity->Rotate(center, angleRadians);
        }
    }
    document.NotifyEntitiesUpdated(ids);
    return ids;
}

std::vector<EntityId> EditKernel::ScaleEntities(CadDocument& document, const std::vector<EntityId>& ids, const GeoPoint& center, double factor)
{
    for (EntityId id : ids) {
        if (Entity* entity = document.FindEntity(id)) {
            entity->Scale(center, factor);
        }
    }
    document.NotifyEntitiesUpdated(ids);
    return ids;
}

std::vector<std::unique_ptr<Entity>> EditKernel::CloneRectangularArray(
    const CadDocument& document,
    const std::vector<EntityId>& sourceIds,
    const RectangularArrayOptions& options)
{
    std::vector<std::unique_ptr<Entity>> copies;
    const int rows = std::max(1, options.rows);
    const int columns = std::max(1, options.columns);
    if (static_cast<long long>(rows) * static_cast<long long>(columns) > 100000) {
        return copies;
    }
    const double angle = options.angleDegrees * kArrayPi / 180.0;
    const GeoPoint columnVector(
        std::cos(angle) * options.columnSpacing,
        std::sin(angle) * options.columnSpacing);
    const GeoPoint rowVector(
        -std::sin(angle) * options.rowSpacing,
        std::cos(angle) * options.rowSpacing);
    copies.reserve(sourceIds.size() * static_cast<std::size_t>(rows * columns - 1));
    for (int row = 0; row < rows; ++row) {
        for (int column = 0; column < columns; ++column) {
            if (row == 0 && column == 0) {
                continue;
            }
            const double dx = columnVector.X() * column + rowVector.X() * row;
            const double dy = columnVector.Y() * column + rowVector.Y() * row;
            for (EntityId id : sourceIds) {
                const Entity* source = document.FindEntity(id);
                if (!source) {
                    continue;
                }
                std::unique_ptr<Entity> clone = CloneArrayEntity(*source);
                clone->MoveBy(dx, dy);
                copies.push_back(std::move(clone));
            }
        }
    }
    return copies;
}

std::vector<std::unique_ptr<Entity>> EditKernel::ClonePolarArray(
    const CadDocument& document,
    const std::vector<EntityId>& sourceIds,
    const GeoPoint& center,
    const PolarArrayOptions& options)
{
    std::vector<std::unique_ptr<Entity>> copies;
    const int count = std::min(10000, std::max(1, options.itemCount));
    if (count <= 1) {
        return copies;
    }
    const double fill = options.fillAngleDegrees * kArrayPi / 180.0;
    const bool fullCircle = std::abs(std::abs(options.fillAngleDegrees) - 360.0) <= 1.0e-9;
    const double step = fill / static_cast<double>(fullCircle ? count : count - 1);
    const GeoPoint sourceBase = options.basePoint.value_or(ArraySourceBase(document, sourceIds));
    copies.reserve(sourceIds.size() * static_cast<std::size_t>(count - 1));
    for (int item = 1; item < count; ++item) {
        const double angle = step * item;
        const GeoPoint rotatedBase = GeoTransform::RotatePoint(sourceBase, center, angle);
        for (EntityId id : sourceIds) {
            const Entity* source = document.FindEntity(id);
            if (!source) {
                continue;
            }
            std::unique_ptr<Entity> clone = CloneArrayEntity(*source);
            if (options.rotateItems) {
                clone->Rotate(center, angle);
            } else {
                clone->MoveBy(
                    rotatedBase.X() - sourceBase.X(),
                    rotatedBase.Y() - sourceBase.Y());
            }
            copies.push_back(std::move(clone));
        }
    }
    return copies;
}

std::vector<std::unique_ptr<Entity>> EditKernel::ClonePathArray(
    const CadDocument& document,
    const std::vector<EntityId>& sourceIds,
    EntityId pathId,
    const PathArrayOptions& options)
{
    std::vector<std::unique_ptr<Entity>> copies;
    const Entity* pathEntity = document.FindEntity(pathId);
    if (!pathEntity || !IsSupportedArrayPath(*pathEntity)) {
        return copies;
    }
    const ArrayPathMeasure path = MeasureArrayPath(*pathEntity);
    if (path.points.size() < 2 || path.length <= Tolerance::Model) {
        return copies;
    }

    std::vector<double> distances;
    if (options.method == PathArrayMethod::Measure) {
        const double spacing = std::abs(options.spacing);
        if (spacing <= Tolerance::Model) {
            return copies;
        }
        const int count = static_cast<int>(std::clamp(
            std::floor(path.length / spacing) + 1.0,
            1.0,
            10000.0));
        distances.reserve(static_cast<std::size_t>(count));
        for (int i = 0; i < count; ++i) {
            distances.push_back(std::min(path.length, spacing * i));
        }
    } else {
        const int count = std::min(10000, std::max(1, options.itemCount));
        distances.reserve(static_cast<std::size_t>(count));
        const double denominator = path.closed
            ? static_cast<double>(count)
            : static_cast<double>(std::max(1, count - 1));
        for (int i = 0; i < count; ++i) {
            distances.push_back(path.length * static_cast<double>(i) / denominator);
        }
    }
    if (distances.size() <= 1) {
        return copies;
    }

    const auto first = PointAndTangentAt(path, distances.front());
    const GeoPoint sourceBase = ArraySourceBase(document, sourceIds);
    copies.reserve(sourceIds.size() * (distances.size() - 1));
    for (std::size_t item = 1; item < distances.size(); ++item) {
        const auto placement = PointAndTangentAt(path, distances[item]);
        const double angle = placement.second - first.second;
        const double dx = placement.first.X() - first.first.X();
        const double dy = placement.first.Y() - first.first.Y();
        for (EntityId id : sourceIds) {
            const Entity* source = document.FindEntity(id);
            if (!source) {
                continue;
            }
            std::unique_ptr<Entity> clone = CloneArrayEntity(*source);
            if (options.alignItems) {
                clone->Rotate(sourceBase, angle);
            }
            clone->MoveBy(dx, dy);
            copies.push_back(std::move(clone));
        }
    }
    return copies;
}

bool EditKernel::IsSupportedArrayPath(const Entity& entity)
{
    switch (entity.Type()) {
    case EntityType::Line:
    case EntityType::Arc:
    case EntityType::Circle:
    case EntityType::Ellipse:
    case EntityType::Rectangle:
    case EntityType::Polygon:
    case EntityType::Polyline:
    case EntityType::Spline:
        return true;
    default:
        return false;
    }
}

bool EditKernel::TrimLineEntity(CadDocument& document, EntityId targetId, EntityId cutterId, const GeoPoint& pickPoint)
{
    Entity* target = document.FindEntity(targetId);
    Entity* cutter = document.FindEntity(cutterId);
    if (!target || !cutter || target->Type() != EntityType::Line) {
        return false;
    }

    const std::optional<GeoLine> cutterLine = EntityAsCutLine(*cutter, pickPoint);
    if (!cutterLine) {
        return false;
    }

    auto& lineEntity = static_cast<LineEntity&>(*target);
    const auto trimmed = GeoTrimExtend::TrimLine(lineEntity.Line(), *cutterLine, pickPoint);
    if (!trimmed) {
        return false;
    }
    lineEntity.Line() = *trimmed;
    document.NotifyEntitiesUpdated({targetId});
    return true;
}

bool EditKernel::ExtendLineEntity(CadDocument& document, EntityId targetId, EntityId boundaryId, const GeoPoint& pickPoint)
{
    Entity* target = document.FindEntity(targetId);
    Entity* boundary = document.FindEntity(boundaryId);
    if (!target || !boundary || target->Type() != EntityType::Line) {
        return false;
    }

    const std::optional<GeoLine> boundaryLine = EntityAsCutLine(*boundary, pickPoint);
    if (!boundaryLine) {
        return false;
    }

    auto& lineEntity = static_cast<LineEntity&>(*target);
    const auto extended = GeoTrimExtend::ExtendLine(lineEntity.Line(), *boundaryLine, pickPoint);
    if (!extended) {
        return false;
    }
    lineEntity.Line() = *extended;
    document.NotifyEntitiesUpdated({targetId});
    return true;
}

std::vector<std::unique_ptr<Entity>> EditKernel::OffsetSelectedEntities(
    CadDocument& document,
    const std::vector<EntityId>& ids,
    const GeoPoint& fromPoint,
    const GeoPoint& toPoint)
{
    std::vector<std::unique_ptr<Entity>> created;
    for (EntityId id : ids) {
        const Entity* entity = document.FindEntity(id);
        if (!entity) {
            continue;
        }
        const GeoRegion sourceRegion = RegionFromEntity(*entity);
        if (!sourceRegion.IsClosed()) {
            continue;
        }
        const double delta = ComputeOffsetDistance(fromPoint, toPoint, sourceRegion);
        if (std::abs(delta) <= Tolerance::Model) {
            continue;
        }
        for (const GeoRegion& offsetRegion : ClipperUtil::OffsetRegion(sourceRegion, delta)) {
            created.push_back(std::make_unique<RegionEntity>(IdGenerator::Next(), offsetRegion, true));
        }
    }
    return created;
}

std::vector<GeoRegion> EditKernel::CollectRegions(const CadDocument& document, const std::vector<EntityId>& ids)
{
    std::vector<GeoRegion> regions;
    for (EntityId id : ids) {
        const Entity* entity = document.FindEntity(id);
        if (!entity) {
            continue;
        }
        const GeoRegion region = RegionFromEntity(*entity);
        if (region.IsClosed()) {
            regions.push_back(region);
        }
    }
    return regions;
}

std::vector<EntityId> EditKernel::OffsetSelectedRegions(CadDocument& document, const std::vector<EntityId>& ids, double delta)
{
    std::vector<EntityId> created;
    for (EntityId id : ids) {
        const Entity* entity = document.FindEntity(id);
        if (!entity || entity->Type() != EntityType::Region) {
            continue;
        }
        const auto& source = static_cast<const RegionEntity*>(entity)->Region();
        for (const GeoRegion& offsetRegion : ClipperUtil::OffsetRegion(source, delta)) {
            const EntityId newId = IdGenerator::Next();
            document.AddEntity(std::make_unique<RegionEntity>(newId, offsetRegion, true));
            created.push_back(newId);
        }
    }
    return created;
}

std::vector<EntityId> EditKernel::BooleanSelected(CadDocument& document, const std::vector<EntityId>& subjectIds, const std::vector<EntityId>& clipIds, BooleanOp op)
{
    const std::vector<GeoRegion> subjects = CollectRegions(document, subjectIds);
    const std::vector<GeoRegion> clips = CollectRegions(document, clipIds);
    const std::vector<GeoRegion> result = ClipperUtil::BooleanOperation(op, subjects, clips);

    std::vector<EntityId> created;
    for (const GeoRegion& region : result) {
        const EntityId newId = IdGenerator::Next();
        document.AddEntity(std::make_unique<RegionEntity>(newId, region, true));
        created.push_back(newId);
    }
    return created;
}

std::vector<std::unique_ptr<Entity>> EditKernel::BuildHatchEntities(const CadDocument& document, EntityId regionId, double spacing)
{
    const Entity* entity = document.FindEntity(regionId);
    if (!entity) {
        return {};
    }

    GeoRegion region;
    if (entity->Type() == EntityType::Region) {
        region = static_cast<const RegionEntity*>(entity)->Region();
    } else {
        region = RegionFromEntity(*entity);
    }
    if (!region.IsClosed()) {
        return {};
    }

    const double effectiveSpacing = std::max(spacing, Tolerance::Model * 10.0);
    const std::vector<GeoLine> hatchLines = ClipperUtil::GenerateHatch(region, effectiveSpacing, 0.0);
    std::vector<std::unique_ptr<Entity>> created;
    for (const GeoLine& line : hatchLines) {
        created.push_back(std::make_unique<LineEntity>(IdGenerator::Next(), line));
    }
    return created;
}

std::vector<std::unique_ptr<Entity>> EditKernel::BreakEntity(
    const CadDocument& document,
    EntityId targetId,
    const GeoPoint& firstPoint,
    const std::optional<GeoPoint>& secondPoint)
{
    const Entity* entity = document.FindEntity(targetId);
    if (!entity) {
        return {};
    }

    std::vector<std::unique_ptr<Entity>> created;
    auto addLine = [&](const GeoLine& line) {
        auto lineEntity = std::make_unique<LineEntity>(IdGenerator::Next(), line);
        CopyEntityProperties(*entity, *lineEntity);
        created.push_back(std::move(lineEntity));
    };
    auto addPolyline = [&](const std::vector<GeoPoint>& points) {
        auto polylineEntity = std::make_unique<PolylineEntity>(IdGenerator::Next(), GeoPolyline(points));
        CopyEntityProperties(*entity, *polylineEntity);
        created.push_back(std::move(polylineEntity));
    };
    auto addArc = [&](const GeoArc& arc) {
        double span = arc.EndAngleRadians() - arc.StartAngleRadians();
        while (span <= 0.0) {
            span += 2.0 * 3.14159265358979323846;
        }
        if (span <= Tolerance::Angular) {
            return;
        }
        auto arcEntity = std::make_unique<ArcEntity>(IdGenerator::Next(), arc);
        CopyEntityProperties(*entity, *arcEntity);
        created.push_back(std::move(arcEntity));
    };
    auto addSpline = [&](const GeoSpline& spline) {
        if (spline.Points().size() < 2) {
            return;
        }
        auto splineEntity = std::make_unique<SplineEntity>(IdGenerator::Next(), spline);
        CopyEntityProperties(*entity, *splineEntity);
        created.push_back(std::move(splineEntity));
    };

    switch (entity->Type()) {
    case EntityType::Line: {
        for (const GeoLine& segment : GeoBreak::BreakLine(static_cast<const LineEntity&>(*entity).Line(), firstPoint, secondPoint)) {
            if (Distance2D(segment.Start(), segment.End()) > Tolerance::Model) {
                addLine(segment);
            }
        }
        break;
    }
    case EntityType::Polyline: {
        for (const auto& part : GeoBreak::BreakPolyline(
                 static_cast<const PolylineEntity&>(*entity).Polyline().Points(),
                 firstPoint,
                 secondPoint)) {
            if (part.size() >= 2) {
                addPolyline(part);
            }
        }
        break;
    }
    case EntityType::Arc: {
        for (const GeoArc& arc : GeoBreak::BreakArc(static_cast<const ArcEntity&>(*entity).Arc(), firstPoint, secondPoint)) {
            addArc(arc);
        }
        break;
    }
    case EntityType::Circle: {
        for (const GeoArc& arc : GeoBreak::BreakCircle(static_cast<const CircleEntity&>(*entity).Circle(), firstPoint, secondPoint)) {
            addArc(arc);
        }
        break;
    }
    case EntityType::Spline: {
        for (const GeoSpline& spline : GeoBreak::BreakSpline(static_cast<const SplineEntity&>(*entity).Spline(), firstPoint, secondPoint)) {
            addSpline(spline);
        }
        break;
    }
    default:
        break;
    }

    return created;
}

namespace {

enum class EdgeKind {
    Line,
    Arc
};

struct ResolvedEdge {
    EdgeKind kind = EdgeKind::Line;
    GeoLine line;
    GeoArc arc;
};

bool PointsNear(const GeoPoint& a, const GeoPoint& b)
{
    return Distance2D(a, b) <= Tolerance::Model * 10.0;
}

GeoLine OrientLineFromCorner(const GeoLine& line, const GeoPoint& corner)
{
    if (PointsNear(line.Start(), corner)) {
        return line;
    }
    return GeoLine(line.End(), line.Start());
}

std::optional<GeoPoint> SharedEndpoint(const GeoLine& a, const GeoLine& b)
{
    if (PointsNear(a.Start(), b.Start()) || PointsNear(a.Start(), b.End())) {
        return PointsNear(a.Start(), b.Start()) ? a.Start() : a.Start();
    }
    if (PointsNear(a.End(), b.Start()) || PointsNear(a.End(), b.End())) {
        return PointsNear(a.End(), b.Start()) ? a.End() : a.End();
    }
    return std::nullopt;
}

std::optional<GeoPoint> SharedEndpoint(const GeoLine& line, const GeoArc& arc)
{
    const GeoPoint start(
        arc.Center().X() + std::cos(arc.StartAngleRadians()) * arc.Radius(),
        arc.Center().Y() + std::sin(arc.StartAngleRadians()) * arc.Radius());
    const GeoPoint end(
        arc.Center().X() + std::cos(arc.EndAngleRadians()) * arc.Radius(),
        arc.Center().Y() + std::sin(arc.EndAngleRadians()) * arc.Radius());
    if (PointsNear(line.Start(), start)) {
        return line.Start();
    }
    if (PointsNear(line.Start(), end)) {
        return line.Start();
    }
    if (PointsNear(line.End(), start)) {
        return line.End();
    }
    if (PointsNear(line.End(), end)) {
        return line.End();
    }
    return std::nullopt;
}

int ClosestSegmentIndex(const std::vector<GeoPoint>& points, const GeoPoint& pick, bool closed = false)
{
    int bestIndex = -1;
    double bestDistance = std::numeric_limits<double>::max();
    if (points.size() < 2) {
        return bestIndex;
    }
    const std::size_t count = points.size();
    const std::size_t segments = closed ? count : count - 1;
    for (std::size_t i = 0; i < segments; ++i) {
        const GeoLine segment(points[i], points[(i + 1) % count]);
        const double distance = DistancePointToSegment(pick, segment.Start(), segment.End());
        if (distance < bestDistance) {
            bestDistance = distance;
            bestIndex = static_cast<int>(i);
        }
    }
    return bestIndex;
}

std::optional<std::pair<GeoLine, GeoLine>> AdjacentPolylineSegments(
    const std::vector<GeoPoint>& points,
    const GeoPoint& pick1,
    const GeoPoint& pick2,
    bool closed = false)
{
    const int index1 = ClosestSegmentIndex(points, pick1, closed);
    const int index2 = ClosestSegmentIndex(points, pick2, closed);
    if (index1 < 0 || index2 < 0 || index1 == index2) {
        return std::nullopt;
    }
    const int n = static_cast<int>(points.size());
    const int segments = closed ? n : n - 1;
    auto adjacent = [&](int a, int b) {
        const int d = std::abs(a - b);
        if (d == 1) {
            return true;
        }
        return closed && d == segments - 1;
    };
    if (!adjacent(index1, index2)) {
        return std::nullopt;
    }
    const GeoLine seg1(
        points[static_cast<std::size_t>(index1)],
        points[static_cast<std::size_t>((index1 + 1) % n)]);
    const GeoLine seg2(
        points[static_cast<std::size_t>(index2)],
        points[static_cast<std::size_t>((index2 + 1) % n)]);
    return std::make_pair(seg1, seg2);
}

std::optional<GeoPolyline> PolylineLikeFromEntity(const Entity& entity)
{
    switch (entity.Type()) {
    case EntityType::Polyline:
        return static_cast<const PolylineEntity&>(entity).Polyline();
    case EntityType::Rectangle: {
        const auto& rect = static_cast<const RectangleEntity&>(entity).Rectangle();
        return GeoPolyline(DrawAlgorithms::RectanglePoints(rect.Corner1(), rect.Corner2()), true);
    }
    case EntityType::Polygon: {
        const auto& polygon = static_cast<const PolygonEntity&>(entity).Polygon();
        auto points = DrawAlgorithms::RegularPolygonPoints(
            polygon.Center(),
            polygon.Radius(),
            polygon.Sides(),
            polygon.RotationRadians());
        if (points.size() < 3) {
            return std::nullopt;
        }
        return GeoPolyline(std::move(points), true);
    }
    default:
        return std::nullopt;
    }
}

std::optional<ResolvedEdge> ResolveEdge(const Entity& entity, const GeoPoint& pick)
{
    switch (entity.Type()) {
    case EntityType::Line:
        return ResolvedEdge{EdgeKind::Line, static_cast<const LineEntity&>(entity).Line(), {}};
    case EntityType::Arc:
        return ResolvedEdge{EdgeKind::Arc, {}, static_cast<const ArcEntity&>(entity).Arc()};
    case EntityType::Circle: {
        const auto& circle = static_cast<const CircleEntity&>(entity).Circle();
        return ResolvedEdge{
            EdgeKind::Arc,
            {},
            GeoArc(circle.Center(), circle.Radius(), 0.0, 2.0 * 3.14159265358979323846)};
    }
    case EntityType::Polyline:
    case EntityType::Rectangle:
    case EntityType::Polygon: {
        const auto loop = PolylineLikeFromEntity(entity);
        if (!loop) {
            return std::nullopt;
        }
        const auto segment = NearestPolylineSegment(loop->Points(), pick, loop->Closed());
        if (!segment) {
            return std::nullopt;
        }
        return ResolvedEdge{EdgeKind::Line, *segment, {}};
    }
    default:
        return std::nullopt;
    }
}

void AppendEditedSegment(
    std::vector<std::unique_ptr<Entity>>& created,
    const Entity& styleSource,
    const EditedSegment& segment)
{
    if (segment.type == EditedSegment::Type::Line) {
        if (Distance2D(segment.line.Start(), segment.line.End()) <= Tolerance::Model) {
            return;
        }
        auto entity = std::make_unique<LineEntity>(IdGenerator::Next(), segment.line);
        CopyEntityProperties(styleSource, *entity);
        created.push_back(std::move(entity));
        return;
    }
    double span = segment.arc.EndAngleRadians() - segment.arc.StartAngleRadians();
    while (span <= 0.0) {
        span += 2.0 * 3.14159265358979323846;
    }
    if (span <= Tolerance::Angular || segment.arc.Radius() <= Tolerance::Model) {
        return;
    }
    auto entity = std::make_unique<ArcEntity>(IdGenerator::Next(), segment.arc);
    CopyEntityProperties(styleSource, *entity);
    created.push_back(std::move(entity));
}

void AppendArcEntity(
    std::vector<std::unique_ptr<Entity>>& created,
    const Entity& styleSource,
    const GeoArc& arc)
{
    AppendEditedSegment(created, styleSource, EditedSegment{EditedSegment::Type::Arc, {}, arc});
}

void AppendPolylineIfNeeded(
    std::vector<std::unique_ptr<Entity>>& created,
    const Entity& styleSource,
    const std::vector<GeoPoint>& points)
{
    if (points.size() < 2) {
        return;
    }
    auto entity = std::make_unique<PolylineEntity>(IdGenerator::Next(), GeoPolyline(points));
    CopyEntityProperties(styleSource, *entity);
    created.push_back(std::move(entity));
}

void AppendFilletResult(
    std::vector<std::unique_ptr<Entity>>& created,
    const Entity& styleSource,
    const FilletResult& result,
    bool trim)
{
    if (trim) {
        AppendEditedSegment(created, styleSource, result.segment1);
        AppendEditedSegment(created, styleSource, result.segment2);
    }
    if (result.arc) {
        AppendArcEntity(created, styleSource, *result.arc);
    }
}

void AppendChamferResult(
    std::vector<std::unique_ptr<Entity>>& created,
    const Entity& styleSource,
    const ChamferResult& result,
    bool trim)
{
    if (trim) {
        AppendEditedSegment(created, styleSource, result.segment1);
        AppendEditedSegment(created, styleSource, result.segment2);
    }
    if (Distance2D(result.chamfer.Start(), result.chamfer.End()) > Tolerance::Model) {
        auto entity = std::make_unique<LineEntity>(IdGenerator::Next(), result.chamfer);
        CopyEntityProperties(styleSource, *entity);
        created.push_back(std::move(entity));
    }
}

std::optional<FilletResult> ComputeFillet(
    const ResolvedEdge& edge1,
    const ResolvedEdge& edge2,
    double radius,
    const GeoPoint& pick1,
    const GeoPoint& pick2)
{
    const GeoPoint pickNear((pick1.X() + pick2.X()) * 0.5, (pick1.Y() + pick2.Y()) * 0.5);
    if (edge1.kind == EdgeKind::Line && edge2.kind == EdgeKind::Line) {
        return GeoFilletChamfer::FilletLines(edge1.line, edge2.line, radius, pick1, pick2);
    }
    if (radius <= Tolerance::Model) {
        return std::nullopt;
    }
    if (edge1.kind == EdgeKind::Line && edge2.kind == EdgeKind::Arc) {
        return GeoFilletChamfer::FilletLineArc(edge1.line, edge2.arc, radius, pickNear);
    }
    if (edge1.kind == EdgeKind::Arc && edge2.kind == EdgeKind::Line) {
        const auto result = GeoFilletChamfer::FilletLineArc(edge2.line, edge1.arc, radius, pickNear);
        if (!result) {
            return std::nullopt;
        }
        FilletResult swapped = *result;
        std::swap(swapped.segment1, swapped.segment2);
        return swapped;
    }
    if (edge1.kind == EdgeKind::Arc && edge2.kind == EdgeKind::Arc) {
        return GeoFilletChamfer::FilletArcs(edge1.arc, edge2.arc, radius, pickNear);
    }
    return std::nullopt;
}

std::optional<ChamferResult> ComputeChamfer(
    const ResolvedEdge& edge1,
    const ResolvedEdge& edge2,
    const ChamferOptions& options,
    const GeoPoint& pick1,
    const GeoPoint& pick2)
{
    const GeoPoint pickNear((pick1.X() + pick2.X()) * 0.5, (pick1.Y() + pick2.Y()) * 0.5);
    if (edge1.kind == EdgeKind::Line && edge2.kind == EdgeKind::Line) {
        if (options.method == ChamferMethod::Angle) {
            return GeoFilletChamfer::ChamferLinesByAngle(
                edge1.line, edge2.line, options.length, options.angleDegrees, pick1, pick2);
        }
        return GeoFilletChamfer::ChamferLines(
            edge1.line, edge2.line, options.distance1, options.distance2, pick1, pick2);
    }
    if (options.method == ChamferMethod::Angle) {
        return std::nullopt;
    }
    const double distance = options.distance1;
    if (edge1.kind == EdgeKind::Line && edge2.kind == EdgeKind::Arc) {
        return GeoFilletChamfer::ChamferLineArc(edge1.line, edge2.arc, distance, pickNear);
    }
    if (edge1.kind == EdgeKind::Arc && edge2.kind == EdgeKind::Line) {
        const auto result = GeoFilletChamfer::ChamferLineArc(edge2.line, edge1.arc, distance, pickNear);
        if (!result) {
            return std::nullopt;
        }
        ChamferResult swapped = *result;
        std::swap(swapped.segment1, swapped.segment2);
        return swapped;
    }
    if (edge1.kind == EdgeKind::Arc && edge2.kind == EdgeKind::Arc) {
        return GeoFilletChamfer::ChamferArcs(edge1.arc, edge2.arc, distance, pickNear);
    }
    return std::nullopt;
}

std::vector<EntityId> UniqueEntityIds(EntityId id1, EntityId id2)
{
    if (id1 == id2) {
        return {id1};
    }
    return {id1, id2};
}

GeoPoint SegmentTangentEnd(const EditedSegment& segment)
{
    if (segment.type == EditedSegment::Type::Line) {
        return segment.line.End();
    }
    const double end = segment.arc.EndAngleRadians();
    return GeoPoint(
        segment.arc.Center().X() + std::cos(end) * segment.arc.Radius(),
        segment.arc.Center().Y() + std::sin(end) * segment.arc.Radius());
}

void SetCornerError(CornerEditError* error, CornerEditError value)
{
    if (error) {
        *error = value;
    }
}

bool PointOnFiniteSegment(const GeoLine& segment, const GeoPoint& point)
{
    return DistancePointToSegment(point, segment.Start(), segment.End()) <= Tolerance::Model * 50.0;
}

bool FilletFitsFiniteSegments(const GeoLine& first, const GeoLine& second, const FilletResult& result)
{
    return PointOnFiniteSegment(first, SegmentTangentEnd(result.segment1))
        && PointOnFiniteSegment(second, SegmentTangentEnd(result.segment2));
}

bool ChamferFitsFiniteSegments(const GeoLine& first, const GeoLine& second, const ChamferResult& result)
{
    return PointOnFiniteSegment(first, result.chamfer.Start())
        && PointOnFiniteSegment(second, result.chamfer.End());
}

bool FiniteEntityAcceptsTangent(const Entity& entity, const GeoPoint& pick, const GeoPoint& tangent)
{
    const auto loop = PolylineLikeFromEntity(entity);
    if (!loop) {
        return true;
    }
    const int index = ClosestSegmentIndex(loop->Points(), pick, loop->Closed());
    if (index < 0) {
        return false;
    }
    const auto& points = loop->Points();
    const int count = static_cast<int>(points.size());
    const int next = loop->Closed() ? ((index + 1) % count) : (index + 1);
    if (next < 0 || next >= count) {
        return false;
    }
    return PointOnFiniteSegment(
        GeoLine(points[static_cast<std::size_t>(index)], points[static_cast<std::size_t>(next)]),
        tangent);
}

GeoLine PolylineSegmentAt(const std::vector<GeoPoint>& points, int index)
{
    const int n = static_cast<int>(points.size());
    return GeoLine(
        points[static_cast<std::size_t>(index)],
        points[static_cast<std::size_t>((index + 1) % n)]);
}

bool ImpliedPolylineCornerUnfitFillet(
    const GeoPolyline& loop,
    const GeoPoint& pick1,
    const GeoPoint& toward,
    double radius)
{
    if (radius <= Tolerance::Model) {
        return false;
    }
    const auto& points = loop.Points();
    const bool closed = loop.Closed();
    const int firstIndex = ClosestSegmentIndex(points, pick1, closed);
    const int hoverIndex = ClosestSegmentIndex(points, toward, closed);
    if (firstIndex < 0 || hoverIndex < 0 || firstIndex == hoverIndex) {
        return false;
    }
    const int count = static_cast<int>(points.size());
    const int segments = closed ? count : count - 1;
    const GeoLine first = PolylineSegmentAt(points, firstIndex);
    const bool towardEnd = Distance2D(toward, first.End()) <= Distance2D(toward, first.Start());
    int secondIndex = towardEnd ? firstIndex + 1 : firstIndex - 1;
    if (closed) {
        secondIndex = (secondIndex % segments + segments) % segments;
    } else if (secondIndex < 0 || secondIndex >= segments) {
        return false;
    }
    const GeoLine second = PolylineSegmentAt(points, secondIndex);
    const GeoPoint shared = towardEnd ? first.End() : first.Start();
    const GeoPoint pickAdj = Distance2D(second.Start(), shared) <= Distance2D(second.End(), shared)
        ? second.End()
        : second.Start();
    const auto result = GeoFilletChamfer::FilletLines(first, second, radius, pick1, pickAdj);
    if (!result) {
        return false;
    }
    return !FilletFitsFiniteSegments(first, second, *result);
}

bool ImpliedPolylineCornerUnfitChamfer(
    const GeoPolyline& loop,
    const GeoPoint& pick1,
    const GeoPoint& toward,
    const ChamferOptions& options)
{
    const bool hasDistance = options.method == ChamferMethod::Angle
        ? options.length > Tolerance::Model
        : (options.distance1 > Tolerance::Model || options.distance2 > Tolerance::Model);
    if (!hasDistance) {
        return false;
    }
    const auto& points = loop.Points();
    const bool closed = loop.Closed();
    const int firstIndex = ClosestSegmentIndex(points, pick1, closed);
    const int hoverIndex = ClosestSegmentIndex(points, toward, closed);
    if (firstIndex < 0 || hoverIndex < 0 || firstIndex == hoverIndex) {
        return false;
    }
    const int count = static_cast<int>(points.size());
    const int segments = closed ? count : count - 1;
    const GeoLine first = PolylineSegmentAt(points, firstIndex);
    const bool towardEnd = Distance2D(toward, first.End()) <= Distance2D(toward, first.Start());
    int secondIndex = towardEnd ? firstIndex + 1 : firstIndex - 1;
    if (closed) {
        secondIndex = (secondIndex % segments + segments) % segments;
    } else if (secondIndex < 0 || secondIndex >= segments) {
        return false;
    }
    const GeoLine second = PolylineSegmentAt(points, secondIndex);
    const GeoPoint shared = towardEnd ? first.End() : first.Start();
    const GeoPoint pickAdj = Distance2D(second.Start(), shared) <= Distance2D(second.End(), shared)
        ? second.End()
        : second.Start();
    std::optional<ChamferResult> result;
    if (options.method == ChamferMethod::Angle) {
        result = GeoFilletChamfer::ChamferLinesByAngle(
            first, second, options.length, options.angleDegrees, pick1, pickAdj);
    } else {
        result = GeoFilletChamfer::ChamferLines(
            first, second, options.distance1, options.distance2, pick1, pickAdj);
    }
    if (!result) {
        return false;
    }
    return !ChamferFitsFiniteSegments(first, second, *result);
}

constexpr double kPi = 3.14159265358979323846;

int SharedPolylineVertexIndex(
    const std::vector<GeoPoint>& points,
    int index1,
    int index2,
    bool closed)
{
    const int n = static_cast<int>(points.size());
    if (n < 2 || index1 < 0 || index2 < 0 || index1 >= n || index2 >= n) {
        return -1;
    }
    const int next1 = closed ? (index1 + 1) % n : index1 + 1;
    const int next2 = closed ? (index2 + 1) % n : index2 + 1;
    if (next1 >= 0 && next1 < n && next1 == index2) {
        return index2;
    }
    if (next2 >= 0 && next2 < n && next2 == index1) {
        return index1;
    }
    return -1;
}

std::optional<GeoPoint> SharedPolylineCorner(
    const std::vector<GeoPoint>& points,
    int index1,
    int index2,
    bool closed)
{
    const int vertex = SharedPolylineVertexIndex(points, index1, index2, closed);
    if (vertex < 0) {
        return std::nullopt;
    }
    return points[static_cast<std::size_t>(vertex)];
}

GeoPoint ArcMidpoint(const GeoArc& arc)
{
    const double mid = (arc.StartAngleRadians() + arc.EndAngleRadians()) * 0.5;
    return GeoPoint(
        arc.Center().X() + std::cos(mid) * arc.Radius(),
        arc.Center().Y() + std::sin(mid) * arc.Radius());
}

double BulgeTowardCorner(
    const GeoPoint& start,
    const GeoPoint& end,
    const GeoPoint& center,
    const GeoPoint& corner)
{
    const double a1 = std::atan2(start.Y() - center.Y(), start.X() - center.X());
    const double a2 = std::atan2(end.Y() - center.Y(), end.X() - center.X());
    double sweep = a2 - a1;
    while (sweep <= -kPi) {
        sweep += 2.0 * kPi;
    }
    while (sweep > kPi) {
        sweep -= 2.0 * kPi;
    }
    const double mag = std::abs(std::tan(sweep * 0.25));
    if (mag <= 1.0e-12) {
        return 0.0;
    }
    auto score = [&](double bulge) {
        const auto arc = DrawAlgorithms::ArcFromBulge(start, end, bulge);
        if (!arc) {
            return std::numeric_limits<double>::max();
        }
        return Distance2D(ArcMidpoint(*arc), corner);
    };
    return score(mag) <= score(-mag) ? mag : -mag;
}

double FilletBulge(const FilletResult& result)
{
    if (!result.arc) {
        return 0.0;
    }
    const GeoPoint t1 = SegmentTangentEnd(result.segment1);
    const GeoPoint t2 = SegmentTangentEnd(result.segment2);
    const GeoPoint& center = result.arc->Center();
    const double start = std::atan2(t1.Y() - center.Y(), t1.X() - center.X());
    const double end = std::atan2(t2.Y() - center.Y(), t2.X() - center.X());
    double sweep = end - start;
    while (sweep <= -kPi) {
        sweep += 2.0 * kPi;
    }
    while (sweep > kPi) {
        sweep -= 2.0 * kPi;
    }
    return std::tan(sweep * 0.25);
}

bool ReplacePolylineSegments(
    GeoPolyline& polyline,
    int index1,
    int index2,
    const GeoPoint& t1,
    const GeoPoint& t2,
    double bulge,
    const GeoPoint* filletCenter = nullptr)
{
    polyline.EnsureBulges();
    const auto& oldPts = polyline.Points();
    const auto& oldBul = polyline.Bulges();
    const int n = static_cast<int>(oldPts.size());
    if (n < 2) {
        return false;
    }
    const bool closed = polyline.Closed();
    const auto corner = SharedPolylineCorner(oldPts, index1, index2, closed);

    int a = index1;
    int b = index2;
    GeoPoint ta = t1;
    GeoPoint tb = t2;
    if (closed) {
        const int ab = (b - a + n) % n;
        const int ba = (a - b + n) % n;
        if (ba < ab) {
            std::swap(a, b);
            std::swap(ta, tb);
            bulge = -bulge;
        }
    } else if (b < a) {
        std::swap(a, b);
        std::swap(ta, tb);
        bulge = -bulge;
    }
    if (filletCenter && corner) {
        bulge = BulgeTowardCorner(ta, tb, *filletCenter, *corner);
    }

    const int gap = closed ? ((b - a + n) % n) : (b - a);
    if (gap != 1 && gap != 2) {
        return false;
    }

    std::vector<GeoPoint> newPts;
    std::vector<double> newBul;
    newPts.reserve(static_cast<std::size_t>(n) + 2);
    newBul.reserve(static_cast<std::size_t>(n) + 2);

    if (closed && a > b) {
        newPts.push_back(tb);
        newBul.push_back(0.0);
        for (int i = b + 1; i <= a; ++i) {
            newPts.push_back(oldPts[static_cast<std::size_t>(i)]);
            newBul.push_back(i == a ? 0.0 : oldBul[static_cast<std::size_t>(i)]);
        }
        newPts.push_back(ta);
        newBul.push_back(bulge);
        if (newPts.size() < 2) {
            return false;
        }
        if (std::abs(bulge) > 1.0e-12) {
            const std::size_t filletIndex = newPts.size() - 1;
            const int count = static_cast<int>(newPts.size());
            const GeoPoint& start = newPts[filletIndex];
            const GeoPoint& end = newPts.front();
            const GeoPoint& incoming = newPts[static_cast<std::size_t>(count - 2)];
            const GeoPoint& outgoing = newPts[1];
            const double cross = (start.X() - incoming.X()) * (outgoing.Y() - end.Y())
                - (start.Y() - incoming.Y()) * (outgoing.X() - end.X());
            newBul[filletIndex] = (cross >= 0.0 ? std::abs(bulge) : -std::abs(bulge));
        }
        polyline = GeoPolyline(std::move(newPts), std::move(newBul), true);
        return true;
    }

    for (int i = 0; i <= a; ++i) {
        newPts.push_back(oldPts[static_cast<std::size_t>(i)]);
        newBul.push_back(i == a ? 0.0 : oldBul[static_cast<std::size_t>(i)]);
    }
    newPts.push_back(ta);
    newBul.push_back(bulge);
    newPts.push_back(tb);
    newBul.push_back(0.0);
    for (int i = b + 1; i < n; ++i) {
        newPts.push_back(oldPts[static_cast<std::size_t>(i)]);
        newBul.push_back(oldBul[static_cast<std::size_t>(i)]);
    }
    if (newPts.size() < 2) {
        return false;
    }
    if (std::abs(bulge) > 1.0e-12 && newPts.size() >= 4) {
        const int count = static_cast<int>(newPts.size());
        const int filletIndex = a + 1;
        const int endi = filletIndex + 1;
        int after = filletIndex + 2;
        if (closed) {
            after = (after + count) % count;
        }
        if (filletIndex > 0 && endi < count && after >= 0 && after < count) {
            const GeoPoint& start = newPts[static_cast<std::size_t>(filletIndex)];
            const GeoPoint& endPt = newPts[static_cast<std::size_t>(endi)];
            const GeoPoint& incoming = newPts[static_cast<std::size_t>(filletIndex - 1)];
            const GeoPoint& outgoing = newPts[static_cast<std::size_t>(after)];
            const double cross = (start.X() - incoming.X()) * (outgoing.Y() - endPt.Y())
                - (start.Y() - incoming.Y()) * (outgoing.X() - endPt.X());
            newBul[static_cast<std::size_t>(filletIndex)] =
                (cross >= 0.0 ? std::abs(bulge) : -std::abs(bulge));
        }
    }
    polyline = GeoPolyline(std::move(newPts), std::move(newBul), closed);
    return true;
}

std::optional<CornerEditOutput> BuildPolylineInPlace(
    const Entity& entity,
    const GeoPolyline& source,
    const GeoPoint& pick1,
    const GeoPoint& pick2,
    const FilletResult* fillet,
    const ChamferResult* chamfer,
    bool trim)
{
    const auto& points = source.Points();
    const int index1 = ClosestSegmentIndex(points, pick1, source.Closed());
    const int index2 = ClosestSegmentIndex(points, pick2, source.Closed());
    if (index1 < 0 || index2 < 0) {
        return std::nullopt;
    }

    GeoPoint t1;
    GeoPoint t2;
    double bulge = 0.0;
    if (fillet) {
        t1 = SegmentTangentEnd(fillet->segment1);
        t2 = SegmentTangentEnd(fillet->segment2);
        bulge = FilletBulge(*fillet);
        if (!fillet->arc && Distance2D(t1, t2) <= Tolerance::Model) {
            t2 = t1;
            bulge = 0.0;
        }
    } else if (chamfer) {
        t1 = chamfer->chamfer.Start();
        t2 = chamfer->chamfer.End();
        bulge = 0.0;
        if (Distance2D(t1, t2) <= Tolerance::Model) {
            return std::nullopt;
        }
    } else {
        return std::nullopt;
    }

    if (!trim) {
        CornerEditOutput output;
        if (fillet && fillet->arc) {
            AppendArcEntity(output.created, entity, *fillet->arc);
        } else if (chamfer) {
            auto line = std::make_unique<LineEntity>(IdGenerator::Next(), chamfer->chamfer);
            CopyEntityProperties(entity, *line);
            output.created.push_back(std::move(line));
        }
        return output;
    }

    GeoPolyline updated = source;
    const GeoPoint* filletCenter = (fillet && fillet->arc) ? &fillet->arc->Center() : nullptr;
    if (!ReplacePolylineSegments(updated, index1, index2, t1, t2, bulge, filletCenter)) {
        return std::nullopt;
    }
    if (updated.Points().size() < 2) {
        return std::nullopt;
    }
    CornerEditOutput output;
    output.removeIds = {entity.Id()};
    auto created = std::make_unique<PolylineEntity>(IdGenerator::Next(), std::move(updated));
    CopyEntityProperties(entity, *created);
    output.created.push_back(std::move(created));
    return output;
}

bool FilletPolylineVertex(
    GeoPolyline& polyline,
    int vertex,
    const FilletOptions& options,
    bool* tooLarge = nullptr)
{
    if (tooLarge) {
        *tooLarge = false;
    }
    const auto& points = polyline.Points();
    const int n = static_cast<int>(points.size());
    if (n < 3) {
        return false;
    }
    const bool closed = polyline.Closed();
    if (!closed && (vertex <= 0 || vertex >= n - 1)) {
        return false;
    }
    const int prev = closed ? (vertex + n - 1) % n : vertex - 1;
    const int next = closed ? (vertex + 1) % n : vertex + 1;
    polyline.EnsureBulges();
    if (std::abs(polyline.SegmentBulge(static_cast<std::size_t>(prev))) > 1.0e-12
        || std::abs(polyline.SegmentBulge(static_cast<std::size_t>(vertex))) > 1.0e-12) {
        return false;
    }
    const GeoLine s1(points[static_cast<std::size_t>(prev)], points[static_cast<std::size_t>(vertex)]);
    const GeoLine s2(points[static_cast<std::size_t>(vertex)], points[static_cast<std::size_t>(next)]);
    const auto result = GeoFilletChamfer::FilletLines(s1, s2, options.radius, s1.Start(), s2.End());
    if (!result) {
        return false;
    }
    if (!result->arc) {
        return false;
    }
    if (!FilletFitsFiniteSegments(s1, s2, *result)) {
        if (tooLarge) {
            *tooLarge = true;
        }
        return false;
    }
    const GeoPoint t1 = SegmentTangentEnd(result->segment1);
    const GeoPoint t2 = SegmentTangentEnd(result->segment2);
    const int seg1 = prev;
    const int seg2 = vertex;
    return ReplacePolylineSegments(
        polyline, seg1, seg2, t1, t2, FilletBulge(*result), &result->arc->Center());
}

bool ChamferPolylineVertex(
    GeoPolyline& polyline,
    int vertex,
    const ChamferOptions& options,
    bool* tooLarge = nullptr)
{
    if (tooLarge) {
        *tooLarge = false;
    }
    const auto& points = polyline.Points();
    const int n = static_cast<int>(points.size());
    if (n < 3) {
        return false;
    }
    const bool closed = polyline.Closed();
    if (!closed && (vertex <= 0 || vertex >= n - 1)) {
        return false;
    }
    const int prev = closed ? (vertex + n - 1) % n : vertex - 1;
    const int next = closed ? (vertex + 1) % n : vertex + 1;
    polyline.EnsureBulges();
    if (std::abs(polyline.SegmentBulge(static_cast<std::size_t>(prev))) > 1.0e-12
        || std::abs(polyline.SegmentBulge(static_cast<std::size_t>(vertex))) > 1.0e-12) {
        return false;
    }
    const GeoLine s1(points[static_cast<std::size_t>(prev)], points[static_cast<std::size_t>(vertex)]);
    const GeoLine s2(points[static_cast<std::size_t>(vertex)], points[static_cast<std::size_t>(next)]);
    std::optional<ChamferResult> result;
    if (options.method == ChamferMethod::Angle) {
        result = GeoFilletChamfer::ChamferLinesByAngle(
            s1, s2, options.length, options.angleDegrees, s1.Start(), s2.End());
    } else {
        result = GeoFilletChamfer::ChamferLines(
            s1, s2, options.distance1, options.distance2, s1.Start(), s2.End());
    }
    if (!result) {
        return false;
    }
    if (Distance2D(result->chamfer.Start(), result->chamfer.End()) <= Tolerance::Model) {
        return false;
    }
    if (!ChamferFitsFiniteSegments(s1, s2, *result)) {
        if (tooLarge) {
            *tooLarge = true;
        }
        return false;
    }
    return ReplacePolylineSegments(
        polyline,
        prev,
        vertex,
        result->chamfer.Start(),
        result->chamfer.End(),
        0.0);
}

bool UpdatePolylineSegment(GeoPolyline& polyline, int index, const GeoLine& trimmed)
{
    auto& pts = polyline.Points();
    const int n = static_cast<int>(pts.size());
    if (index < 0 || n < 2) {
        return false;
    }
    const int next = polyline.Closed() ? ((index + 1) % n) : (index + 1);
    if (next < 0 || next >= n) {
        return false;
    }
    GeoPoint& a = pts[static_cast<std::size_t>(index)];
    GeoPoint& b = pts[static_cast<std::size_t>(next)];
    const bool aOn = DistancePointToSegment(a, trimmed.Start(), trimmed.End()) <= Tolerance::Model * 50.0;
    const bool bOn = DistancePointToSegment(b, trimmed.Start(), trimmed.End()) <= Tolerance::Model * 50.0;
    if (aOn && !bOn) {
        b = Distance2D(a, trimmed.Start()) >= Distance2D(a, trimmed.End()) ? trimmed.Start() : trimmed.End();
    } else if (bOn && !aOn) {
        a = Distance2D(b, trimmed.Start()) >= Distance2D(b, trimmed.End()) ? trimmed.Start() : trimmed.End();
    } else {
        a = trimmed.Start();
        b = trimmed.End();
    }
    return true;
}

bool AppendTrimmedLoopOrSegment(
    CornerEditOutput& output,
    const Entity& entity,
    const GeoPoint& pick,
    const EditedSegment& trimmed)
{
    auto loop = PolylineLikeFromEntity(entity);
    if (loop) {
        const int index = ClosestSegmentIndex(loop->Points(), pick, loop->Closed());
        if (index < 0 || trimmed.type != EditedSegment::Type::Line) {
            return false;
        }
        if (!UpdatePolylineSegment(*loop, index, trimmed.line)) {
            return false;
        }
        output.removeIds.push_back(entity.Id());
        auto created = std::make_unique<PolylineEntity>(IdGenerator::Next(), std::move(*loop));
        CopyEntityProperties(entity, *created);
        output.created.push_back(std::move(created));
        return true;
    }
    output.removeIds.push_back(entity.Id());
    AppendEditedSegment(output.created, entity, trimmed);
    return true;
}

std::optional<CornerEditOutput> FinishMixedLoopCorner(
    const Entity& entity1,
    const Entity& entity2,
    EntityId id1,
    EntityId id2,
    const GeoPoint& pick1,
    const GeoPoint& pick2,
    const FilletResult* fillet,
    const ChamferResult* chamfer,
    bool trim)
{
    const auto loop1 = PolylineLikeFromEntity(entity1);
    const auto loop2 = PolylineLikeFromEntity(entity2);
    if (!loop1 && !loop2) {
        return std::nullopt;
    }

    CornerEditOutput output;
    auto appendCorner = [&]() {
        if (fillet && fillet->arc) {
            AppendArcEntity(output.created, entity1, *fillet->arc);
        } else if (chamfer
            && Distance2D(chamfer->chamfer.Start(), chamfer->chamfer.End()) > Tolerance::Model) {
            auto line = std::make_unique<LineEntity>(IdGenerator::Next(), chamfer->chamfer);
            CopyEntityProperties(entity1, *line);
            output.created.push_back(std::move(line));
        }
    };

    if (!trim) {
        appendCorner();
        if (output.created.empty()) {
            return std::nullopt;
        }
        return output;
    }

    const EditedSegment& s1 = fillet ? fillet->segment1 : chamfer->segment1;
    const EditedSegment& s2 = fillet ? fillet->segment2 : chamfer->segment2;
    if (!AppendTrimmedLoopOrSegment(output, entity1, pick1, s1)) {
        return std::nullopt;
    }
    if (id1 != id2 && !AppendTrimmedLoopOrSegment(output, entity2, pick2, s2)) {
        return std::nullopt;
    }
    appendCorner();
    std::sort(output.removeIds.begin(), output.removeIds.end());
    output.removeIds.erase(
        std::unique(output.removeIds.begin(), output.removeIds.end()),
        output.removeIds.end());
    if (output.created.empty()) {
        return std::nullopt;
    }
    return output;
}

} // namespace

std::optional<GeoLine> EditKernel::PickedLinearEdge(const Entity& entity, const GeoPoint& pick)
{
    const auto edge = ResolveEdge(entity, pick);
    if (!edge || edge->kind != EdgeKind::Line) {
        return std::nullopt;
    }
    return edge->line;
}

std::optional<CornerEditOutput> EditKernel::FilletEntities(
    const CadDocument& document,
    EntityId id1,
    EntityId id2,
    const FilletOptions& options,
    const GeoPoint& pick1,
    const GeoPoint& pick2,
    CornerEditError* error)
{
    SetCornerError(error, CornerEditError::NoSolution);
    const Entity* entity1 = document.FindEntity(id1);
    const Entity* entity2 = document.FindEntity(id2);
    if (!entity1 || !entity2) {
        return std::nullopt;
    }

    if (id1 == id2) {
        const auto loop = PolylineLikeFromEntity(*entity1);
        if (loop) {
            const auto segments = AdjacentPolylineSegments(
                loop->Points(), pick1, pick2, loop->Closed());
            if (!segments) {
                if (ImpliedPolylineCornerUnfitFillet(*loop, pick1, pick2, options.radius)) {
                    SetCornerError(error, CornerEditError::RadiusTooLarge);
                }
                return std::nullopt;
            }
            const auto result = GeoFilletChamfer::FilletLines(
                segments->first, segments->second, options.radius, pick1, pick2);
            if (!result) {
                return std::nullopt;
            }
            if (!FilletFitsFiniteSegments(segments->first, segments->second, *result)) {
                SetCornerError(error, CornerEditError::RadiusTooLarge);
                return std::nullopt;
            }
            if (loop->Closed() && options.trim && options.radius > Tolerance::Model) {
                const int i1 = ClosestSegmentIndex(loop->Points(), pick1, true);
                const int i2 = ClosestSegmentIndex(loop->Points(), pick2, true);
                const int vertex = SharedPolylineVertexIndex(loop->Points(), i1, i2, true);
                if (vertex >= 0) {
                    GeoPolyline updated = *loop;
                    if (FilletPolylineVertex(updated, vertex, options)) {
                        CornerEditOutput output;
                        output.removeIds = {id1};
                        auto created = std::make_unique<PolylineEntity>(IdGenerator::Next(), std::move(updated));
                        CopyEntityProperties(*entity1, *created);
                        output.created.push_back(std::move(created));
                        SetCornerError(error, CornerEditError::None);
                        return output;
                    }
                }
            }
            auto built = BuildPolylineInPlace(
                *entity1, *loop, pick1, pick2, &*result, nullptr, options.trim);
            if (built) {
                SetCornerError(error, CornerEditError::None);
            }
            return built;
        }
    }

    const auto edge1 = ResolveEdge(*entity1, pick1);
    const auto edge2 = ResolveEdge(*entity2, pick2);
    if (!edge1 || !edge2) {
        return std::nullopt;
    }
    const auto result = ComputeFillet(*edge1, *edge2, options.radius, pick1, pick2);
    if (!result) {
        return std::nullopt;
    }
    if (!FiniteEntityAcceptsTangent(*entity1, pick1, SegmentTangentEnd(result->segment1))
        || !FiniteEntityAcceptsTangent(*entity2, pick2, SegmentTangentEnd(result->segment2))) {
        SetCornerError(error, CornerEditError::RadiusTooLarge);
        return std::nullopt;
    }

    if (auto mixed = FinishMixedLoopCorner(
            *entity1, *entity2, id1, id2, pick1, pick2, &*result, nullptr, options.trim)) {
        SetCornerError(error, CornerEditError::None);
        return mixed;
    }

    CornerEditOutput output;
    if (options.trim) {
        output.removeIds = UniqueEntityIds(id1, id2);
    }
    AppendFilletResult(output.created, *entity1, *result, options.trim);
    if (output.created.empty()) {
        return std::nullopt;
    }
    SetCornerError(error, CornerEditError::None);
    return output;
}

std::optional<CornerEditOutput> EditKernel::ChamferEntities(
    const CadDocument& document,
    EntityId id1,
    EntityId id2,
    const ChamferOptions& options,
    const GeoPoint& pick1,
    const GeoPoint& pick2,
    CornerEditError* error)
{
    SetCornerError(error, CornerEditError::NoSolution);
    const Entity* entity1 = document.FindEntity(id1);
    const Entity* entity2 = document.FindEntity(id2);
    if (!entity1 || !entity2) {
        return std::nullopt;
    }

    if (id1 == id2) {
        const auto loop = PolylineLikeFromEntity(*entity1);
        if (loop) {
            const auto segments = AdjacentPolylineSegments(
                loop->Points(), pick1, pick2, loop->Closed());
            if (!segments) {
                if (ImpliedPolylineCornerUnfitChamfer(*loop, pick1, pick2, options)) {
                    SetCornerError(error, CornerEditError::DistanceTooLarge);
                }
                return std::nullopt;
            }
            std::optional<ChamferResult> result;
            if (options.method == ChamferMethod::Angle) {
                result = GeoFilletChamfer::ChamferLinesByAngle(
                    segments->first, segments->second, options.length, options.angleDegrees, pick1, pick2);
            } else {
                result = GeoFilletChamfer::ChamferLines(
                    segments->first, segments->second, options.distance1, options.distance2, pick1, pick2);
            }
            if (!result) {
                return std::nullopt;
            }
            if (!ChamferFitsFiniteSegments(segments->first, segments->second, *result)) {
                SetCornerError(error, CornerEditError::DistanceTooLarge);
                return std::nullopt;
            }
            auto built = BuildPolylineInPlace(
                *entity1, *loop, pick1, pick2, nullptr, &*result, options.trim);
            if (built) {
                SetCornerError(error, CornerEditError::None);
            }
            return built;
        }
    }

    const auto edge1 = ResolveEdge(*entity1, pick1);
    const auto edge2 = ResolveEdge(*entity2, pick2);
    if (!edge1 || !edge2) {
        return std::nullopt;
    }
    const auto result = ComputeChamfer(*edge1, *edge2, options, pick1, pick2);
    if (!result) {
        return std::nullopt;
    }
    if (!FiniteEntityAcceptsTangent(*entity1, pick1, result->chamfer.Start())
        || !FiniteEntityAcceptsTangent(*entity2, pick2, result->chamfer.End())) {
        SetCornerError(error, CornerEditError::DistanceTooLarge);
        return std::nullopt;
    }

    if (auto mixed = FinishMixedLoopCorner(
            *entity1, *entity2, id1, id2, pick1, pick2, nullptr, &*result, options.trim)) {
        SetCornerError(error, CornerEditError::None);
        return mixed;
    }

    CornerEditOutput output;
    if (options.trim) {
        output.removeIds = UniqueEntityIds(id1, id2);
    }
    AppendChamferResult(output.created, *entity1, *result, options.trim);
    if (output.created.empty()) {
        return std::nullopt;
    }
    SetCornerError(error, CornerEditError::None);
    return output;
}

std::optional<CornerEditOutput> EditKernel::FilletPolyline(
    const CadDocument& document,
    EntityId id,
    const FilletOptions& options,
    CornerEditError* error)
{
    SetCornerError(error, CornerEditError::NoSolution);
    const Entity* entity = document.FindEntity(id);
    if (!entity) {
        return std::nullopt;
    }
    auto loop = PolylineLikeFromEntity(*entity);
    if (!loop) {
        return std::nullopt;
    }
    GeoPolyline polyline = std::move(*loop);
    if (polyline.Points().size() < 3) {
        return std::nullopt;
    }
    bool any = false;
    bool tooLarge = false;
    const auto tryVertex = [&](int vertex) {
        bool vertexTooLarge = false;
        if (FilletPolylineVertex(polyline, vertex, options, &vertexTooLarge)) {
            any = true;
        }
        tooLarge = tooLarge || vertexTooLarge;
    };
    if (polyline.Closed()) {
        for (int i = static_cast<int>(polyline.Points().size()) - 1; i >= 0; --i) {
            tryVertex(i);
        }
    } else {
        for (int i = static_cast<int>(polyline.Points().size()) - 2; i >= 1; --i) {
            tryVertex(i);
        }
    }
    if (!any) {
        SetCornerError(
            error,
            tooLarge ? CornerEditError::RadiusTooLarge : CornerEditError::NoSolution);
        return std::nullopt;
    }
    if (!options.trim) {
        return std::nullopt;
    }
    CornerEditOutput output;
    output.removeIds = {id};
    auto created = std::make_unique<PolylineEntity>(IdGenerator::Next(), std::move(polyline));
    CopyEntityProperties(*entity, *created);
    output.created.push_back(std::move(created));
    SetCornerError(error, CornerEditError::None);
    return output;
}

std::optional<CornerEditOutput> EditKernel::ChamferPolyline(
    const CadDocument& document,
    EntityId id,
    const ChamferOptions& options,
    CornerEditError* error)
{
    SetCornerError(error, CornerEditError::NoSolution);
    const Entity* entity = document.FindEntity(id);
    if (!entity) {
        return std::nullopt;
    }
    auto loop = PolylineLikeFromEntity(*entity);
    if (!loop) {
        return std::nullopt;
    }
    GeoPolyline polyline = std::move(*loop);
    if (polyline.Points().size() < 3) {
        return std::nullopt;
    }
    bool any = false;
    bool tooLarge = false;
    const auto tryVertex = [&](int vertex) {
        bool vertexTooLarge = false;
        if (ChamferPolylineVertex(polyline, vertex, options, &vertexTooLarge)) {
            any = true;
        }
        tooLarge = tooLarge || vertexTooLarge;
    };
    if (polyline.Closed()) {
        for (int i = static_cast<int>(polyline.Points().size()) - 1; i >= 0; --i) {
            tryVertex(i);
        }
    } else {
        for (int i = static_cast<int>(polyline.Points().size()) - 2; i >= 1; --i) {
            tryVertex(i);
        }
    }
    if (!any || !options.trim) {
        SetCornerError(
            error,
            (!any && tooLarge) ? CornerEditError::DistanceTooLarge : CornerEditError::NoSolution);
        return std::nullopt;
    }
    CornerEditOutput output;
    output.removeIds = {id};
    auto created = std::make_unique<PolylineEntity>(IdGenerator::Next(), std::move(polyline));
    CopyEntityProperties(*entity, *created);
    output.created.push_back(std::move(created));
    SetCornerError(error, CornerEditError::None);
    return output;
}

bool EditKernel::ApplyGripEdit(CadDocument& document, EntityId id, int gripIndex, const GeoPoint& position)
{
    Entity* entity = document.FindEntity(id);
    if (!entity) {
        return false;
    }

    if (entity->Type() == EntityType::BlockReference
        && gripIndex >= BlockReferenceEntity::kDynamicGripBase) {
        auto& block = static_cast<BlockReferenceEntity&>(*entity);
        const BlockDefinition* def = document.FindBlockDefinition(block.BlockName());
        if (!def || !block.ApplyDynamicGrip(def, gripIndex, position)) {
            return false;
        }
        document.NotifyEntitiesUpdated({id});
        return true;
    }

    GeoPoint oldPosition;
    bool hasOldPosition = false;
    for (const GripInfo& grip : entity->Grips()) {
        if (grip.index == gripIndex && grip.kind == GripKind::Vertex) {
            oldPosition = grip.position;
            hasOldPosition = true;
            break;
        }
    }

    entity->SetGrip(gripIndex, position);

    std::vector<EntityId> changed{id};
    if (hasOldPosition) {
        // Looser than Tolerance::Model so slightly-mismatched rectangle corners
        // (especially after prior edits) still weld together.
        constexpr double kWeldTol = 1.0e-4;
        for (const auto& other : document.Entities()) {
            if (!other || other->Id() == id) {
                continue;
            }
            if (!other->IsSelected() && document.SelectedEntityIds().size() > 1) {
                continue;
            }

            bool moved = false;
            for (const GripInfo& grip : other->Grips()) {
                if (grip.kind != GripKind::Vertex) {
                    continue;
                }
                if (std::abs(grip.position.X() - oldPosition.X()) > kWeldTol
                    || std::abs(grip.position.Y() - oldPosition.Y()) > kWeldTol) {
                    continue;
                }
                // Snap exactly to the drag target so the corner is truly shared.
                other->SetGrip(grip.index, position);
                moved = true;
            }
            if (moved) {
                changed.push_back(other->Id());
            }
        }
    }

    document.NotifyEntitiesUpdated(changed);
    return true;
}

} // namespace cad
