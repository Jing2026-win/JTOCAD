#include "View/DimensionRenderUtil.h"

#include "Base/Tolerance.h"
#include "DataModel/DimensionEntity.h"
#include "Geometry/DimensionGeometry.h"
#include <QFont>
#include <QFontMetricsF>
#include <QPainter>
#include <QPainterPath>
#include <QGraphicsSimpleTextItem>
#include <QtMath>
#include <cmath>

namespace cad {

QFont DimensionFont(const DimensionStyle& style)
{
    QFont font(QStringLiteral("Arial"));
    font.setPixelSize(qMax(1, qRound(std::max(1.0, style.textHeight))));
    return font;
}

namespace detail {

QPointF ToScene(const GeoPoint& point)
{
    return QPointF(point.X(), -point.Y());
}

QString ToQString(const std::string& text)
{
    return QString::fromUtf8(text.c_str());
}

void DrawArrowHead(QPainter* painter, const QPointF& from, const QPointF& tip, double size)
{
    const QPointF direction = tip - from;
    const double length = std::hypot(direction.x(), direction.y());
    if (length <= Tolerance::Model || size <= 0.0) {
        return;
    }
    const QPointF unit(direction.x() / length, direction.y() / length);
    const QPointF normal(-unit.y(), unit.x());
    const QPointF base = tip - unit * size;
    const QPointF left = base + normal * (size * 0.4);
    const QPointF right = base - normal * (size * 0.4);
    QPainterPath head;
    head.moveTo(tip);
    head.lineTo(left);
    head.lineTo(right);
    head.closeSubpath();
    painter->fillPath(head, painter->pen().color());
}

void DrawTickMark(QPainter* painter, const QPointF& onLine, const QPointF& outward, double size)
{
    const QPointF direction = outward - onLine;
    const double length = std::hypot(direction.x(), direction.y());
    if (length <= Tolerance::Model || size <= 0.0) {
        return;
    }
    const QPointF unit(direction.x() / length, direction.y() / length);
    const QPointF normal(-unit.y(), unit.x());
    const QPointF tip = onLine + unit * size;
    painter->drawLine(onLine - normal * (size * 0.35), tip + normal * (size * 0.35));
}

QString FormatMeasureValue(double value)
{
    return QString::number(value, 'f', 2);
}

LinearDimensionLayout LayoutFromModel(
    const LinearDimModelLayout& modelLayout,
    const std::string& overrideText)
{
    LinearDimensionLayout layout;
    layout.orientation = modelLayout.orientation;
    layout.ext1Start = ToScene(modelLayout.ext1Start);
    layout.ext1End = ToScene(modelLayout.ext1End);
    layout.ext2Start = ToScene(modelLayout.ext2Start);
    layout.ext2End = ToScene(modelLayout.ext2End);
    layout.dimStart = ToScene(modelLayout.dimStart);
    layout.dimEnd = ToScene(modelLayout.dimEnd);
    layout.textPos = ToScene(modelLayout.textAnchor);
    layout.measureValue = modelLayout.measureValue;
    layout.textGapSign = modelLayout.textGapSign;
    layout.label = overrideText.empty()
        ? FormatMeasureValue(modelLayout.measureValue)
        : ToQString(overrideText);
    return layout;
}

double SceneUnitsPerDevicePixel(const QPainter* painter)
{
    const QTransform transform = painter->worldTransform();
    return 1.0 / std::max(std::hypot(transform.m11(), transform.m12()), Tolerance::MinZoom);
}

double MapDeviceLengthToScene(const QPainter* painter, const double deviceLength)
{
    if (deviceLength <= 0.0) {
        return 0.0;
    }
    const QTransform inverted = painter->worldTransform().inverted();
    const QPointF origin = inverted.map(QPointF(0.0, 0.0));
    const QPointF mapped = inverted.map(QPointF(deviceLength, 0.0));
    return std::hypot(mapped.x() - origin.x(), mapped.y() - origin.y());
}

double MapDeviceOffsetToScene(const QPainter* painter, const QPointF& deviceDelta)
{
    const QTransform inverted = painter->worldTransform().inverted();
    const QPointF origin = inverted.map(QPointF(0.0, 0.0));
    const QPointF mapped = inverted.map(deviceDelta);
    return std::hypot(mapped.x() - origin.x(), mapped.y() - origin.y());
}

void DrawBrokenDimensionLine(
    QPainter* painter,
    const QPointF& start,
    const QPointF& end,
    const QPointF& textCenter,
    double textWidthScene,
    double breakGapScene)
{
    const QPointF direction = end - start;
    const double length = std::hypot(direction.x(), direction.y());
    if (length <= Tolerance::Model) {
        return;
    }

    const QPointF unit(direction.x() / length, direction.y() / length);
    const double projection = (textCenter.x() - start.x()) * unit.x() + (textCenter.y() - start.y()) * unit.y();
    const QPointF center = start + unit * projection;
    const double halfGap = textWidthScene * 0.5 + breakGapScene;
    const QPointF gapStart = center - unit * halfGap;
    const QPointF gapEnd = center + unit * halfGap;

    const double tGapStart = ((gapStart.x() - start.x()) * unit.x() + (gapStart.y() - start.y()) * unit.y());
    const double tGapEnd = ((gapEnd.x() - start.x()) * unit.x() + (gapEnd.y() - start.y()) * unit.y());

    if (tGapStart > 0.0) {
        painter->drawLine(start, gapStart);
    }
    if (length - tGapEnd > 0.0) {
        painter->drawLine(gapEnd, end);
    }
}

void DrawDimensionText(
    QPainter* painter,
    const QPointF& center,
    const QString& label,
    const QFontMetricsF& metrics,
    double rotationDegrees,
    double paddingScene)
{
    painter->save();
    painter->translate(center);
    if (!qFuzzyIsNull(rotationDegrees)) {
        painter->rotate(-rotationDegrees);
    }

    const double pad = std::max(paddingScene, 0.0);
    const QRectF textRect(
        -metrics.horizontalAdvance(label) * 0.5 - pad,
        -metrics.height() * 0.5 - pad,
        metrics.horizontalAdvance(label) + pad * 2.0,
        metrics.height() + pad * 2.0);
    painter->fillRect(textRect, QColor(32, 32, 32));
    painter->drawText(
        QRectF(
            -metrics.horizontalAdvance(label) * 0.5,
            -metrics.height() * 0.5,
            metrics.horizontalAdvance(label),
            metrics.height()),
        Qt::AlignCenter,
        label);
    painter->restore();
}

struct LinearTextPlacement {
    QPointF center;
    double rotationDegrees = 0.0;
    bool breakDimLine = true;
    double breakWidthScene = 0.0;
    double breakGapScene = 0.0;
};

LinearTextPlacement ComputeLinearTextPlacement(
    const LinearDimensionLayout& layout,
    const QFontMetricsF& metrics,
    const DimensionStyle& style)
{
    LinearTextPlacement placement;
    const QPointF dimMid = (layout.dimStart + layout.dimEnd) * 0.5;
    const QPointF dimDir = layout.dimEnd - layout.dimStart;
    const double dimLen = std::hypot(dimDir.x(), dimDir.y());
    if (dimLen <= Tolerance::Model) {
        placement.center = layout.textPos;
        return placement;
    }

    const QPointF unit(dimDir.x() / dimLen, dimDir.y() / dimLen);
    const QPointF normal(-unit.y(), unit.x());

    const double offsetScene = std::max(style.dimensionLineGap, style.textHeight * 0.75);

    const QPointF toText = layout.textPos - dimMid;
    const double dot = toText.x() * normal.x() + toText.y() * normal.y();
    double sideSign = 0.0;
    if (std::abs(dot) > Tolerance::Model) {
        sideSign = dot >= 0.0 ? 1.0 : -1.0;
    } else {
        sideSign = layout.textGapSign >= 0.0 ? 1.0 : -1.0;
    }

    placement.center = dimMid + normal * (sideSign * offsetScene);

    if (layout.orientation == LinearDimensionOrientation::Vertical) {
        placement.breakDimLine = false;
        if (qFuzzyIsNull(layout.textRotationDegrees)) {
            placement.rotationDegrees = sideSign >= 0.0 ? 90.0 : -90.0;
        } else {
            placement.rotationDegrees = layout.textRotationDegrees;
        }
        return placement;
    }

    placement.rotationDegrees = layout.textRotationDegrees;
    placement.breakDimLine = false;
    placement.breakWidthScene = metrics.horizontalAdvance(layout.label);
    placement.breakGapScene = std::max(style.dimensionLineGap, style.textHeight * 0.15);
    return placement;
}

AlignedDimensionLayout LayoutFromAlignedModel(
    const AlignedDimModelLayout& modelLayout,
    const std::string& overrideText)
{
    AlignedDimensionLayout layout;
    layout.ext1Start = ToScene(modelLayout.ext1Start);
    layout.ext1End = ToScene(modelLayout.ext1End);
    layout.ext2Start = ToScene(modelLayout.ext2Start);
    layout.ext2End = ToScene(modelLayout.ext2End);
    layout.dimStart = ToScene(modelLayout.dimStart);
    layout.dimEnd = ToScene(modelLayout.dimEnd);
    layout.textPos = ToScene(modelLayout.textAnchor);
    layout.measureValue = modelLayout.measureValue;
    layout.textGapSign = modelLayout.textGapSign;
    layout.dimRotationDegrees = qRadiansToDegrees(modelLayout.dimAngleRadians);
    layout.label = overrideText.empty()
        ? FormatMeasureValue(modelLayout.measureValue)
        : ToQString(overrideText);
    return layout;
}

struct AlignedTextPlacement {
    QPointF center;
    double rotationDegrees = 0.0;
};

AlignedTextPlacement ComputeAlignedTextPlacement(
    const AlignedDimensionLayout& layout,
    const QFontMetricsF& metrics,
    const DimensionStyle& style)
{
    AlignedTextPlacement placement;
    const QPointF dimMid = (layout.dimStart + layout.dimEnd) * 0.5;
    const QPointF dimDir = layout.dimEnd - layout.dimStart;
    const double dimLen = std::hypot(dimDir.x(), dimDir.y());
    if (dimLen <= Tolerance::Model) {
        placement.center = layout.textPos;
        placement.rotationDegrees = layout.textRotationDegrees;
        return placement;
    }

    const QPointF unit(dimDir.x() / dimLen, dimDir.y() / dimLen);
    const QPointF normal(-unit.y(), unit.x());
    const double offsetScene = std::max(style.dimensionLineGap, style.textHeight * 0.75);

    const QPointF toText = layout.textPos - dimMid;
    const double dot = toText.x() * normal.x() + toText.y() * normal.y();
    double sideSign = layout.textGapSign >= 0.0 ? 1.0 : -1.0;
    if (std::abs(dot) > Tolerance::Model) {
        sideSign = dot >= 0.0 ? 1.0 : -1.0;
    }

    placement.center = dimMid + normal * (sideSign * offsetScene);
    if (qFuzzyIsNull(layout.textRotationDegrees)) {
        placement.rotationDegrees = qRadiansToDegrees(std::atan2(dimDir.y(), dimDir.x()));
    } else {
        placement.rotationDegrees = layout.textRotationDegrees;
    }
    return placement;
}

} // namespace detail

AlignedDimensionLayout BuildAlignedDimensionLayout(
    const GeoPoint& first,
    const GeoPoint& second,
    const GeoPoint& textPosition,
    const DimensionStyle& style,
    const std::string& overrideText,
    const double textRotationRadians)
{
    AlignedDimensionLayout layout = detail::LayoutFromAlignedModel(
        BuildAlignedDimModelLayout(first, second, textPosition, style),
        overrideText);
    layout.textRotationDegrees = qRadiansToDegrees(textRotationRadians);
    return layout;
}

AlignedDimensionLayout BuildAlignedDimensionLayout(
    const DimensionEntity& dimension,
    const DimensionStyle& style)
{
    AlignedDimensionLayout layout = detail::LayoutFromAlignedModel(
        BuildAlignedDimModelLayout(dimension, style),
        dimension.OverrideText());
    layout.textRotationDegrees = qRadiansToDegrees(dimension.TextRotationRadians());
    return layout;
}

QPainterPath AlignedDimensionScenePath(const AlignedDimensionLayout& layout, const DimensionStyle& style)
{
    Q_UNUSED(style);
    QPainterPath path;
    path.moveTo(layout.ext1Start);
    path.lineTo(layout.ext1End);
    path.moveTo(layout.ext2Start);
    path.lineTo(layout.ext2End);
    path.moveTo(layout.dimStart);
    path.lineTo(layout.dimEnd);
    return path;
}

void SetPreviewMeasureLabelPosition(
    QGraphicsSimpleTextItem* item,
    const QPointF& center,
    const double rotationDegrees)
{
    if (!item) {
        return;
    }
    const QRectF textBounds = item->boundingRect();
    item->setTransformOriginPoint(textBounds.center());
    item->setPos(center.x() - textBounds.width() * 0.5, center.y() - textBounds.height() * 0.5);
    item->setRotation(-rotationDegrees);
}

DimensionTextPreview AlignedDimensionTextPreview(
    const AlignedDimensionLayout& layout,
    const DimensionStyle& style)
{
    const QFont font = DimensionFont(style);
    const QFontMetricsF metrics(font);
    const detail::AlignedTextPlacement placement = detail::ComputeAlignedTextPlacement(layout, metrics, style);
    return {placement.center, placement.rotationDegrees};
}

DimensionTextPreview LinearDimensionTextPreview(
    const LinearDimensionLayout& layout,
    const DimensionStyle& style)
{
    const QFont font = DimensionFont(style);
    const QFontMetricsF metrics(font);
    const detail::LinearTextPlacement placement = detail::ComputeLinearTextPlacement(layout, metrics, style);
    return {placement.center, placement.rotationDegrees};
}

LinearDimensionLayout BuildLinearDimensionLayout(
    const GeoPoint& first,
    const GeoPoint& second,
    const GeoPoint& textPosition,
    const DimensionStyle& style,
    const std::optional<LinearDimensionOrientation>& forcedOrientation,
    const std::string& overrideText,
    const double textRotationRadians)
{
    LinearDimensionLayout layout = detail::LayoutFromModel(
        BuildLinearDimModelLayout(first, second, textPosition, style, forcedOrientation),
        overrideText);
    layout.textRotationDegrees = qRadiansToDegrees(textRotationRadians);
    return layout;
}

LinearDimensionLayout BuildLinearDimensionLayout(const DimensionEntity& dimension, const DimensionStyle& style)
{
    std::optional<LinearDimensionOrientation> forced;
    if (dimension.Kind() == DimensionKind::Linear) {
        forced = dimension.LinearOrientation();
    }
    LinearDimensionLayout layout = detail::LayoutFromModel(
        BuildLinearDimModelLayout(dimension, style),
        dimension.OverrideText());
    layout.textRotationDegrees = qRadiansToDegrees(dimension.TextRotationRadians());
    return layout;
}

QPainterPath LinearDimensionScenePath(const LinearDimensionLayout& layout, const DimensionStyle& style)
{
    Q_UNUSED(style);
    QPainterPath path;
    path.moveTo(layout.ext1Start);
    path.lineTo(layout.ext1End);
    path.moveTo(layout.ext2Start);
    path.lineTo(layout.ext2End);
    path.moveTo(layout.dimStart);
    path.lineTo(layout.dimEnd);
    return path;
}

RadiusDimensionLayout LayoutFromRadiusModel(
    const RadiusDimModelLayout& modelLayout,
    const std::string& overrideText)
{
    RadiusDimensionLayout layout;
    layout.center = detail::ToScene(modelLayout.center);
    layout.attachment = detail::ToScene(modelLayout.attachment);
    layout.dimLineStart = detail::ToScene(modelLayout.dimLineStart);
    layout.dimLineEnd = detail::ToScene(modelLayout.dimLineEnd);
    layout.baselineEnd = detail::ToScene(modelLayout.baselineEnd);
    layout.textPos = detail::ToScene(modelLayout.textAnchor);
    layout.hasBaseline = modelLayout.hasBaseline;
    layout.showCenterLeader = modelLayout.showCenterLeader;
    layout.measureValue = modelLayout.measureValue;
    layout.label = overrideText.empty()
        ? QStringLiteral("R%1").arg(detail::FormatMeasureValue(modelLayout.measureValue))
        : detail::ToQString(overrideText);
    return layout;
}

RadiusDimensionLayout BuildRadiusDimensionLayout(
    const GeoPoint& center,
    const GeoPoint& attachment,
    const GeoPoint& textPosition,
    const DimensionStyle& style,
    const std::string& overrideText,
    const double textRotationRadians)
{
    RadiusDimensionLayout layout = LayoutFromRadiusModel(
        BuildRadiusDimModelLayout(center, attachment, textPosition, style),
        overrideText);
    layout.textRotationDegrees = qRadiansToDegrees(textRotationRadians);
    return layout;
}

RadiusDimensionLayout BuildRadiusDimensionLayout(const DimensionEntity& dimension, const DimensionStyle& style)
{
    RadiusDimensionLayout layout = LayoutFromRadiusModel(
        BuildRadiusDimModelLayout(dimension, style),
        dimension.OverrideText());
    layout.textRotationDegrees = qRadiansToDegrees(dimension.TextRotationRadians());
    return layout;
}

QPainterPath RadiusDimensionScenePath(const RadiusDimensionLayout& layout, const DimensionStyle& style)
{
    Q_UNUSED(style);
    QPainterPath path;
    if (layout.showCenterLeader) {
        path.moveTo(layout.center);
        path.lineTo(layout.attachment);
    }
    path.moveTo(layout.dimLineStart);
    path.lineTo(layout.dimLineEnd);
    if (layout.hasBaseline) {
        path.lineTo(layout.baselineEnd);
    }
    return path;
}

DimensionTextPreview RadiusDimensionTextPreview(
    const RadiusDimensionLayout& layout,
    const DimensionStyle& style)
{
    Q_UNUSED(style);
    return {layout.textPos, layout.textRotationDegrees};
}

JoggedRadiusDimensionLayout LayoutFromJoggedRadiusModel(
    const JoggedRadiusDimModelLayout& modelLayout,
    const std::string& overrideText)
{
    JoggedRadiusDimensionLayout layout;
    layout.attachment = detail::ToScene(modelLayout.attachment);
    layout.dimLineStart = detail::ToScene(modelLayout.dimLineStart);
    layout.radialBreak = detail::ToScene(modelLayout.radialBreak);
    layout.jogVertexNear = detail::ToScene(modelLayout.jogVertexNear);
    layout.jogVertexFar = detail::ToScene(modelLayout.jogVertexFar);
    layout.centerOverride = detail::ToScene(modelLayout.centerOverride);
    layout.textPos = detail::ToScene(modelLayout.textAnchor);
    layout.arcExtensionEnd = detail::ToScene(modelLayout.arcExtensionEnd);
    layout.hasArcExtension = modelLayout.hasArcExtension;
    layout.measureValue = modelLayout.measureValue;
    layout.label = overrideText.empty()
        ? QStringLiteral("R%1").arg(detail::FormatMeasureValue(modelLayout.measureValue))
        : detail::ToQString(overrideText);
    layout.textRotationDegrees = qRadiansToDegrees(modelLayout.textRotationRadians);
    return layout;
}

JoggedRadiusDimensionLayout BuildJoggedRadiusDimensionLayout(
    const GeoPoint& trueCenter,
    const GeoPoint& centerOverride,
    const GeoPoint& dimLineLocation,
    const GeoPoint& jogLocation,
    const DimensionStyle& style,
    const std::string& overrideText,
    const double textRotationRadians,
    const bool hasArcSpan,
    const double arcStartAngleRadians,
    const double arcEndAngleRadians,
    const double radius)
{
    JoggedRadiusDimensionLayout layout = LayoutFromJoggedRadiusModel(
        BuildJoggedRadiusDimModelLayout(
            trueCenter,
            centerOverride,
            dimLineLocation,
            jogLocation,
            style,
            hasArcSpan,
            arcStartAngleRadians,
            arcEndAngleRadians,
            radius),
        overrideText);
    if (textRotationRadians != 0.0) {
        layout.textRotationDegrees = qRadiansToDegrees(textRotationRadians);
    }
    return layout;
}

JoggedRadiusDimensionLayout BuildJoggedRadiusDimensionLayout(
    const DimensionEntity& dimension,
    const DimensionStyle& style)
{
    JoggedRadiusDimensionLayout layout = LayoutFromJoggedRadiusModel(
        BuildJoggedRadiusDimModelLayout(dimension, style),
        dimension.OverrideText());
    if (dimension.TextRotationRadians() != 0.0) {
        layout.textRotationDegrees = qRadiansToDegrees(dimension.TextRotationRadians());
    }
    return layout;
}

QPainterPath JoggedRadiusDimensionScenePath(const JoggedRadiusDimensionLayout& layout, const DimensionStyle& style)
{
    Q_UNUSED(style);
    QPainterPath path;
    path.moveTo(layout.centerOverride);
    path.lineTo(layout.jogVertexNear);
    path.lineTo(layout.jogVertexFar);
    path.lineTo(layout.radialBreak);
    path.lineTo(layout.dimLineStart);
    path.lineTo(layout.attachment);
    if (layout.hasArcExtension) {
        path.moveTo(layout.textPos);
        path.lineTo(layout.arcExtensionEnd);
    }
    return path;
}

DimensionTextPreview JoggedRadiusDimensionTextPreview(
    const JoggedRadiusDimensionLayout& layout,
    const DimensionStyle& style)
{
    Q_UNUSED(style);
    return {layout.textPos, layout.textRotationDegrees};
}

DiameterDimensionLayout LayoutFromDiameterModel(
    const DiameterDimModelLayout& modelLayout,
    const std::string& overrideText)
{
    DiameterDimensionLayout layout;
    layout.center = detail::ToScene(modelLayout.center);
    layout.nearAttachment = detail::ToScene(modelLayout.nearAttachment);
    layout.farAttachment = detail::ToScene(modelLayout.farAttachment);
    layout.dimLineStart = detail::ToScene(modelLayout.dimLineStart);
    layout.dimLineEnd = detail::ToScene(modelLayout.dimLineEnd);
    layout.baselineEnd = detail::ToScene(modelLayout.baselineEnd);
    layout.textPos = detail::ToScene(modelLayout.textAnchor);
    layout.hasBaseline = modelLayout.hasBaseline;
    layout.showFarExtension = modelLayout.showFarExtension;
    layout.measureValue = modelLayout.measureValue;
    layout.label = overrideText.empty()
        ? QStringLiteral("\u03C6%1").arg(detail::FormatMeasureValue(modelLayout.measureValue))
        : detail::ToQString(overrideText);
    return layout;
}

DiameterDimensionLayout BuildDiameterDimensionLayout(
    const GeoPoint& center,
    const GeoPoint& nearAttachment,
    const GeoPoint& textPosition,
    const DimensionStyle& style,
    const std::string& overrideText,
    const double textRotationRadians)
{
    DiameterDimensionLayout layout = LayoutFromDiameterModel(
        BuildDiameterDimModelLayout(center, nearAttachment, textPosition, style),
        overrideText);
    layout.textRotationDegrees = qRadiansToDegrees(textRotationRadians);
    return layout;
}

DiameterDimensionLayout BuildDiameterDimensionLayout(
    const DimensionEntity& dimension,
    const DimensionStyle& style)
{
    DiameterDimensionLayout layout = LayoutFromDiameterModel(
        BuildDiameterDimModelLayout(dimension, style),
        dimension.OverrideText());
    layout.textRotationDegrees = qRadiansToDegrees(dimension.TextRotationRadians());
    return layout;
}

QPainterPath DiameterDimensionScenePath(const DiameterDimensionLayout& layout, const DimensionStyle& style)
{
    Q_UNUSED(style);
    QPainterPath path;
    if (layout.showFarExtension) {
        path.moveTo(layout.farAttachment);
        path.lineTo(layout.nearAttachment);
    }
    path.moveTo(layout.dimLineStart);
    path.lineTo(layout.dimLineEnd);
    if (layout.hasBaseline) {
        path.lineTo(layout.baselineEnd);
    }
    return path;
}

DimensionTextPreview DiameterDimensionTextPreview(
    const DiameterDimensionLayout& layout,
    const DimensionStyle& style)
{
    Q_UNUSED(style);
    return {layout.textPos, layout.textRotationDegrees};
}

AngularDimensionLayout LayoutFromAngularModel(
    const AngularDimModelLayout& modelLayout,
    const std::string& overrideText)
{
    AngularDimensionLayout layout;
    layout.vertex = detail::ToScene(modelLayout.vertex);
    layout.ext1Start = detail::ToScene(modelLayout.ext1Start);
    layout.ext1End = detail::ToScene(modelLayout.ext1End);
    layout.ext2Start = detail::ToScene(modelLayout.ext2Start);
    layout.ext2End = detail::ToScene(modelLayout.ext2End);
    layout.arcStart = detail::ToScene(modelLayout.arcStart);
    layout.arcEnd = detail::ToScene(modelLayout.arcEnd);
    layout.textPos = detail::ToScene(modelLayout.textAnchor);
    layout.dimArcRadius = modelLayout.dimArcRadius;
    layout.arcStartAngleRadians = modelLayout.arcStartAngleRadians;
    layout.sweepAngleRadians = modelLayout.sweepAngleRadians;
    layout.measureValue = modelLayout.measureValue;
    layout.label = overrideText.empty()
        ? detail::FormatMeasureValue(modelLayout.measureValue) + QChar(0x00B0)
        : detail::ToQString(overrideText);
    if (!overrideText.empty() && layout.label.contains(QStringLiteral("<>"))) {
        layout.label.replace(
            QStringLiteral("<>"),
            detail::FormatMeasureValue(modelLayout.measureValue));
    }
    return layout;
}

AngularDimensionLayout BuildAngularDimensionLayout(
    const GeoPoint& vertex,
    const GeoPoint& arm1,
    const GeoPoint& arm2,
    const GeoPoint& textPosition,
    const DimensionStyle& style,
    const std::string& overrideText,
    const double textRotationRadians,
    const bool hasArcSpan,
    const double arcStartAngleRadians,
    const double arcEndAngleRadians,
    const double sourceArcRadius)
{
    AngularDimensionLayout layout = LayoutFromAngularModel(
        BuildAngularDimModelLayout(
            vertex,
            arm1,
            arm2,
            textPosition,
            style,
            hasArcSpan,
            arcStartAngleRadians,
            arcEndAngleRadians,
            std::nullopt,
            sourceArcRadius),
        overrideText);
    if (qFuzzyIsNull(textRotationRadians)) {
        const double midAngle = layout.arcStartAngleRadians + layout.sweepAngleRadians * 0.5;
        layout.textRotationDegrees = qRadiansToDegrees(midAngle);
    } else {
        layout.textRotationDegrees = qRadiansToDegrees(textRotationRadians);
    }
    return layout;
}

AngularDimensionLayout BuildAngularDimensionLayout(
    const DimensionEntity& dimension,
    const DimensionStyle& style)
{
    AngularDimensionLayout layout = LayoutFromAngularModel(
        BuildAngularDimModelLayout(dimension, style),
        dimension.OverrideText());
    layout.textRotationDegrees = qRadiansToDegrees(dimension.TextRotationRadians());
    if (qFuzzyIsNull(dimension.TextRotationRadians())) {
        const double midAngle = layout.arcStartAngleRadians + layout.sweepAngleRadians * 0.5;
        layout.textRotationDegrees = qRadiansToDegrees(midAngle);
    }
    return layout;
}

QPainterPath AngularDimensionScenePath(const AngularDimensionLayout& layout, const DimensionStyle& style)
{
    Q_UNUSED(style);
    QPainterPath path;
    path.moveTo(layout.ext1Start);
    path.lineTo(layout.ext1End);
    path.moveTo(layout.ext2Start);
    path.lineTo(layout.ext2End);

    const int segments = std::max(8, static_cast<int>(layout.sweepAngleRadians * 12.0));
    const QPointF vertex = layout.vertex;
    const double radius = layout.dimArcRadius;
    QPointF previous = layout.arcStart;
    path.moveTo(previous);
    for (int i = 1; i <= segments; ++i) {
        const double t = static_cast<double>(i) / static_cast<double>(segments);
        const double angle = layout.arcStartAngleRadians + layout.sweepAngleRadians * t;
        const QPointF current(
            vertex.x() + std::cos(angle) * radius,
            vertex.y() - std::sin(angle) * radius);
        path.lineTo(current);
        previous = current;
    }
    if (segments > 0) {
        path.lineTo(layout.arcEnd);
    }
    return path;
}

DimensionTextPreview AngularDimensionTextPreview(
    const AngularDimensionLayout& layout,
    const DimensionStyle& style)
{
    Q_UNUSED(style);
    return {layout.textPos, layout.textRotationDegrees};
}

void PaintAngularDimension(
    QPainter* painter,
    const DimensionEntity& dimension,
    const QColor& color,
    const DimensionStyle& style,
    const CurveDisplayOptions* display)
{
    const AngularDimensionLayout layout = BuildAngularDimensionLayout(dimension, style);

    QPen pen(color);
    pen.setCosmetic(true);
    painter->setPen(pen);
    painter->setBrush(Qt::NoBrush);

    painter->drawLine(layout.ext1Start, layout.ext1End);
    painter->drawLine(layout.ext2Start, layout.ext2End);

    double pixelsPerModel = 1.0;
    int viewRes = ViewDisplaySettings::kDefaultViewRes;
    if (display != nullptr) {
        pixelsPerModel = display->pixelsPerModelUnit;
        viewRes = display->viewRes;
    } else {
        const QTransform t = painter->worldTransform();
        pixelsPerModel = std::max(std::abs(t.m11()), 1.0e-12);
    }

    // layout.dimArcRadius is already in scene units (== model units with Y flip).
    const int segments = display != nullptr && display->useTrueCurves
        ? std::max(64, static_cast<int>(std::abs(layout.sweepAngleRadians) * 64.0))
        : ViewDisplaySettings::ArcDisplaySegments(
            layout.dimArcRadius,
            layout.sweepAngleRadians,
            pixelsPerModel,
            viewRes);
    const QPointF vertex = layout.vertex;
    const double radius = layout.dimArcRadius;
    QPointF previous = layout.arcStart;
    for (int i = 1; i <= segments; ++i) {
        const double t = static_cast<double>(i) / static_cast<double>(segments);
        const double angle = layout.arcStartAngleRadians + layout.sweepAngleRadians * t;
        const QPointF current(
            vertex.x() + std::cos(angle) * radius,
            vertex.y() - std::sin(angle) * radius);
        painter->drawLine(previous, current);
        previous = current;
    }
    if (segments > 0) {
        painter->drawLine(previous, layout.arcEnd);
    }

    const double delta = 0.08;
    const auto scenePointOnArc = [&](const double angle) {
        return QPointF(
            vertex.x() + std::cos(angle) * radius,
            vertex.y() - std::sin(angle) * radius);
    };
    const QPointF arcStartTip = scenePointOnArc(layout.arcStartAngleRadians);
    const QPointF arcStartFrom = scenePointOnArc(layout.arcStartAngleRadians + delta);
    const QPointF arcEndTip = scenePointOnArc(layout.arcStartAngleRadians + layout.sweepAngleRadians);
    const QPointF arcEndFrom = scenePointOnArc(layout.arcStartAngleRadians + layout.sweepAngleRadians - delta);
    detail::DrawArrowHead(painter, arcStartFrom, arcStartTip, style.arrowSize);
    detail::DrawArrowHead(painter, arcEndFrom, arcEndTip, style.arrowSize);

    const QFont font = DimensionFont(style);
    painter->setFont(font);
    const QFontMetricsF metrics(font);
    const double paddingScene = std::max(2.0, style.textHeight * 0.15);
    detail::DrawDimensionText(
        painter,
        layout.textPos,
        layout.label,
        metrics,
        layout.textRotationDegrees,
        paddingScene);
}

void PaintRadiusDimension(
    QPainter* painter,
    const DimensionEntity& dimension,
    const QColor& color,
    const DimensionStyle& style)
{
    const RadiusDimensionLayout layout = BuildRadiusDimensionLayout(dimension, style);

    QPen pen(color);
    pen.setCosmetic(true);
    painter->setPen(pen);
    painter->setBrush(Qt::NoBrush);

    if (layout.showCenterLeader) {
        painter->drawLine(layout.center, layout.attachment);
    }
    painter->drawLine(layout.dimLineStart, layout.dimLineEnd);
    if (layout.hasBaseline) {
        painter->drawLine(layout.dimLineEnd, layout.baselineEnd);
    }

    detail::DrawArrowHead(painter, layout.dimLineStart, layout.attachment, style.arrowSize);

    const QFont font = DimensionFont(style);
    painter->setFont(font);
    const QFontMetricsF metrics(font);
    const double paddingScene = std::max(2.0, style.textHeight * 0.15);
    detail::DrawDimensionText(
        painter,
        layout.textPos,
        layout.label,
        metrics,
        layout.textRotationDegrees,
        paddingScene);
}

void PaintJoggedRadiusDimension(
    QPainter* painter,
    const DimensionEntity& dimension,
    const QColor& color,
    const DimensionStyle& style)
{
    const JoggedRadiusDimensionLayout layout = BuildJoggedRadiusDimensionLayout(dimension, style);

    QPen pen(color);
    pen.setCosmetic(true);
    painter->setPen(pen);
    painter->setBrush(Qt::NoBrush);

    painter->drawLine(layout.centerOverride, layout.jogVertexNear);
    painter->drawLine(layout.jogVertexNear, layout.jogVertexFar);
    painter->drawLine(layout.jogVertexFar, layout.radialBreak);
    painter->drawLine(layout.radialBreak, layout.dimLineStart);
    painter->drawLine(layout.dimLineStart, layout.attachment);
    if (layout.hasArcExtension) {
        painter->drawLine(layout.textPos, layout.arcExtensionEnd);
    }

    detail::DrawArrowHead(painter, layout.dimLineStart, layout.attachment, style.arrowSize);

    const double crossSize = std::max(2.0, style.arrowSize * 0.75);
    painter->drawLine(
        layout.centerOverride.x() - crossSize,
        layout.centerOverride.y(),
        layout.centerOverride.x() + crossSize,
        layout.centerOverride.y());
    painter->drawLine(
        layout.centerOverride.x(),
        layout.centerOverride.y() - crossSize,
        layout.centerOverride.x(),
        layout.centerOverride.y() + crossSize);

    const QFont font = DimensionFont(style);
    painter->setFont(font);
    const QFontMetricsF metrics(font);
    const double paddingScene = std::max(2.0, style.textHeight * 0.15);
    detail::DrawDimensionText(
        painter,
        layout.textPos,
        layout.label,
        metrics,
        layout.textRotationDegrees,
        paddingScene);
}

void PaintDiameterDimension(
    QPainter* painter,
    const DimensionEntity& dimension,
    const QColor& color,
    const DimensionStyle& style)
{
    const DiameterDimensionLayout layout = BuildDiameterDimensionLayout(dimension, style);

    QPen pen(color);
    pen.setCosmetic(true);
    painter->setPen(pen);
    painter->setBrush(Qt::NoBrush);

    if (layout.showFarExtension) {
        painter->drawLine(layout.farAttachment, layout.nearAttachment);
    }
    painter->drawLine(layout.dimLineStart, layout.dimLineEnd);
    if (layout.hasBaseline) {
        painter->drawLine(layout.dimLineEnd, layout.baselineEnd);
    }

    detail::DrawArrowHead(painter, layout.dimLineStart, layout.nearAttachment, style.arrowSize);

    const QFont font = DimensionFont(style);
    painter->setFont(font);
    const QFontMetricsF metrics(font);
    const double paddingScene = std::max(2.0, style.textHeight * 0.15);
    detail::DrawDimensionText(
        painter,
        layout.textPos,
        layout.label,
        metrics,
        layout.textRotationDegrees,
        paddingScene);
}

OrdinateDimensionLayout LayoutFromOrdinateModel(
    const OrdinateDimModelLayout& modelLayout,
    const std::string& overrideText)
{
    OrdinateDimensionLayout layout;
    layout.orientation = modelLayout.orientation;
    layout.feature = detail::ToScene(modelLayout.feature);
    layout.leaderJog = detail::ToScene(modelLayout.leaderJog);
    layout.leaderEnd = detail::ToScene(modelLayout.leaderEnd);
    layout.textPos = detail::ToScene(modelLayout.textAnchor);
    layout.measureValue = modelLayout.measureValue;
    layout.textGapSign = modelLayout.textGapSign;
    layout.label = overrideText.empty()
        ? detail::FormatMeasureValue(modelLayout.measureValue)
        : detail::ToQString(overrideText);
    return layout;
}

OrdinateDimensionLayout BuildOrdinateDimensionLayout(
    const GeoPoint& feature,
    const GeoPoint& textPosition,
    const GeoPoint& ucsOrigin,
    const DimensionStyle& style,
    const std::optional<LinearDimensionOrientation>& forcedOrientation,
    const std::string& overrideText,
    const double textRotationRadians)
{
    OrdinateDimensionLayout layout = LayoutFromOrdinateModel(
        BuildOrdinateDimModelLayout(feature, textPosition, ucsOrigin, style, forcedOrientation),
        overrideText);
    layout.textRotationDegrees = qRadiansToDegrees(textRotationRadians);
    return layout;
}

OrdinateDimensionLayout BuildOrdinateDimensionLayout(
    const DimensionEntity& dimension,
    const DimensionStyle& style)
{
    OrdinateDimensionLayout layout = LayoutFromOrdinateModel(
        BuildOrdinateDimModelLayout(dimension, style),
        dimension.OverrideText());
    layout.textRotationDegrees = qRadiansToDegrees(dimension.TextRotationRadians());
    return layout;
}

QPainterPath OrdinateDimensionScenePath(const OrdinateDimensionLayout& layout, const DimensionStyle& style)
{
    Q_UNUSED(style);
    QPainterPath path;
    path.moveTo(layout.feature);
    path.lineTo(layout.leaderJog);
    path.lineTo(layout.leaderEnd);
    return path;
}

DimensionTextPreview OrdinateDimensionTextPreview(
    const OrdinateDimensionLayout& layout,
    const DimensionStyle& style)
{
    Q_UNUSED(style);
    return {layout.textPos, layout.textRotationDegrees};
}

void PaintOrdinateDimension(
    QPainter* painter,
    const DimensionEntity& dimension,
    const QColor& color,
    const DimensionStyle& style)
{
    const OrdinateDimensionLayout layout = BuildOrdinateDimensionLayout(dimension, style);

    QPen pen(color);
    pen.setCosmetic(true);
    painter->setPen(pen);
    painter->setBrush(Qt::NoBrush);

    painter->drawLine(layout.feature, layout.leaderJog);
    painter->drawLine(layout.leaderJog, layout.leaderEnd);
    detail::DrawTickMark(painter, layout.leaderEnd, layout.leaderJog, style.arrowSize);

    const QFont font = DimensionFont(style);
    painter->setFont(font);

    const QFontMetricsF metrics(font);
    const double paddingScene = std::max(2.0, style.textHeight * 0.15);

    detail::DrawDimensionText(
        painter,
        layout.textPos,
        layout.label,
        metrics,
        layout.textRotationDegrees,
        paddingScene);
}

void PaintAlignedDimension(
    QPainter* painter,
    const DimensionEntity& dimension,
    const QColor& color,
    const DimensionStyle& style)
{
    const AlignedDimensionLayout layout = BuildAlignedDimensionLayout(dimension, style);

    QPen pen(color);
    pen.setCosmetic(true);
    painter->setPen(pen);
    painter->setBrush(Qt::NoBrush);

    painter->drawLine(layout.ext1Start, layout.ext1End);
    painter->drawLine(layout.ext2Start, layout.ext2End);
    painter->drawLine(layout.dimStart, layout.dimEnd);

    detail::DrawArrowHead(painter, layout.dimEnd, layout.dimStart, style.arrowSize);
    detail::DrawArrowHead(painter, layout.dimStart, layout.dimEnd, style.arrowSize);

    const QFont font = DimensionFont(style);
    painter->setFont(font);

    const QFontMetricsF metrics(font);
    const detail::AlignedTextPlacement textPlacement = detail::ComputeAlignedTextPlacement(layout, metrics, style);
    const double paddingScene = std::max(2.0, style.textHeight * 0.15);

    detail::DrawDimensionText(
        painter,
        textPlacement.center,
        layout.label,
        metrics,
        textPlacement.rotationDegrees,
        paddingScene);
}

void PaintLinearDimension(
    QPainter* painter,
    const DimensionEntity& dimension,
    const QColor& color,
    const DimensionStyle& style)
{
    const LinearDimensionLayout layout = BuildLinearDimensionLayout(dimension, style);

    QPen pen(color);
    pen.setCosmetic(true);
    painter->setPen(pen);
    painter->setBrush(Qt::NoBrush);

    painter->drawLine(layout.ext1Start, layout.ext1End);
    painter->drawLine(layout.ext2Start, layout.ext2End);

    const QFont font = DimensionFont(style);
    painter->setFont(font);

    const QFontMetricsF metrics(font);
    const detail::LinearTextPlacement textPlacement = detail::ComputeLinearTextPlacement(
        layout,
        metrics,
        style);
    const double paddingScene = std::max(2.0, style.textHeight * 0.15);

    if (layout.orientation == LinearDimensionOrientation::Vertical) {
        painter->drawLine(layout.dimStart, layout.dimEnd);
        detail::DrawArrowHead(painter, layout.dimEnd, layout.dimStart, style.arrowSize);
        detail::DrawArrowHead(painter, layout.dimStart, layout.dimEnd, style.arrowSize);
    } else {
        detail::DrawTickMark(painter, layout.dimStart, layout.ext1End, style.arrowSize);
        detail::DrawTickMark(painter, layout.dimEnd, layout.ext2End, style.arrowSize);
        painter->drawLine(layout.dimStart, layout.dimEnd);
    }

    detail::DrawDimensionText(
        painter,
        textPlacement.center,
        layout.label,
        metrics,
        textPlacement.rotationDegrees,
        paddingScene);
}

void PaintDimensionEntity(
    QPainter* painter,
    const DimensionEntity& dimension,
    const QColor& color,
    const DimensionStyle& style,
    const CurveDisplayOptions* display)
{
    if (dimension.Kind() == DimensionKind::Linear) {
        PaintLinearDimension(painter, dimension, color, style);
    } else if (dimension.Kind() == DimensionKind::Radius) {
        PaintRadiusDimension(painter, dimension, color, style);
    } else if (dimension.Kind() == DimensionKind::JoggedRadius) {
        PaintJoggedRadiusDimension(painter, dimension, color, style);
    } else if (dimension.Kind() == DimensionKind::Diameter) {
        PaintDiameterDimension(painter, dimension, color, style);
    } else if (dimension.Kind() == DimensionKind::Angular) {
        PaintAngularDimension(painter, dimension, color, style, display);
    } else if (dimension.Kind() == DimensionKind::Ordinate) {
        PaintOrdinateDimension(painter, dimension, color, style);
    } else {
        PaintAlignedDimension(painter, dimension, color, style);
    }
}

} // namespace cad
