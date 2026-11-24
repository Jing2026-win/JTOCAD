#include "Geometry/DimensionGeometry.h"

#include "Base/Tolerance.h"
#include "DataModel/DimensionEntity.h"
#include "Geometry/GeoAlgorithms.h"
#include "Geometry/GeoShape.h"
#include <QRectF>
#include <algorithm>
#include <cmath>
#include <limits>

namespace cad {

namespace {

GeoPoint ExtensionStartModel(
    const GeoPoint& origin,
    const GeoPoint& dimPoint,
    const double extensionOffset)
{
    const double dx = dimPoint.X() - origin.X();
    const double dy = dimPoint.Y() - origin.Y();
    const double length = std::hypot(dx, dy);
    if (length <= Tolerance::Model || extensionOffset <= 0.0) {
        return origin;
    }
    return GeoPoint(
        origin.X() + dx * (extensionOffset / length),
        origin.Y() + dy * (extensionOffset / length));
}

bool PointInsideModelRect(const GeoPoint& point, const QRectF& rect, const double tolerance)
{
    return point.X() >= rect.left() - tolerance
        && point.X() <= rect.right() + tolerance
        && point.Y() >= rect.top() - tolerance
        && point.Y() <= rect.bottom() + tolerance;
}

bool SegmentIntersectsModelRect(
    const GeoPoint& start,
    const GeoPoint& end,
    const QRectF& rect,
    const double tolerance)
{
    const double xmin = rect.left() - tolerance;
    const double xmax = rect.right() + tolerance;
    const double ymin = rect.top() - tolerance;
    const double ymax = rect.bottom() + tolerance;

    const double x0 = start.X();
    const double y0 = start.Y();
    const double dx = end.X() - x0;
    const double dy = end.Y() - y0;

    double t0 = 0.0;
    double t1 = 1.0;

    auto clip = [&](const double p, const double q) {
        if (std::abs(p) <= Tolerance::Model) {
            return q >= -Tolerance::Model;
        }
        const double r = q / p;
        if (p < 0.0) {
            if (r > t1) {
                return false;
            }
            if (r > t0) {
                t0 = r;
            }
        } else {
            if (r < t0) {
                return false;
            }
            if (r < t1) {
                t1 = r;
            }
        }
        return true;
    };

    if (!clip(-dx, x0 - xmin)) {
        return false;
    }
    if (!clip(dx, xmax - x0)) {
        return false;
    }
    if (!clip(-dy, y0 - ymin)) {
        return false;
    }
    if (!clip(dy, ymax - y0)) {
        return false;
    }

    return t0 <= t1 + Tolerance::Model;
}

bool SegmentFullyInsideModelRect(
    const GeoPoint& start,
    const GeoPoint& end,
    const QRectF& rect,
    const double tolerance)
{
    return PointInsideModelRect(start, rect, tolerance)
        && PointInsideModelRect(end, rect, tolerance);
}

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
    return Distance2D(
        point,
        GeoPoint(start.X() + t * vx, start.Y() + t * vy));
}

} // namespace

LinearDimensionOrientation ResolveLinearOrientation(
    const GeoPoint& first,
    const GeoPoint& second,
    const GeoPoint& placement,
    const std::optional<LinearDimensionOrientation>& forced)
{
    if (forced.has_value()) {
        return forced.value();
    }

    const double segDx = std::abs(second.X() - first.X());
    const double segDy = std::abs(second.Y() - first.Y());
    if (segDx > segDy + Tolerance::Model) {
        return LinearDimensionOrientation::Horizontal;
    }
    if (segDy > segDx + Tolerance::Model) {
        return LinearDimensionOrientation::Vertical;
    }

    const double midX = (first.X() + second.X()) * 0.5;
    const double midY = (first.Y() + second.Y()) * 0.5;
    const double placeDx = std::abs(placement.X() - midX);
    const double placeDy = std::abs(placement.Y() - midY);
    return placeDy >= placeDx
        ? LinearDimensionOrientation::Horizontal
        : LinearDimensionOrientation::Vertical;
}

