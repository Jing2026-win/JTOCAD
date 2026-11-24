#include "View/GeoShapePainter.h"

#include "Base/Tolerance.h"
#include "Geometry/ClipperUtil.h"
#include "Geometry/DrawAlgorithms.h"
#include "Geometry/GeoPoint.h"
#include <QPainter>
#include <QPainterPath>
#include <QtMath>
#include <algorithm>
#include <cmath>
#include <vector>

namespace cad {

namespace {

QPointF ToScenePoint(const GeoPoint& point)
{
    return QPointF(point.X(), -point.Y());
}

GeoBounds ResolveClipBounds(const GeoShape& shape, const GeoBounds* clipBounds)
{
    if (clipBounds && clipBounds->valid) {
        return *clipBounds;
    }

    GeoBounds bounds = shape.Bounds();
    if (!bounds.valid) {
        constexpr double kFallbackExtension = 1.0e6;
        return GeoBounds{-kFallbackExtension, -kFallbackExtension, kFallbackExtension, kFallbackExtension, true};
    }
    return bounds;
}

double RadiansToDegrees(double radians)
{
    return qRadiansToDegrees(radians);
}

QPointF ArcStartScenePoint(const GeoArc& arc)
{
    const double start = arc.StartAngleRadians();
    return QPointF(
        arc.Center().X() + std::cos(start) * arc.Radius(),
        -(arc.Center().Y() + std::sin(start) * arc.Radius()));
}

QRectF ArcSceneRect(const GeoArc& arc)
{
    const QPointF center = ToScenePoint(arc.Center());
    const double radius = arc.Radius();
    return QRectF(
        center.x() - radius,
        center.y() - radius,
        radius * 2.0,
        radius * 2.0);
}

void AppendTrueArcToPath(QPainterPath& path, const GeoArc& arc)
{
    const double start = arc.StartAngleRadians();
    const double span = arc.EndAngleRadians() - start;
    if (std::abs(span) <= Tolerance::Model || arc.Radius() <= Tolerance::Model) {
        return;
    }

    // Scene points already use ToScenePoint (Y-up → Y-down). Qt arcTo uses the
    // same 3 o'clock origin and CCW-positive convention as CAD, so pass model
    // start/sweep un-negated. Negating the sweep previously mirrored the arc
    // into the opposite quadrant (FILLET preview looked like an interior notch).
    path.moveTo(ArcStartScenePoint(arc));
    path.arcTo(ArcSceneRect(arc), RadiansToDegrees(start), RadiansToDegrees(span));
}

void AppendTessellatedArcToPath(
    QPainterPath& path,
    const GeoArc& arc,
    const CurveDisplayOptions& display)
{
    const double start = arc.StartAngleRadians();
    const double span = arc.EndAngleRadians() - start;
    if (std::abs(span) <= Tolerance::Model || arc.Radius() <= Tolerance::Model) {
        return;
    }

    const int segments = ViewDisplaySettings::ArcDisplaySegments(
        arc.Radius(),
        span,
        display.pixelsPerModelUnit,
        display.viewRes);

    for (int i = 0; i <= segments; ++i) {
        const double t = static_cast<double>(i) / static_cast<double>(segments);
        const double angle = start + span * t;
        const QPointF pt(
            arc.Center().X() + std::cos(angle) * arc.Radius(),
            -(arc.Center().Y() + std::sin(angle) * arc.Radius()));
        if (i == 0) {
            path.moveTo(pt);
        } else {
            path.lineTo(pt);
        }
    }
}

void AppendCircleToPath(QPainterPath& path, const GeoCircle& circle, const CurveDisplayOptions* display)
{
    if (display == nullptr || display->useTrueCurves) {
        const QPointF center = ToScenePoint(circle.Center());
        path.addEllipse(center, circle.Radius(), circle.Radius());
        return;
    }

    const GeoArc fullArc(circle.Center(), circle.Radius(), 0.0, 2.0 * 3.14159265358979323846);
    AppendTessellatedArcToPath(path, fullArc, *display);
    path.closeSubpath();
}

void AppendArcToPath(QPainterPath& path, const GeoArc& arc, const CurveDisplayOptions* display)
{
    if (display == nullptr || display->useTrueCurves) {
        AppendTrueArcToPath(path, arc);
        return;
    }
    AppendTessellatedArcToPath(path, arc, *display);
}

void AppendEllipseToPath(
    QPainterPath& path,
    const GeoShapeEllipse& ellipse,
    const GeoBounds& bounds,
    const CurveDisplayOptions* display)
{
    GeoShape geoShape;
    geoShape.AddEllipse(ellipse);

    int segments = 72;
    if (display != nullptr) {
        if (display->useTrueCurves) {
            segments = 256;
        } else {
            segments = ViewDisplaySettings::EllipseDisplaySegments(
                std::max(ellipse.majorRadius, ellipse.minorRadius),
                display->pixelsPerModelUnit,
                display->viewRes);
        }
    }

    const std::vector<GeoPoint> points = geoShape.DiscretizeEllipse(ellipse, segments);
    if (points.empty()) {
        return;
    }
    path.moveTo(ToScenePoint(points.front()));
    for (std::size_t i = 1; i < points.size(); ++i) {
        path.lineTo(ToScenePoint(points[i]));
    }
    path.closeSubpath();
    Q_UNUSED(bounds);
}

void AppendSplineToPath(
    QPainterPath& path,
    const GeoSpline& spline,
    const CurveDisplayOptions* display)
{
    Q_UNUSED(display);
    const GeoPoint* startTangent = spline.StartTangent() ? &*spline.StartTangent() : nullptr;
    const GeoPoint* endTangent = spline.EndTangent() ? &*spline.EndTangent() : nullptr;
    const std::vector<DrawAlgorithms::CubicBezierSegment> beziers =
        DrawAlgorithms::SplineCubicBeziers(
            spline.Points(), spline.Mode(), spline.Degree(), spline.Knots(), startTangent, endTangent);
    if (!beziers.empty()) {
        path.moveTo(ToScenePoint(beziers.front().p0));
        for (const auto& seg : beziers) {
            path.cubicTo(
                ToScenePoint(seg.p1),
                ToScenePoint(seg.p2),
                ToScenePoint(seg.p3));
        }
        return;
    }

    const std::vector<GeoPoint> samples =
        DrawAlgorithms::SplinePreviewPoints(spline.Points(), spline.Mode(), spline.Degree(), spline.Knots());
    if (samples.empty()) {
        return;
    }
    path.moveTo(ToScenePoint(samples.front()));
    for (std::size_t i = 1; i < samples.size(); ++i) {
        path.lineTo(ToScenePoint(samples[i]));
    }
}

CurveDisplayOptions ResolveDisplayOptions(const CurveDisplayOptions* display, QPainter* painter)
{
    CurveDisplayOptions options;
    if (display != nullptr) {
        options = *display;
    } else if (painter != nullptr) {
        const QTransform t = painter->worldTransform();
        options.pixelsPerModelUnit = std::max(std::abs(t.m11()), 1.0e-12);
        options.useTrueCurves = false;
    }
    options.viewRes = ViewDisplaySettings::ClampViewRes(options.viewRes);
    return options;
}

} // namespace

QPainterPath GeoShapePainter::BuildPath(
    const GeoShape& shape,
    const GeoBounds* clipBounds,
    const CurveDisplayOptions* display)
{
    QPainterPath path;
    const GeoBounds bounds = ResolveClipBounds(shape, clipBounds);
    // Hit-testing defaults to true curves for accurate picking.
    CurveDisplayOptions hitOptions;
    hitOptions.useTrueCurves = true;
    const CurveDisplayOptions* effective = display != nullptr ? display : &hitOptions;

    for (const GeoPoint& point : shape.Points()) {
        if (path.isEmpty()) {
            path.moveTo(ToScenePoint(point));
        } else {
            path.addEllipse(ToScenePoint(point), 2.0, 2.0);
        }
    }

    for (const GeoLine& segment : shape.Segments()) {
        path.moveTo(ToScenePoint(segment.Start()));
        path.lineTo(ToScenePoint(segment.End()));
    }

    if (shape.HasInfiniteLines()) {
        for (const GeoLine& segment : shape.ToSegments(bounds)) {
            path.moveTo(ToScenePoint(segment.Start()));
            path.lineTo(ToScenePoint(segment.End()));
        }
    }

    for (const GeoCircle& circle : shape.Circles()) {
        AppendCircleToPath(path, circle, effective);
    }

    for (const GeoArc& arc : shape.Arcs()) {
        AppendArcToPath(path, arc, effective);
    }

    for (const GeoShapeEllipse& ellipse : shape.Ellipses()) {
        AppendEllipseToPath(path, ellipse, bounds, effective);
    }

    for (std::size_t i = 0; i < shape.Polylines().size(); ++i) {
        const auto& polyline = shape.Polylines()[i];
        for (std::size_t j = 1; j < polyline.size(); ++j) {
            path.moveTo(ToScenePoint(polyline[j - 1]));
            path.lineTo(ToScenePoint(polyline[j]));
        }
        if (i < shape.PolylineClosed().size() && shape.PolylineClosed()[i] && polyline.size() >= 2) {
            path.moveTo(ToScenePoint(polyline.back()));
            path.lineTo(ToScenePoint(polyline.front()));
        }
    }

    for (const GeoSpline& spline : shape.Splines()) {
        AppendSplineToPath(path, spline, effective);
    }

    if (shape.Fill()) {
        const auto& fill = *shape.Fill();
        const auto& outer = fill.region.Outer();
        if (!outer.empty()) {
            QPainterPath regionPath(ToScenePoint(outer.front()));
            for (std::size_t i = 1; i < outer.size(); ++i) {
                regionPath.lineTo(ToScenePoint(outer[i]));
            }
            regionPath.closeSubpath();
            path.addPath(regionPath);
        }
    }

    return path;
}

void GeoShapePainter::Paint(
    QPainter* painter,
    const GeoShape& shape,
    const QPen& pen,
    const GeoBounds* clipBounds,
    const CurveDisplayOptions* display)
{
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setPen(pen);
    painter->setBrush(Qt::NoBrush);

    const GeoBounds bounds = ResolveClipBounds(shape, clipBounds);
    const CurveDisplayOptions options = ResolveDisplayOptions(display, painter);

    for (const GeoPoint& point : shape.Points()) {
        painter->drawEllipse(ToScenePoint(point), 2.0, 2.0);
    }

    QPainterPath strokePath;
    if (shape.HasInfiniteLines()) {
        for (const GeoLine& segment : shape.ToSegments(bounds)) {
            strokePath.moveTo(ToScenePoint(segment.Start()));
            strokePath.lineTo(ToScenePoint(segment.End()));
        }
    } else {
        for (const GeoLine& segment : shape.Segments()) {
            strokePath.moveTo(ToScenePoint(segment.Start()));
            strokePath.lineTo(ToScenePoint(segment.End()));
        }
        for (const GeoArc& arc : shape.Arcs()) {
            AppendArcToPath(strokePath, arc, &options);
        }
        for (const GeoCircle& circle : shape.Circles()) {
            AppendCircleToPath(strokePath, circle, &options);
        }
    }

    for (const GeoShapeEllipse& ellipse : shape.Ellipses()) {
        AppendEllipseToPath(strokePath, ellipse, bounds, &options);
    }

    for (std::size_t i = 0; i < shape.Polylines().size(); ++i) {
        const auto& polyline = shape.Polylines()[i];
        if (polyline.empty()) {
            continue;
        }
        strokePath.moveTo(ToScenePoint(polyline.front()));
        for (std::size_t j = 1; j < polyline.size(); ++j) {
            strokePath.lineTo(ToScenePoint(polyline[j]));
        }
        if (i < shape.PolylineClosed().size() && shape.PolylineClosed()[i] && polyline.size() >= 2) {
            strokePath.closeSubpath();
        }
    }

    for (const GeoSpline& spline : shape.Splines()) {
        AppendSplineToPath(strokePath, spline, &options);
    }

    if (!strokePath.isEmpty()) {
        painter->drawPath(strokePath);
    }

    if (shape.Fill()) {
        const auto& fill = *shape.Fill();
        const auto& outer = fill.region.Outer();
        if (!outer.empty()) {
            QPainterPath regionPath(ToScenePoint(outer.front()));
            for (std::size_t i = 1; i < outer.size(); ++i) {
                regionPath.lineTo(ToScenePoint(outer[i]));
            }
            regionPath.closeSubpath();
            if (fill.mode == FillMode::Solid) {
                painter->fillPath(regionPath, QColor(80, 120, 200, 80));
            } else if (fill.mode == FillMode::Hatch) {
                const std::vector<GeoLine> hatchLines = ClipperUtil::GenerateHatch(
                    fill.region,
                    fill.hatchSpacing,
                    fill.hatchAngleRadians);
                QPen hatchPen(pen);
                hatchPen.setColor(QColor(100, 140, 210, 180));
                painter->setPen(hatchPen);
                for (const GeoLine& line : hatchLines) {
                    if (Distance2D(line.Start(), line.End()) > Tolerance::Model) {
                        painter->drawLine(ToScenePoint(line.Start()), ToScenePoint(line.End()));
                    }
                }
                painter->setPen(pen);
            }
            painter->drawPath(regionPath);
        }
    }
}

} // namespace cad

//} // namespace cad






