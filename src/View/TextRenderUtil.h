#pragma once

#include "Base/TextTypes.h"
#include "DataModel/CadDocument.h"
#include "DataModel/Entity.h"
#include "Geometry/GeoPoint.h"
#include <QColor>
#include <QPainterPath>
#include <QPolygonF>
#include <QRectF>
#include <array>
#include <optional>
#include <string>

class QPainter;

namespace cad {

class MTextEntity;
class TextEntity;

struct TextLayoutOptions {
    const CadDocument* document = nullptr;
    const EntityStyle* style = nullptr;
    std::string fileName;
    std::optional<QColor> overrideColor;
};

QRectF TextEntityBounds(const TextEntity& text, const TextLayoutOptions& options);
QRectF MTextEntityBounds(const MTextEntity& text, const TextLayoutOptions& options);
QRectF TextLikeEntityBounds(const Entity& entity, const TextLayoutOptions& options);
std::array<QPointF, 4> TextEntitySceneCorners(const TextEntity& text, const TextLayoutOptions& options);
std::array<QPointF, 4> MTextEntitySceneCorners(const MTextEntity& text, const TextLayoutOptions& options);
std::array<GeoPoint, 4> TextLikeEntityModelCorners(const Entity& entity, const TextLayoutOptions& options);
bool TextLikeEntityContainsModelPoint(
    const Entity& entity,
    const GeoPoint& point,
    const TextLayoutOptions& options,
    double hitToleranceModel = 0.0);
bool TextLikeEntityCrossesModelRect(const Entity& entity, const QRectF& modelRect, double tolerance, const TextLayoutOptions& options);
bool TextLikeEntityFullyInsideModelRect(const Entity& entity, const QRectF& modelRect, double tolerance, const TextLayoutOptions& options);
double TextLikeEntityDistanceToModelPoint(const Entity& entity, const GeoPoint& point, const TextLayoutOptions& options);

void PaintTextEntity(QPainter* painter, const TextEntity& text, const EntityStyle& style, const TextLayoutOptions& options);
void PaintMTextEntity(QPainter* painter, const MTextEntity& text, const EntityStyle& style, const TextLayoutOptions& options);

QPainterPath TextEntityScenePath(const TextEntity& text, const TextLayoutOptions& options);
QPainterPath MTextEntityScenePath(const MTextEntity& text, const TextLayoutOptions& options);
QPainterPath TextLikeEntityScenePath(const Entity& entity, const TextLayoutOptions& options);
void PaintTextGlyphHoverGlow(QPainter* painter, const QPainterPath& path);
void PaintTextGlyphSelectionOutline(QPainter* painter, const QPainterPath& path);

std::string ExpandTextContent(const std::string& content, const TextLayoutOptions& options);
std::string PlainTextFromMText(const std::string& content);

QFont ResolveTextFont(const EntityStyle& style, const CadDocument* document, double height);
QPointF TextAlignmentOffset(const QRectF& bounds, TextHorizontalAlign horizontal, TextVerticalAlign vertical);
QPointF MTextAttachmentOffset(const QRectF& bounds, MTextAttachment attachment);

} // namespace cad