LinearDimModelLayout BuildLinearDimModelLayout(
    const GeoPoint& first,
    const GeoPoint& second,
    const GeoPoint& textPosition,
    const DimensionStyle& style,
    const std::optional<LinearDimensionOrientation>& forcedOrientation)
{
    LinearDimModelLayout layout;
    layout.orientation = ResolveLinearOrientation(first, second, textPosition, forcedOrientation);

    if (layout.orientation == LinearDimensionOrientation::Horizontal) {
        const double dimY = textPosition.Y();
        const GeoPoint dimPoint1(first.X(), dimY);
        const GeoPoint dimPoint2(second.X(), dimY);
        layout.ext1Start = ExtensionStartModel(first, dimPoint1, style.extensionOffset);
        layout.ext1End = dimPoint1;
        layout.ext2Start = ExtensionStartModel(second, dimPoint2, style.extensionOffset);
        layout.ext2End = dimPoint2;
        layout.dimStart = dimPoint1;
        layout.dimEnd = dimPoint2;
        layout.measureValue = std::abs(second.X() - first.X());

        const double midY = (first.Y() + second.Y()) * 0.5;
        const double gapSign = dimY >= midY ? 1.0 : -1.0;
        layout.textGapSign = gapSign;
        const double textGap = std::max(style.dimensionLineGap, style.textHeight * 0.75);
        layout.textAnchor = GeoPoint(
            (dimPoint1.X() + dimPoint2.X()) * 0.5,
            dimY + gapSign * textGap);
    } else {
        const double dimX = textPosition.X();
        const GeoPoint dimPoint1(dimX, first.Y());
        const GeoPoint dimPoint2(dimX, second.Y());
        layout.ext1Start = ExtensionStartModel(first, dimPoint1, style.extensionOffset);
        layout.ext1End = dimPoint1;
        layout.ext2Start = ExtensionStartModel(second, dimPoint2, style.extensionOffset);
        layout.ext2End = dimPoint2;
        layout.dimStart = dimPoint1;
        layout.dimEnd = dimPoint2;
        layout.measureValue = std::abs(second.Y() - first.Y());

        const double midX = (first.X() + second.X()) * 0.5;
        const double gapSign = dimX >= midX ? 1.0 : -1.0;
        layout.textGapSign = gapSign;
        const double textGap = std::max(style.dimensionLineGap, style.textHeight * 0.75);
        layout.textAnchor = GeoPoint(
            dimX + gapSign * textGap,
            (dimPoint1.Y() + dimPoint2.Y()) * 0.5);
    }

    return layout;
}

namespace {

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

bool IsAngleInArcSpan(double angle, double start, double end)
{
    angle = NormalizeAnglePositive(angle);
    start = NormalizeAnglePositive(start);
    end = NormalizeAnglePositive(end);
    if (start <= end + Tolerance::Model) {
        return angle + Tolerance::Model >= start && angle <= end + Tolerance::Model;
    }
    return angle + Tolerance::Model >= start || angle <= end + Tolerance::Model;
}

double AngularDistancePositive(double from, double to)
{
    from = NormalizeAnglePositive(from);
    to = NormalizeAnglePositive(to);
    double span = to - from;
    if (span < 0.0) {
        span += kTwoPi;
    }
    return span;
}

bool NeedsRadiusBaseline(double angleRadians)
{
    const double degrees = std::abs(std::fmod(angleRadians * 180.0 / 3.14159265358979323846, 90.0));
    return degrees > 8.0 && degrees < 82.0;
}

} // namespace

double ResolveRadiusAttachmentAngle(
    double angleRadians,
    double arcStartAngleRadians,
    double arcEndAngleRadians)
{
    if (IsAngleInArcSpan(angleRadians, arcStartAngleRadians, arcEndAngleRadians)) {
        return angleRadians;
    }

    const double startDistance = AngularDistancePositive(arcStartAngleRadians, angleRadians);
    const double endDistance = AngularDistancePositive(angleRadians, arcEndAngleRadians);
    const double wrapStartDistance = AngularDistancePositive(angleRadians, arcStartAngleRadians);
    const double wrapEndDistance = AngularDistancePositive(arcEndAngleRadians, angleRadians);
    const double minStart = std::min(startDistance, wrapStartDistance);
    const double minEnd = std::min(endDistance, wrapEndDistance);
    return minStart <= minEnd ? arcStartAngleRadians : arcEndAngleRadians;
}

GeoPoint ComputeRadiusAttachmentPoint(
    const GeoPoint& center,
    double radius,
    const GeoPoint& placement,
    const bool hasArcSpan,
    double arcStartAngleRadians,
    double arcEndAngleRadians)
{
    double angle = std::atan2(placement.Y() - center.Y(), placement.X() - center.X());
    if (hasArcSpan) {
        angle = ResolveRadiusAttachmentAngle(angle, arcStartAngleRadians, arcEndAngleRadians);
    }
    return GeoPoint(
        center.X() + std::cos(angle) * radius,
        center.Y() + std::sin(angle) * radius);
}

RadiusDimModelLayout BuildRadiusDimModelLayout(
    const GeoPoint& center,
    const GeoPoint& attachment,
    const GeoPoint& textPosition,
    const DimensionStyle& style)
{
    RadiusDimModelLayout layout;
    layout.center = center;
    layout.attachment = attachment;
    layout.measureValue = Distance2D(center, attachment);
    layout.showCenterLeader = style.showRadiusCenterLeader;

    double dx = textPosition.X() - center.X();
    double dy = textPosition.Y() - center.Y();
    double placementDistance = std::hypot(dx, dy);
    if (placementDistance <= Tolerance::Model) {
        dx = attachment.X() - center.X();
        dy = attachment.Y() - center.Y();
        placementDistance = layout.measureValue;
    }

    const double invLen = 1.0 / std::max(placementDistance, Tolerance::Model);
    const double ux = dx * invLen;
    const double uy = dy * invLen;
    layout.radialAngleRadians = std::atan2(uy, ux);

    const double gap = std::max(style.radiusExtensionGap, 0.0);
    layout.dimLineStart = GeoPoint(
        attachment.X() + ux * gap,
        attachment.Y() + uy * gap);

    const bool outside = placementDistance > layout.measureValue * 0.85;
    layout.hasBaseline = outside && NeedsRadiusBaseline(layout.radialAngleRadians);

    const double textGap = std::max(style.dimensionLineGap, style.textHeight * 0.75);
    if (layout.hasBaseline) {
        const double kneeDistance = layout.measureValue + gap + std::max(style.textHeight * 1.5, textGap);
        layout.dimLineEnd = GeoPoint(
            center.X() + ux * kneeDistance,
            center.Y() + uy * kneeDistance);
        layout.baselineEnd = GeoPoint(textPosition.X(), layout.dimLineEnd.Y());
        layout.textAnchor = GeoPoint(
            textPosition.X() + (textPosition.X() >= layout.baselineEnd.X() ? textGap : -textGap),
            layout.baselineEnd.Y());
    } else {
        const double outerDistance = std::max(placementDistance, layout.measureValue + gap + textGap);
        layout.dimLineEnd = GeoPoint(
            center.X() + ux * outerDistance,
            center.Y() + uy * outerDistance);
        layout.textAnchor = GeoPoint(
            layout.dimLineEnd.X() + ux * textGap * 0.35,
            layout.dimLineEnd.Y() + uy * textGap * 0.35);
    }

    return layout;
}

