#pragma once

#include "Base/Style.h"
#include "Base/ViewDisplaySettings.h"
#include "DataModel/DimensionTypes.h"
#include "DataModel/DimensionEntity.h"
#include "Geometry/DimensionGeometry.h"
#include "Geometry/GeoPoint.h"
#include <QColor>
#include <QPainterPath>
#include <QPointF>
#include <QString>
#include <optional>

class QPainter;
class QGraphicsSimpleTextItem;

namespace cad {

struct LinearDimensionLayout {
    LinearDimensionOrientation orientation = LinearDimensionOrientation::Horizontal;
    QPointF ext1Start;
    QPointF ext1End;
    QPointF ext2Start;
    QPointF ext2End;
    QPointF dimStart;
    QPointF dimEnd;
    QPointF textPos;
    double measureValue = 0.0;
    QString label;
    double textRotationDegrees = 0.0;
    double textGapSign = 1.0;
};

LinearDimensionLayout BuildLinearDimensionLayout(
    const GeoPoint& first,
    const GeoPoint& second,
    const GeoPoint& textPosition,
    const DimensionStyle& style,
    const std::optional<LinearDimensionOrientation>& forcedOrientation = std::nullopt,
    const std::string& overrideText = {},
    double textRotationRadians = 0.0);

LinearDimensionLayout BuildLinearDimensionLayout(
    const DimensionEntity& dimension,
    const DimensionStyle& style);

QPainterPath LinearDimensionScenePath(const LinearDimensionLayout& layout, const DimensionStyle& style);

struct AlignedDimensionLayout {
    QPointF ext1Start;
    QPointF ext1End;
    QPointF ext2Start;
    QPointF ext2End;
    QPointF dimStart;
    QPointF dimEnd;
    QPointF textPos;
    double measureValue = 0.0;
    QString label;
    double textRotationDegrees = 0.0;
    double dimRotationDegrees = 0.0;
    double textGapSign = 1.0;
};

AlignedDimensionLayout BuildAlignedDimensionLayout(
    const GeoPoint& first,
    const GeoPoint& second,
    const GeoPoint& textPosition,
    const DimensionStyle& style,
    const std::string& overrideText = {},
    double textRotationRadians = 0.0);

AlignedDimensionLayout BuildAlignedDimensionLayout(
    const DimensionEntity& dimension,
    const DimensionStyle& style);

QPainterPath AlignedDimensionScenePath(const AlignedDimensionLayout& layout, const DimensionStyle& style);

struct RadiusDimensionLayout {
    QPointF center;
    QPointF attachment;
    QPointF dimLineStart;
    QPointF dimLineEnd;
    QPointF baselineEnd;
    QPointF textPos;
    bool hasBaseline = false;
    bool showCenterLeader = true;
    double measureValue = 0.0;
    QString label;
    double textRotationDegrees = 0.0;
};

RadiusDimensionLayout BuildRadiusDimensionLayout(
    const GeoPoint& center,
    const GeoPoint& attachment,
    const GeoPoint& textPosition,
    const DimensionStyle& style,
    const std::string& overrideText = {},
    double textRotationRadians = 0.0);

RadiusDimensionLayout BuildRadiusDimensionLayout(
    const DimensionEntity& dimension,
    const DimensionStyle& style);

QPainterPath RadiusDimensionScenePath(const RadiusDimensionLayout& layout, const DimensionStyle& style);

struct JoggedRadiusDimensionLayout {
    QPointF attachment;
    QPointF dimLineStart;
    QPointF radialBreak;
    QPointF jogVertexNear;
    QPointF jogVertexFar;
    QPointF centerOverride;
    QPointF textPos;
    QPointF arcExtensionEnd;
    bool hasArcExtension = false;
    double measureValue = 0.0;
    QString label;
    double textRotationDegrees = 0.0;
};

JoggedRadiusDimensionLayout BuildJoggedRadiusDimensionLayout(
    const GeoPoint& trueCenter,
    const GeoPoint& centerOverride,
    const GeoPoint& dimLineLocation,
    const GeoPoint& jogLocation,
    const DimensionStyle& style,
    const std::string& overrideText = {},
    double textRotationRadians = 0.0,
    bool hasArcSpan = false,
    double arcStartAngleRadians = 0.0,
    double arcEndAngleRadians = 0.0,
    double radius = 0.0);

JoggedRadiusDimensionLayout BuildJoggedRadiusDimensionLayout(
    const DimensionEntity& dimension,
    const DimensionStyle& style);

QPainterPath JoggedRadiusDimensionScenePath(const JoggedRadiusDimensionLayout& layout, const DimensionStyle& style);

struct DiameterDimensionLayout {
    QPointF center;
    QPointF nearAttachment;
    QPointF farAttachment;
    QPointF dimLineStart;
    QPointF dimLineEnd;
    QPointF baselineEnd;
    QPointF textPos;
    bool hasBaseline = false;
    bool showFarExtension = true;
    double measureValue = 0.0;
    QString label;
    double textRotationDegrees = 0.0;
};

DiameterDimensionLayout BuildDiameterDimensionLayout(
    const GeoPoint& center,
    const GeoPoint& nearAttachment,
    const GeoPoint& textPosition,
    const DimensionStyle& style,
    const std::string& overrideText = {},
    double textRotationRadians = 0.0);

DiameterDimensionLayout BuildDiameterDimensionLayout(
    const DimensionEntity& dimension,
    const DimensionStyle& style);

QPainterPath DiameterDimensionScenePath(const DiameterDimensionLayout& layout, const DimensionStyle& style);

struct AngularDimensionLayout {
    QPointF vertex;
    QPointF ext1Start;
    QPointF ext1End;
    QPointF ext2Start;
    QPointF ext2End;
    QPointF arcStart;
    QPointF arcEnd;
    QPointF textPos;
    double dimArcRadius = 0.0;
    double arcStartAngleRadians = 0.0;
    double sweepAngleRadians = 0.0;
    double measureValue = 0.0;
    QString label;
    double textRotationDegrees = 0.0;
};

AngularDimensionLayout BuildAngularDimensionLayout(
    const GeoPoint& vertex,
    const GeoPoint& arm1,
    const GeoPoint& arm2,
    const GeoPoint& textPosition,
    const DimensionStyle& style,
    const std::string& overrideText = {},
    double textRotationRadians = 0.0,
    bool hasArcSpan = false,
    double arcStartAngleRadians = 0.0,
    double arcEndAngleRadians = 0.0,
    double sourceArcRadius = 0.0);

AngularDimensionLayout BuildAngularDimensionLayout(
    const DimensionEntity& dimension,
    const DimensionStyle& style);

QPainterPath AngularDimensionScenePath(const AngularDimensionLayout& layout, const DimensionStyle& style);

void SetPreviewMeasureLabelPosition(
    QGraphicsSimpleTextItem* item,
    const QPointF& center,
    double rotationDegrees);

struct DimensionTextPreview {
    QPointF center;
    double rotationDegrees = 0.0;
};

DimensionTextPreview AlignedDimensionTextPreview(
    const AlignedDimensionLayout& layout,
    const DimensionStyle& style);

DimensionTextPreview LinearDimensionTextPreview(
    const LinearDimensionLayout& layout,
    const DimensionStyle& style);

DimensionTextPreview RadiusDimensionTextPreview(
    const RadiusDimensionLayout& layout,
    const DimensionStyle& style);

DimensionTextPreview JoggedRadiusDimensionTextPreview(
    const JoggedRadiusDimensionLayout& layout,
    const DimensionStyle& style);

DimensionTextPreview DiameterDimensionTextPreview(
    const DiameterDimensionLayout& layout,
    const DimensionStyle& style);

DimensionTextPreview AngularDimensionTextPreview(
    const AngularDimensionLayout& layout,
    const DimensionStyle& style);

struct OrdinateDimensionLayout {
    LinearDimensionOrientation orientation = LinearDimensionOrientation::Vertical;
    QPointF feature;
    QPointF leaderJog;
    QPointF leaderEnd;
    QPointF textPos;
    double measureValue = 0.0;
    QString label;
    double textRotationDegrees = 0.0;
    double textGapSign = 1.0;
};

OrdinateDimensionLayout BuildOrdinateDimensionLayout(
    const GeoPoint& feature,
    const GeoPoint& textPosition,
    const GeoPoint& ucsOrigin,
    const DimensionStyle& style,
    const std::optional<LinearDimensionOrientation>& forcedOrientation = std::nullopt,
    const std::string& overrideText = {},
    double textRotationRadians = 0.0);

OrdinateDimensionLayout BuildOrdinateDimensionLayout(
    const DimensionEntity& dimension,
    const DimensionStyle& style);

QPainterPath OrdinateDimensionScenePath(const OrdinateDimensionLayout& layout, const DimensionStyle& style);

DimensionTextPreview OrdinateDimensionTextPreview(
    const OrdinateDimensionLayout& layout,
    const DimensionStyle& style);

void PaintAlignedDimension(
    QPainter* painter,
    const DimensionEntity& dimension,
    const QColor& color,
    const DimensionStyle& style);

void PaintLinearDimension(
    QPainter* painter,
    const DimensionEntity& dimension,
    const QColor& color,
    const DimensionStyle& style);

void PaintRadiusDimension(
    QPainter* painter,
    const DimensionEntity& dimension,
    const QColor& color,
    const DimensionStyle& style);

void PaintJoggedRadiusDimension(
    QPainter* painter,
    const DimensionEntity& dimension,
    const QColor& color,
    const DimensionStyle& style);

void PaintDiameterDimension(
    QPainter* painter,
    const DimensionEntity& dimension,
    const QColor& color,
    const DimensionStyle& style);

void PaintAngularDimension(
    QPainter* painter,
    const DimensionEntity& dimension,
    const QColor& color,
    const DimensionStyle& style,
    const CurveDisplayOptions* display = nullptr);

void PaintOrdinateDimension(
    QPainter* painter,
    const DimensionEntity& dimension,
    const QColor& color,
    const DimensionStyle& style);

void PaintDimensionEntity(
    QPainter* painter,
    const DimensionEntity& dimension,
    const QColor& color,
    const DimensionStyle& style,
    const CurveDisplayOptions* display = nullptr);

} // namespace cad