RadiusDimModelLayout BuildRadiusDimModelLayout(
    const DimensionEntity& dimension,
    const DimensionStyle& style)
{
    return BuildRadiusDimModelLayout(
        dimension.First(),
        dimension.Second(),
        dimension.TextPosition(),
        style);
}

JoggedRadiusDimModelLayout BuildJoggedRadiusDimModelLayout(
    const GeoPoint& trueCenter,
    const GeoPoint& centerOverride,
    const GeoPoint& dimLineLocation,
    const GeoPoint& jogLocation,
    const DimensionStyle& style,
    const bool hasArcSpan,
    const double arcStartAngleRadians,
    const double arcEndAngleRadians,
    const double radius)
{
    JoggedRadiusDimModelLayout layout;
    layout.centerOverride = centerOverride;
    layout.measureValue = radius;

    const GeoPoint attachment = radius > Tolerance::Model
        ? ComputeRadiusAttachmentPoint(
            trueCenter,
            radius,
            dimLineLocation,
            hasArcSpan,
            arcStartAngleRadians,
            arcEndAngleRadians)
        : trueCenter;
    layout.attachment = attachment;
    layout.measureValue = radius > Tolerance::Model ? radius : Distance2D(trueCenter, attachment);

    double dx = attachment.X() - trueCenter.X();
    double dy = attachment.Y() - trueCenter.Y();
    const double radialLen = std::hypot(dx, dy);
    if (radialLen <= Tolerance::Model) {
        layout.dimLineStart = attachment;
        layout.radialBreak = attachment;
        layout.jogVertexNear = centerOverride;
        layout.jogVertexFar = attachment;
        layout.textAnchor = dimLineLocation;
        return layout;
    }

    const double ux = dx / radialLen;
    const double uy = dy / radialLen;
    layout.radialAngleRadians = std::atan2(uy, ux);
    layout.textRotationRadians = layout.radialAngleRadians;

    const double gap = std::max(style.radiusExtensionGap, 0.0);
    layout.dimLineStart = GeoPoint(
        attachment.X() + ux * gap,
        attachment.Y() + uy * gap);

    const double jogHeight = std::max(
        style.textHeight * std::max(style.radiusJogHeightFactor, 0.1),
        style.textHeight * 0.5);
    const double jogAngle = std::clamp(
        style.radiusJogAngleDegrees,
        5.0,
        90.0) * 3.14159265358979323846 / 180.0;

    const double cross = ux * (centerOverride.Y() - jogLocation.Y())
        - uy * (centerOverride.X() - jogLocation.X());
    const double sign = cross >= 0.0 ? 1.0 : -1.0;
    const double cosA = std::cos(sign * jogAngle);
    const double sinA = std::sin(sign * jogAngle);
    const double tx = ux * cosA - uy * sinA;
    const double ty = ux * sinA + uy * cosA;

    const double radialParam = (jogLocation.X() - trueCenter.X()) * ux
        + (jogLocation.Y() - trueCenter.Y()) * uy;
    layout.radialBreak = GeoPoint(
        trueCenter.X() + ux * radialParam,
        trueCenter.Y() + uy * radialParam);

    layout.jogVertexFar = layout.radialBreak;
    layout.jogVertexNear = GeoPoint(
        layout.jogVertexFar.X() - tx * jogHeight,
        layout.jogVertexFar.Y() - ty * jogHeight);

    const double textGap = std::max(style.dimensionLineGap, style.textHeight * 0.75);
    const double textParam = (dimLineLocation.X() - trueCenter.X()) * ux
        + (dimLineLocation.Y() - trueCenter.Y()) * uy;
    const double minTextParam = gap + textGap * 0.35;
    const double maxTextParam = std::max(minTextParam, radialParam - textGap * 0.25);
    const double clampedTextParam = std::clamp(textParam, minTextParam, maxTextParam);
    layout.textAnchor = GeoPoint(
        trueCenter.X() + ux * clampedTextParam,
        trueCenter.Y() + uy * clampedTextParam);

    const double placementDistance = std::hypot(
        dimLineLocation.X() - trueCenter.X(),
        dimLineLocation.Y() - trueCenter.Y());
    layout.hasArcExtension = placementDistance > layout.measureValue * 0.85
        && NeedsRadiusBaseline(layout.radialAngleRadians);
    if (layout.hasArcExtension) {
        layout.arcExtensionEnd = GeoPoint(dimLineLocation.X(), layout.textAnchor.Y());
    }

    return layout;
}

JoggedRadiusDimModelLayout BuildJoggedRadiusDimModelLayout(
    const DimensionEntity& dimension,
    const DimensionStyle& style)
{
    return BuildJoggedRadiusDimModelLayout(
        dimension.First(),
        dimension.JogCenter(),
        dimension.TextPosition(),
        dimension.JogPoint(),
        style,
        dimension.HasArcSpan(),
        dimension.ArcStartAngleRadians(),
        dimension.ArcEndAngleRadians(),
        dimension.RadiusValue());
}

DiameterDimModelLayout BuildDiameterDimModelLayout(
    const GeoPoint& center,
    const GeoPoint& nearAttachment,
    const GeoPoint& textPosition,
    const DimensionStyle& style)
{
    DiameterDimModelLayout layout;
    layout.center = center;
    layout.nearAttachment = nearAttachment;
    const double radius = Distance2D(center, nearAttachment);
    layout.measureValue = radius * 2.0;
    layout.showFarExtension = style.showRadiusCenterLeader;

    double dx = nearAttachment.X() - center.X();
    double dy = nearAttachment.Y() - center.Y();
    const double radialLen = std::hypot(dx, dy);
    if (radialLen <= Tolerance::Model) {
        layout.farAttachment = nearAttachment;
        layout.dimLineStart = nearAttachment;
        layout.dimLineEnd = textPosition;
        layout.textAnchor = textPosition;
        return layout;
    }

    const double ux = dx / radialLen;
    const double uy = dy / radialLen;
    layout.radialAngleRadians = std::atan2(uy, ux);
    layout.farAttachment = GeoPoint(
        center.X() - ux * radius,
        center.Y() - uy * radius);

    const double gap = std::max(style.radiusExtensionGap, 0.0);
    layout.dimLineStart = GeoPoint(
        layout.farAttachment.X() + ux * gap,
        layout.farAttachment.Y() + uy * gap);

    double pdx = textPosition.X() - center.X();
    double pdy = textPosition.Y() - center.Y();
    double placementDistance = std::hypot(pdx, pdy);
    if (placementDistance <= Tolerance::Model) {
        placementDistance = radius;
    }

    const double textGap = std::max(style.dimensionLineGap, style.textHeight * 0.75);
    const bool outside = placementDistance > radius * 0.85;
    layout.hasBaseline = outside && NeedsRadiusBaseline(layout.radialAngleRadians);

    if (layout.hasBaseline) {
        const double kneeDistance = radius + gap + std::max(style.textHeight * 1.5, textGap);
        layout.dimLineEnd = GeoPoint(
            center.X() + ux * kneeDistance,
            center.Y() + uy * kneeDistance);
        layout.baselineEnd = GeoPoint(textPosition.X(), layout.dimLineEnd.Y());
        layout.textAnchor = GeoPoint(
            textPosition.X() + (textPosition.X() >= layout.baselineEnd.X() ? textGap : -textGap),
            layout.baselineEnd.Y());
    } else {
        const double outerDistance = std::max(placementDistance, radius + gap + textGap);
        layout.dimLineEnd = GeoPoint(
            center.X() + ux * outerDistance,
            center.Y() + uy * outerDistance);
        layout.textAnchor = GeoPoint(
            layout.dimLineEnd.X() + ux * textGap * 0.35,
            layout.dimLineEnd.Y() + uy * textGap * 0.35);
    }

    return layout;
}

DiameterDimModelLayout BuildDiameterDimModelLayout(
    const DimensionEntity& dimension,
    const DimensionStyle& style)
{
    return BuildDiameterDimModelLayout(
        dimension.First(),
        dimension.Second(),
        dimension.TextPosition(),
        style);
}

AlignedDimModelLayout BuildAlignedDimModelLayout(
    const GeoPoint& first,
    const GeoPoint& second,
    const GeoPoint& textPosition,
    const DimensionStyle& style)
{
    AlignedDimModelLayout layout;
    const double vx = second.X() - first.X();
    const double vy = second.Y() - first.Y();
    layout.measureValue = std::hypot(vx, vy);
    layout.dimAngleRadians = std::atan2(vy, vx);

    if (layout.measureValue <= Tolerance::Model) {
        layout.ext1Start = first;
        layout.ext1End = first;
        layout.ext2Start = second;
        layout.ext2End = second;
        layout.dimStart = first;
        layout.dimEnd = second;
        layout.textAnchor = textPosition;
        return layout;
    }

    const GeoPoint mid(
        (first.X() + second.X()) * 0.5,
        (first.Y() + second.Y()) * 0.5);
    const double invLen = 1.0 / layout.measureValue;
    const GeoPoint axisUnit(vx * invLen, vy * invLen);
    const GeoPoint normal(-axisUnit.Y(), axisUnit.X());

    const GeoPoint toPlacement(
        textPosition.X() - mid.X(),
        textPosition.Y() - mid.Y());
    const double perpDistance = toPlacement.X() * normal.X() + toPlacement.Y() * normal.Y();
    layout.textGapSign = perpDistance >= 0.0 ? 1.0 : -1.0;

    const GeoPoint offset(normal.X() * perpDistance, normal.Y() * perpDistance);
    const GeoPoint dimStart(first.X() + offset.X(), first.Y() + offset.Y());
    const GeoPoint dimEnd(second.X() + offset.X(), second.Y() + offset.Y());

    layout.ext1Start = ExtensionStartModel(first, dimStart, style.extensionOffset);
    layout.ext1End = dimStart;
    layout.ext2Start = ExtensionStartModel(second, dimEnd, style.extensionOffset);
    layout.ext2End = dimEnd;
    layout.dimStart = dimStart;
    layout.dimEnd = dimEnd;

    const GeoPoint dimMid(
        (dimStart.X() + dimEnd.X()) * 0.5,
        (dimStart.Y() + dimEnd.Y()) * 0.5);
    const double textGap = std::max(style.dimensionLineGap, style.textHeight * 0.75);
    layout.textAnchor = GeoPoint(
        dimMid.X() + normal.X() * textGap * layout.textGapSign,
        dimMid.Y() + normal.Y() * textGap * layout.textGapSign);

    return layout;
}

AlignedDimModelLayout BuildAlignedDimModelLayout(
    const DimensionEntity& dimension,
    const DimensionStyle& style)
{
    return BuildAlignedDimModelLayout(
        dimension.First(),
        dimension.Second(),
        dimension.TextPosition(),
        style);
}

double NormalizeAngle0To2Pi(double angleRadians)
{
    const double twoPi = 2.0 * 3.14159265358979323846;
    angleRadians = std::fmod(angleRadians, twoPi);
    if (angleRadians < 0.0) {
        angleRadians += twoPi;
    }
    return angleRadians;
}

double CCWSweepRadians(double fromRadians, double toRadians)
{
    fromRadians = NormalizeAngle0To2Pi(fromRadians);
    toRadians = NormalizeAngle0To2Pi(toRadians);
    double sweep = toRadians - fromRadians;
    if (sweep < 0.0) {
        sweep += 2.0 * 3.14159265358979323846;
    }
    return sweep;
}

bool AngleInCCWSweep(double testRadians, double fromRadians, double toRadians)
{
    return CCWSweepRadians(fromRadians, testRadians) <= CCWSweepRadians(fromRadians, toRadians) + Tolerance::Angular;
}

double PointAngleRadians(const GeoPoint& origin, const GeoPoint& point)
{
    return std::atan2(point.Y() - origin.Y(), point.X() - origin.X());
}

GeoPoint PolarOffset(const GeoPoint& center, double angleRadians, double radius)
{
    return GeoPoint(
        center.X() + std::cos(angleRadians) * radius,
        center.Y() + std::sin(angleRadians) * radius);
}

AngularDimModelLayout BuildAngularDimModelLayout(
    const GeoPoint& vertex,
    const GeoPoint& arm1,
    const GeoPoint& arm2,
    const GeoPoint& textPosition,
    const DimensionStyle& style,
    const bool hasArcSpan,
    const double arcStartAngleRadians,
    const double arcEndAngleRadians,
    const std::optional<int> /*quadrantLock*/,
    const double sourceArcRadius)
{
    AngularDimModelLayout layout;
    layout.vertex = vertex;

    const double arm1Angle = hasArcSpan
        ? NormalizeAngle0To2Pi(arcStartAngleRadians)
        : PointAngleRadians(vertex, arm1);
    const double arm2Angle = hasArcSpan
        ? NormalizeAngle0To2Pi(arcEndAngleRadians)
        : PointAngleRadians(vertex, arm2);
    layout.arm1AngleRadians = arm1Angle;
    layout.arm2AngleRadians = arm2Angle;

    const double pickRadius = std::hypot(
        textPosition.X() - vertex.X(),
        textPosition.Y() - vertex.Y());
    const double minDimRadius = style.extensionOffset + style.textHeight * 0.5;
    double dimRadius = std::max(pickRadius, minDimRadius);

    const double armSweep = CCWSweepRadians(arm1Angle, arm2Angle);
    double startAngle = arm1Angle;
    double sweepAngle = armSweep;

    // AutoCAD DAN：标注弧始终落在鼠标所在象限/扇区——内侧或外侧均根据
    // placementAngle 选择 arm1→arm2 或补角，不能在内侧时固定用弧跨度。
    double placementAngle = PointAngleRadians(vertex, textPosition);
    if (pickRadius <= Tolerance::Model) {
        // 鼠标在圆心处时 atan2 无意义，用两臂角平分线避免退化几何与箭头杂线。
        placementAngle = NormalizeAngle0To2Pi(arm1Angle + armSweep * 0.5);
    }
    if (AngleInCCWSweep(placementAngle, arm1Angle, arm2Angle)) {
        startAngle = arm1Angle;
        sweepAngle = armSweep;
    } else {
        startAngle = arm2Angle;
        sweepAngle = CCWSweepRadians(arm2Angle, arm1Angle);
    }

    const double extAngle1 = startAngle;
    const double extAngle2 = NormalizeAngle0To2Pi(startAngle + sweepAngle);

    layout.arcStartAngleRadians = startAngle;
    layout.sweepAngleRadians = sweepAngle;
    layout.dimArcRadius = dimRadius;
    layout.measureValue = sweepAngle * 180.0 / 3.14159265358979323846;

    const bool outwardFromSource = !hasArcSpan
        || sourceArcRadius <= Tolerance::Model
        || pickRadius >= sourceArcRadius;

    const auto extensionStart = [&](const double angle) {
        if (hasArcSpan && sourceArcRadius > Tolerance::Model) {
            return PolarOffset(vertex, angle, sourceArcRadius);
        }
        return ExtensionStartModel(
            vertex,
            PolarOffset(vertex, angle, dimRadius),
            style.extensionOffset);
    };

    layout.ext1Start = extensionStart(extAngle1);
    layout.ext1End = PolarOffset(vertex, extAngle1, dimRadius);
    layout.ext2Start = extensionStart(extAngle2);
    layout.ext2End = PolarOffset(vertex, extAngle2, dimRadius);

    layout.arcStart = layout.ext1End;
    layout.arcEnd = layout.ext2End;

    const double midAngle = startAngle + sweepAngle * 0.5;
    const double textGap = std::max(style.dimensionLineGap, style.textHeight * 0.75);
    const double textRadius = outwardFromSource
        ? dimRadius + textGap
        : std::max(minDimRadius, dimRadius - textGap);
    layout.textAnchor = PolarOffset(vertex, midAngle, textRadius);

    return layout;
}

AngularDimModelLayout BuildAngularDimModelLayout(
    const DimensionEntity& dimension,
    const DimensionStyle& style)
{
    return BuildAngularDimModelLayout(
        dimension.First(),
        dimension.Second(),
        dimension.JogCenter(),
        dimension.TextPosition(),
        style,
        dimension.HasArcSpan(),
        dimension.ArcStartAngleRadians(),
        dimension.ArcEndAngleRadians(),
        std::nullopt,
        dimension.HasArcSpan() ? dimension.RadiusValue() : 0.0);
}

LinearDimModelLayout BuildLinearDimModelLayout(
    const DimensionEntity& dimension,
    const DimensionStyle& style)
{
    std::optional<LinearDimensionOrientation> forced;
    if (dimension.Kind() == DimensionKind::Linear) {
        forced = dimension.LinearOrientation();
    }
    return BuildLinearDimModelLayout(
        dimension.First(),
        dimension.Second(),
        dimension.TextPosition(),
        style,
        forced);
}

LinearDimensionOrientation ResolveOrdinateOrientation(
    const GeoPoint& feature,
    const GeoPoint& textPosition,
    const std::optional<LinearDimensionOrientation>& forced)
{
    if (forced.has_value()) {
        return forced.value();
    }

    const double dx = std::abs(textPosition.X() - feature.X());
    const double dy = std::abs(textPosition.Y() - feature.Y());
    if (dy > dx + Tolerance::Model) {
        return LinearDimensionOrientation::Vertical;
    }
    if (dx > dy + Tolerance::Model) {
        return LinearDimensionOrientation::Horizontal;
    }
    return LinearDimensionOrientation::Vertical;
}

OrdinateDimModelLayout BuildOrdinateDimModelLayout(
    const GeoPoint& feature,
    const GeoPoint& textPosition,
    const GeoPoint& ucsOrigin,
    const DimensionStyle& style,
    const std::optional<LinearDimensionOrientation>& forcedOrientation)
{
    OrdinateDimModelLayout layout;
    layout.feature = feature;
    layout.leaderEnd = textPosition;
    layout.ucsOrigin = ucsOrigin;
    layout.orientation = ResolveOrdinateOrientation(feature, textPosition, forcedOrientation);

    const double textGap = std::max(style.dimensionLineGap, style.textHeight * 0.75);

    if (layout.orientation == LinearDimensionOrientation::Vertical) {
        layout.measureValue = feature.X() - ucsOrigin.X();
        layout.leaderJog = GeoPoint(feature.X(), textPosition.Y());
        layout.textGapSign = textPosition.X() >= feature.X() ? 1.0 : -1.0;
        layout.textAnchor = GeoPoint(
            textPosition.X() + layout.textGapSign * textGap,
            textPosition.Y());
    } else {
        layout.measureValue = feature.Y() - ucsOrigin.Y();
        layout.leaderJog = GeoPoint(textPosition.X(), feature.Y());
        layout.textGapSign = textPosition.Y() >= feature.Y() ? 1.0 : -1.0;
        layout.textAnchor = GeoPoint(
            textPosition.X(),
            textPosition.Y() + layout.textGapSign * textGap);
    }

    return layout;
}

OrdinateDimModelLayout BuildOrdinateDimModelLayout(
    const DimensionEntity& dimension,
    const DimensionStyle& style)
{
    std::optional<LinearDimensionOrientation> forced;
    if (dimension.Kind() == DimensionKind::Ordinate) {
        forced = dimension.LinearOrientation();
    }
    return BuildOrdinateDimModelLayout(
        dimension.First(),
        dimension.TextPosition(),
        dimension.Second(),
        style,
        forced);
}

std::vector<GeoLine> DimensionModelSegments(
    const DimensionEntity& dimension,
    const DimensionStyle& style)
{
    if (dimension.Kind() == DimensionKind::Radius) {
        const RadiusDimModelLayout layout = BuildRadiusDimModelLayout(dimension, style);
        std::vector<GeoLine> segments;
        if (layout.showCenterLeader) {
            segments.emplace_back(layout.center, layout.attachment);
        }
        segments.emplace_back(layout.dimLineStart, layout.dimLineEnd);
        if (layout.hasBaseline) {
            segments.emplace_back(layout.dimLineEnd, layout.baselineEnd);
        }
        return segments;
    }

    if (dimension.Kind() == DimensionKind::JoggedRadius) {
        const JoggedRadiusDimModelLayout layout = BuildJoggedRadiusDimModelLayout(dimension, style);
        std::vector<GeoLine> segments;
        segments.emplace_back(layout.centerOverride, layout.jogVertexNear);
        segments.emplace_back(layout.jogVertexNear, layout.jogVertexFar);
        segments.emplace_back(layout.jogVertexFar, layout.radialBreak);
        segments.emplace_back(layout.radialBreak, layout.dimLineStart);
        segments.emplace_back(layout.dimLineStart, layout.attachment);
        if (layout.hasArcExtension) {
            segments.emplace_back(layout.textAnchor, layout.arcExtensionEnd);
        }
        return segments;
    }

    if (dimension.Kind() == DimensionKind::Diameter) {
        const DiameterDimModelLayout layout = BuildDiameterDimModelLayout(dimension, style);
        std::vector<GeoLine> segments;
        if (layout.showFarExtension) {
            segments.emplace_back(layout.farAttachment, layout.nearAttachment);
        }
        segments.emplace_back(layout.dimLineStart, layout.dimLineEnd);
        if (layout.hasBaseline) {
            segments.emplace_back(layout.dimLineEnd, layout.baselineEnd);
        }
        return segments;
    }

    if (dimension.Kind() == DimensionKind::Angular) {
        const AngularDimModelLayout layout = BuildAngularDimModelLayout(dimension, style);
        std::vector<GeoLine> segments;
        segments.emplace_back(layout.ext1Start, layout.ext1End);
        segments.emplace_back(layout.ext2Start, layout.ext2End);
        const int arcSegments = std::max(8, static_cast<int>(layout.sweepAngleRadians * 12.0));
        GeoPoint previous = layout.arcStart;
        for (int i = 1; i <= arcSegments; ++i) {
            const double t = static_cast<double>(i) / static_cast<double>(arcSegments);
            const double angle = layout.arcStartAngleRadians + layout.sweepAngleRadians * t;
            const GeoPoint current = PolarOffset(layout.vertex, angle, layout.dimArcRadius);
            segments.emplace_back(previous, current);
            previous = current;
        }
        return segments;
    }

    if (dimension.Kind() == DimensionKind::Linear) {
        const LinearDimModelLayout layout = BuildLinearDimModelLayout(dimension, style);
        return {
            GeoLine(layout.ext1Start, layout.ext1End),
            GeoLine(layout.ext2Start, layout.ext2End),
            GeoLine(layout.dimStart, layout.dimEnd),
        };
    }

    if (dimension.Kind() == DimensionKind::Ordinate) {
        const OrdinateDimModelLayout layout = BuildOrdinateDimModelLayout(dimension, style);
        return {
            GeoLine(layout.feature, layout.leaderJog),
            GeoLine(layout.leaderJog, layout.leaderEnd),
        };
    }

    const AlignedDimModelLayout layout = BuildAlignedDimModelLayout(dimension, style);
    return {
        GeoLine(layout.ext1Start, layout.ext1End),
        GeoLine(layout.ext2Start, layout.ext2End),
        GeoLine(layout.dimStart, layout.dimEnd),
    };
}

GeoBounds DimensionModelBounds(
    const DimensionEntity& dimension,
    const DimensionStyle& style)
{
    GeoBounds bounds;
    auto includePoint = [&](const GeoPoint& point) {
        if (!bounds.valid) {
            bounds = GeoBounds{point.X(), point.Y(), point.X(), point.Y(), true};
            return;
        }
        bounds.minX = std::min(bounds.minX, point.X());
        bounds.minY = std::min(bounds.minY, point.Y());
        bounds.maxX = std::max(bounds.maxX, point.X());
        bounds.maxY = std::max(bounds.maxY, point.Y());
    };

    for (const GeoLine& segment : DimensionModelSegments(dimension, style)) {
        includePoint(segment.Start());
        includePoint(segment.End());
    }

    const double textPad = std::max(style.textHeight, style.dimensionLineGap);
    if (dimension.Kind() == DimensionKind::Radius) {
        const RadiusDimModelLayout layout = BuildRadiusDimModelLayout(dimension, style);
        includePoint(layout.textAnchor);
        includePoint(GeoPoint(layout.textAnchor.X() + textPad, layout.textAnchor.Y() + textPad));
        includePoint(GeoPoint(layout.textAnchor.X() - textPad, layout.textAnchor.Y() - textPad));
    } else if (dimension.Kind() == DimensionKind::JoggedRadius) {
        const JoggedRadiusDimModelLayout layout = BuildJoggedRadiusDimModelLayout(dimension, style);
        includePoint(layout.textAnchor);
        includePoint(layout.centerOverride);
        includePoint(GeoPoint(layout.textAnchor.X() + textPad, layout.textAnchor.Y() + textPad));
        includePoint(GeoPoint(layout.textAnchor.X() - textPad, layout.textAnchor.Y() - textPad));
    } else if (dimension.Kind() == DimensionKind::Diameter) {
        const DiameterDimModelLayout layout = BuildDiameterDimModelLayout(dimension, style);
        includePoint(layout.textAnchor);
        includePoint(GeoPoint(layout.textAnchor.X() + textPad, layout.textAnchor.Y() + textPad));
        includePoint(GeoPoint(layout.textAnchor.X() - textPad, layout.textAnchor.Y() - textPad));
    } else if (dimension.Kind() == DimensionKind::Angular) {
        const AngularDimModelLayout layout = BuildAngularDimModelLayout(dimension, style);
        includePoint(layout.textAnchor);
        includePoint(GeoPoint(layout.textAnchor.X() + textPad, layout.textAnchor.Y() + textPad));
        includePoint(GeoPoint(layout.textAnchor.X() - textPad, layout.textAnchor.Y() - textPad));
        // 箭头三角可能超出弧线段包围盒，需额外留白否则留下残影。
        const double arrowPad = std::max(style.arrowSize * 1.5, textPad);
        const double angles[] = {
            layout.arcStartAngleRadians,
            layout.arcStartAngleRadians + layout.sweepAngleRadians,
            layout.arcStartAngleRadians + layout.sweepAngleRadians * 0.5,
        };
        for (const double angle : angles) {
            const GeoPoint onArc = PolarOffset(layout.vertex, angle, layout.dimArcRadius);
            includePoint(onArc);
            includePoint(GeoPoint(onArc.X() + arrowPad, onArc.Y() + arrowPad));
            includePoint(GeoPoint(onArc.X() - arrowPad, onArc.Y() - arrowPad));
        }
        if (dimension.HasArcSpan() && dimension.RadiusValue() > Tolerance::Model) {
            const double srcR = dimension.RadiusValue();
            const double extAngles[] = {
                layout.arm1AngleRadians,
                layout.arm2AngleRadians,
            };
            for (const double angle : extAngles) {
                const GeoPoint onSrc = PolarOffset(layout.vertex, angle, srcR);
                includePoint(onSrc);
            }
        }
    } else if (dimension.Kind() == DimensionKind::Linear) {
        const LinearDimModelLayout layout = BuildLinearDimModelLayout(dimension, style);
        includePoint(layout.textAnchor);
        includePoint(GeoPoint(layout.textAnchor.X() + textPad, layout.textAnchor.Y() + textPad));
        includePoint(GeoPoint(layout.textAnchor.X() - textPad, layout.textAnchor.Y() - textPad));
    } else if (dimension.Kind() == DimensionKind::Ordinate) {
        const OrdinateDimModelLayout layout = BuildOrdinateDimModelLayout(dimension, style);
        includePoint(layout.textAnchor);
        includePoint(GeoPoint(layout.textAnchor.X() + textPad, layout.textAnchor.Y() + textPad));
        includePoint(GeoPoint(layout.textAnchor.X() - textPad, layout.textAnchor.Y() - textPad));
    } else {
        const AlignedDimModelLayout layout = BuildAlignedDimModelLayout(dimension, style);
        includePoint(layout.textAnchor);
        includePoint(GeoPoint(layout.textAnchor.X() + textPad, layout.textAnchor.Y() + textPad));
        includePoint(GeoPoint(layout.textAnchor.X() - textPad, layout.textAnchor.Y() - textPad));
    }

    return bounds;
}

double DimensionDistanceToModelPoint(
    const DimensionEntity& dimension,
    const GeoPoint& point,
    const DimensionStyle& style,
    const double hitToleranceModel)
{
    double bestDistance = std::numeric_limits<double>::max();
    for (const GeoLine& segment : DimensionModelSegments(dimension, style)) {
        bestDistance = std::min(bestDistance, DistancePointToSegment(point, segment.Start(), segment.End()));
    }
    if (hitToleranceModel > Tolerance::Model && bestDistance <= hitToleranceModel) {
        return 0.0;
    }
    return bestDistance;
}

bool DimensionCrossesModelRect(
    const DimensionEntity& dimension,
    const QRectF& modelRect,
    const double tolerance,
    const DimensionStyle& style)
{
    const QRectF rect = modelRect.normalized();
    for (const GeoLine& segment : DimensionModelSegments(dimension, style)) {
        if (SegmentIntersectsModelRect(segment.Start(), segment.End(), rect, tolerance)) {
            return true;
        }
    }
    return false;
}

bool DimensionFullyInsideModelRect(
    const DimensionEntity& dimension,
    const QRectF& modelRect,
    const double tolerance,
    const DimensionStyle& style)
{
    const QRectF rect = modelRect.normalized();
    for (const GeoLine& segment : DimensionModelSegments(dimension, style)) {
        if (!SegmentFullyInsideModelRect(segment.Start(), segment.End(), rect, tolerance)) {
            return false;
        }
    }
    return true;
}

} // namespace cad
