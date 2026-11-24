#include "View/EntityGraphicsItem.h"

#include "Base/Tolerance.h"
#include "DataModel/ArcEntity.h"
#include "DataModel/BlockReferenceEntity.h"
#include "DataModel/CadDocument.h"
#include "DataModel/DynamicBlockEvaluator.h"
#include "DataModel/DimensionEntity.h"
#include "DataModel/Entity.h"
#include "DataModel/GroupEntity.h"
#include "DataModel/LineEntity.h"
#include "DataModel/PolylineEntity.h"
#include "DataModel/RegionEntity.h"
#include "DataModel/MTextEntity.h"
#include "DataModel/TextEntity.h"
#include "IO/LineTypeLibrary.h"
#include "View/DimensionRenderUtil.h"
#include "Geometry/DimensionGeometry.h"
#include "View/CadView.h"
#include "View/GeoShapePainter.h"
#include "View/LineWeightUtil.h"
#include "View/TextRenderUtil.h"
#include "Base/ViewDisplaySettings.h"
#include <QFont>
#include <QGraphicsScene>
#include <QPainter>
#include <QPainterPath>
#include <QPainterPathStroker>
#include <QStyleOptionGraphicsItem>
#include <QtMath>
#include <algorithm>
#include <cctype>
#include <cmath>

namespace cad {

namespace {
QPointF ToScenePoint(const GeoPoint& point)
{
    return QPointF(point.X(), -point.Y());
}

GeoBounds SceneRectToModelBounds(const QRectF& sceneRect)
{
    const QRectF rect = sceneRect.normalized();
    return GeoBounds{rect.left(), -rect.bottom(), rect.right(), -rect.top(), true};
}

LinePattern LinePatternFromLayer(const std::string& lineTypeName)
{
    std::string upper = lineTypeName;
    std::transform(upper.begin(), upper.end(), upper.begin(), [](unsigned char ch) {
        return static_cast<char>(std::toupper(ch));
    });
    if (upper == "DASHED") {
        return LinePattern::Dashed;
    }
    if (upper == "CENTER") {
        return LinePattern::Center;
    }
    if (upper == "HIDDEN") {
        return LinePattern::Hidden;
    }
    return LinePattern::Continuous;
}

EntityStyle EffectiveStyleForEntity(const Entity& entity, const CadDocument& document)
{
    EntityStyle style = entity.Style();
    const Layer* layer = document.FindLayer(entity.LayerName());

    if (style.colorSource == PropertySource::ByLayer && layer) {
        style.red = static_cast<std::uint8_t>((layer->Color() >> 16) & 0xFF);
        style.green = static_cast<std::uint8_t>((layer->Color() >> 8) & 0xFF);
        style.blue = static_cast<std::uint8_t>(layer->Color() & 0xFF);
    } else if (style.colorSource == PropertySource::ByBlock) {
        style.red = 255;
        style.green = 255;
        style.blue = 255;
    }

    if (style.lineTypeSource == PropertySource::ByLayer && layer) {
        style.lineTypeName = layer->LineTypeName();
        style.linePattern = LinePatternFromLineTypeName(layer->LineTypeName());
    } else if (style.lineTypeSource == PropertySource::ByBlock) {
        style.lineTypeName = "Continuous";
        style.linePattern = LinePattern::Continuous;
    } else {
        if (style.lineTypeName.empty()) {
            style.lineTypeName = LineTypeNameFromPattern(style.linePattern);
        } else {
            style.linePattern = LinePatternFromLineTypeName(style.lineTypeName);
        }
    }

    if (style.lineWeightSource == PropertySource::ByLayer && layer) {
        style.lineWidth = EffectiveLineWeightMm(layer->LineWeightMm());
    } else if (style.lineWeightSource == PropertySource::ByBlock) {
        style.lineWidth = 0.0;
    }

    return style;
}

QPen PenForEntity(const Entity& entity, const CadDocument& document)
{
    const EntityStyle style = EffectiveStyleForEntity(entity, document);
    QColor color = entity.IsSelected() ? QColor(255, 210, 64) : QColor(style.red, style.green, style.blue);
    if (const Layer* layer = document.FindLayer(entity.LayerName())) {
        color.setAlphaF((100.0 - static_cast<double>(layer->Transparency())) / 100.0);
        if (layer->IsLocked() && !entity.IsSelected()) {
            color = QColor(
                (color.red() + 90) / 2,
                (color.green() + 90) / 2,
                (color.blue() + 90) / 2,
                qMax(80, color.alpha() / 2));
        }
    }
    QPen pen(color, style.lineWidth);
    if (!document.FindLineType(style.lineTypeName)) {
        if (style.linePattern == LinePattern::Dashed) {
            pen.setStyle(Qt::DashLine);
        } else if (style.linePattern == LinePattern::Center) {
            pen.setStyle(Qt::DashDotLine);
        } else if (style.linePattern == LinePattern::Hidden) {
            pen.setStyle(Qt::DotLine);
        }
    }
    return pen;
}

void ConfigureCadViewportPen(QPen& pen, bool hardwareAccelerated)
{
    // Cosmetic pens keep a stable on-screen weight under zoom.
    // Hardware MSAA needs slightly more coverage than software AA for thin strokes.
    pen.setCosmetic(true);
    pen.setWidthF(hardwareAccelerated ? 2.0 : 1.6);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    if (pen.color().red() > 250 && pen.color().green() > 250 && pen.color().blue() > 250) {
        pen.setColor(QColor(235, 235, 235));
    }
}

CurveDisplayOptions ResolveCurveDisplayOptions(
    const QGraphicsItem* item,
    QWidget* widget,
    bool paintingPrinter)
{
    CurveDisplayOptions options;
    options.useTrueCurves = paintingPrinter;

    auto tryView = [&](QGraphicsView* view) -> bool {
        auto* cad = dynamic_cast<CadView*>(view);
        if (!cad) {
            return false;
        }
        options = cad->MakeCurveDisplayOptions(paintingPrinter);
        return true;
    };

    if (item != nullptr && item->scene() != nullptr) {
        const auto views = item->scene()->views();
        for (QGraphicsView* view : views) {
            if (tryView(view)) {
                return options;
            }
        }
    }

    for (QWidget* current = widget; current != nullptr; current = current->parentWidget()) {
        if (auto* view = dynamic_cast<QGraphicsView*>(current)) {
            if (tryView(view)) {
                break;
            }
        }
    }

    if (!paintingPrinter && options.pixelsPerModelUnit <= Tolerance::Model) {
        options.pixelsPerModelUnit = 1.0;
    }
    return options;
}

QString ToQString(const std::string& text)
{
    return QString::fromUtf8(text.c_str());
}

DimensionStyle ResolveDimensionStyle(const CadDocument& document, const std::string& name)
{
    for (const DimensionStyle& style : document.DimensionStyles()) {
        if (style.name == name) {
            return style;
        }
    }
    return DimensionStyle{};
}

QPainterPath TextLikeScenePath(const Entity& entity, const TextLayoutOptions& options)
{
    return TextLikeEntityScenePath(entity, options);
}

void DrawTextHighlight(QPainter* painter, const Entity& entity, const TextLayoutOptions& options, bool hovered, bool selected)
{
    const QPainterPath path = TextLikeScenePath(entity, options);
    if (path.isEmpty()) {
        return;
    }

    if (hovered && !selected) {
        PaintTextGlyphHoverGlow(painter, path);
    } else if (selected) {
        PaintTextGlyphSelectionOutline(painter, path);
    }
}
} // namespace

EntityGraphicsItem::EntityGraphicsItem(EntityId id, CadDocument& document)
    : entityId_(id)
    , document_(document)
{
    setPos(0.0, 0.0);
    setAcceptedMouseButtons(Qt::NoButton);
    setCacheMode(QGraphicsItem::NoCache);
    setFlag(QGraphicsItem::ItemUsesExtendedStyleOption, true);

    if (const Entity* entity = document.FindEntity(id)) {
        switch (entity->Type()) {
        case EntityType::Text:
        case EntityType::MText:
        case EntityType::Dimension:
            setFlag(QGraphicsItem::ItemClipsToShape, false);
            setBoundingRegionGranularity(0.0);
            break;
        default:
            setBoundingRegionGranularity(0.25);
            break;
        }
    } else {
        setBoundingRegionGranularity(0.25);
    }
}

void EntityGraphicsItem::SyncFromDocument()
{
    prepareGeometryChange();
    update();
}

void EntityGraphicsItem::SetHovered(bool hovered)
{
    if (hovered_ == hovered) {
        return;
    }
    hovered_ = hovered;
    update();
}

const Entity* EntityGraphicsItem::EntityPtr() const
{
    return document_.FindEntity(entityId_);
}

QRectF EntityGraphicsItem::boundingRect() const
{
    const Entity* entity = EntityPtr();
    if (!entity) {
        return {};
    }
    if (!document_.IsEntityVisible(*entity)) {
        return {};
    }

    if (entity->Type() == EntityType::Text) {
        return TextRect();
    }
    if (entity->Type() == EntityType::MText) {
        return MTextRect();
    }
    if (entity->Type() == EntityType::Dimension) {
        return DimensionRect();
    }
    if (entity->Type() == EntityType::Group) {
        return GroupRect();
    }
    if (entity->Type() == EntityType::Ray || entity->Type() == EntityType::XLine) {
        if (scene()) {
            return scene()->sceneRect().adjusted(-100.0, -100.0, 100.0, 100.0);
        }
    }

    const GeoBounds bounds = entity->Shaper().Bounds();
    if (!bounds.valid) {
        return QRectF(-4.0, -4.0, 8.0, 8.0);
    }
    const QPointF topLeft(bounds.minX, -bounds.maxY);
    const QPointF bottomRight(bounds.maxX, -bounds.minY);
    // Extra margin keeps anti-aliased stroke fringe from being clipped.
    return QRectF(topLeft, bottomRight).normalized().adjusted(-12.0, -12.0, 12.0, 12.0);
}

QPainterPath EntityGraphicsItem::shape() const
{
    const Entity* entity = EntityPtr();
    if (!entity || !document_.IsEntityVisible(*entity)) {
        return {};
    }

    if (entity->Type() == EntityType::Text) {
        TextLayoutOptions options;
        options.document = &document_;
        options.style = &entity->Style();
        return TextLikeScenePath(*entity, options);
    }
    if (entity->Type() == EntityType::MText) {
        TextLayoutOptions options;
        options.document = &document_;
        options.style = &entity->Style();
        return TextLikeScenePath(*entity, options);
    }

    QPainterPath path = GeoShapePainter::BuildPath(entity->Shaper());
    if (path.isEmpty()) {
        path.addRect(boundingRect());
        return path;
    }

    QPainterPathStroker stroker;
    stroker.setWidth(8.0);
    stroker.setCapStyle(Qt::RoundCap);
    stroker.setJoinStyle(Qt::RoundJoin);
    return path.united(stroker.createStroke(path));
}

void EntityGraphicsItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget)
{
    const Entity* entity = EntityPtr();
    if (!entity) {
        return;
    }
    if (!document_.IsEntityVisible(*entity)) {
        return;
    }
    const bool paintingPrinter = painter->device()
        && painter->device()->devType() == QInternal::Printer;
    if (paintingPrinter) {
        if (!document_.IsLayerPlottable(entity->LayerName())) {
            return;
        }
    }

    painter->setRenderHints(
        QPainter::Antialiasing
        | QPainter::TextAntialiasing
        | QPainter::SmoothPixmapTransform,
        true);
    const EntityStyle effectiveStyle = EffectiveStyleForEntity(*entity, document_);
    const CurveDisplayOptions curveDisplay = ResolveCurveDisplayOptions(this, widget, paintingPrinter);
    QPen pen = PenForEntity(*entity, document_);
    if (!paintingPrinter) {
        bool hardwareAccel = false;
        if (scene() != nullptr) {
            for (QGraphicsView* view : scene()->views()) {
                if (auto* cad = dynamic_cast<CadView*>(view)) {
                    hardwareAccel = cad->IsHardwareAccelerationActive();
                    break;
                }
            }
        }
        ConfigureCadViewportPen(pen, hardwareAccel);
        const double patternScale = std::max(
            0.0001,
            document_.GlobalLinetypeScale() * std::max(0.0001, effectiveStyle.lineTypeScale));
        const double pixelsPerModelUnit = std::max(
            Tolerance::Model,
            curveDisplay.pixelsPerModelUnit > Tolerance::Model
                ? curveDisplay.pixelsPerModelUnit
                : std::hypot(painter->worldTransform().m11(), painter->worldTransform().m12()));
        if (const LineTypeDefinition* lineType = document_.FindLineType(effectiveStyle.lineTypeName)) {
            LineTypeLibrary::ApplyPatternToPen(pen, *lineType, patternScale, pixelsPerModelUnit);
        }
    } else {
        const double patternScale = std::max(
            0.0001,
            document_.GlobalLinetypeScale() * std::max(0.0001, effectiveStyle.lineTypeScale));
        if (const LineTypeDefinition* lineType = document_.FindLineType(effectiveStyle.lineTypeName)) {
            // Printer: pattern stays in model / width units (non-cosmetic).
            LineTypeLibrary::ApplyPatternToPen(pen, *lineType, patternScale, 0.0);
        }
    }
    painter->setPen(pen);

    struct DimGuard {
        QPainter* painter = nullptr;
        bool active = false;
        explicit DimGuard(QPainter* p, bool on)
            : painter(p)
            , active(on)
        {
            if (active && painter) {
                painter->save();
                painter->setOpacity(0.28);
            }
        }
        ~DimGuard()
        {
            if (active && painter) {
                painter->restore();
            }
        }
    };
    const bool dimForRefEdit =
        document_.IsRefEditActive() && !document_.IsInRefEditWorkingSet(entity->Id());
    if (dimForRefEdit) {
        QColor dim = pen.color();
        dim.setAlpha(qMax(40, dim.alpha() / 4));
        pen.setColor(dim);
        painter->setPen(pen);
    }
    DimGuard dimGuard(painter, dimForRefEdit);

    if (entity->Type() == EntityType::Text) {
        const auto& text = static_cast<const TextEntity&>(*entity);
        TextLayoutOptions options;
        options.document = &document_;
        options.style = &effectiveStyle;
        if (hovered_ && !entity->IsSelected()) {
            options.overrideColor = QColor(210, 238, 255);
        }
        DrawTextHighlight(painter, *entity, options, hovered_, entity->IsSelected());
        painter->save();
        painter->setClipping(false);
        PaintTextEntity(painter, text, effectiveStyle, options);
        painter->restore();
        return;
    }

    if (entity->Type() == EntityType::MText) {
        const auto& text = static_cast<const MTextEntity&>(*entity);
        TextLayoutOptions options;
        options.document = &document_;
        options.style = &effectiveStyle;
        if (hovered_ && !entity->IsSelected()) {
            options.overrideColor = QColor(210, 238, 255);
        }
        DrawTextHighlight(painter, *entity, options, hovered_, entity->IsSelected());
        painter->save();
        painter->setClipping(false);
        PaintMTextEntity(painter, text, effectiveStyle, options);
        painter->restore();
        return;
    }

    if (entity->Type() == EntityType::Dimension) {
        const auto& dim = static_cast<const DimensionEntity&>(*entity);
        const DimensionStyle style = ResolveDimensionStyle(document_, dim.DimensionStyleName());
        PaintDimensionEntity(painter, dim, pen.color(), style, &curveDisplay);
        return;
    }

    if (entity->Type() == EntityType::BlockReference) {
        auto* blockEntity = document_.FindEntity(entityId_);
        if (blockEntity) {
            auto& block = static_cast<BlockReferenceEntity&>(*blockEntity);
            if (const BlockDefinition* def = document_.FindBlockDefinition(block.BlockName())) {
                if (!block.IsCacheValid()) {
                    block.RebuildCache(def);
                }

                const GeoPoint origin(0.0, 0.0);
                bool paintedAny = false;
                const std::vector<DynParamValue> resolved =
                    DynamicBlockEvaluator::ResolveValues(*def, block.DynamicValues());
                std::vector<std::unique_ptr<Entity>> evaluated =
                    DynamicBlockEvaluator::Evaluate(*def, resolved);
                for (auto& clone : evaluated) {
                    if (!clone) {
                        continue;
                    }
                    const double sx = block.ScaleX();
                    if (std::abs(sx - 1.0) > 1.0e-12) {
                        clone->Scale(origin, sx);
                    }
                    if (std::abs(block.RotationRadians()) > 1.0e-12) {
                        clone->Rotate(origin, block.RotationRadians());
                    }
                    clone->MoveBy(block.InsertionPoint().X(), block.InsertionPoint().Y());

                    QPen entityPen = pen;
                    const EntityStyle& es = clone->Style();
                    if (es.colorSource == PropertySource::Override) {
                        entityPen.setColor(QColor(es.red, es.green, es.blue));
                    }
                    GeoShapePainter::Paint(painter, clone->Shaper(), entityPen, nullptr, &curveDisplay);
                    paintedAny = true;
                }
                if (!paintedAny) {
                    GeoShapePainter::Paint(painter, block.Shaper(), pen, nullptr, &curveDisplay);
                }

                double attrOffset = 0.0;
                for (const BlockAttributeDef& attrDef : def->attributeDefs) {
                    if (attrDef.invisible) {
                        continue;
                    }
                    std::string value = attrDef.defaultValue;
                    for (const BlockAttributeValue& av : block.AttributeValues()) {
                        if (av.tag == attrDef.tag) {
                            value = av.value;
                            break;
                        }
                    }
                    const QPointF originScene = ToScenePoint(block.InsertionPoint());
                    QFont font = painter->font();
                    font.setPointSizeF(std::max(6.0, attrDef.textHeight * 0.75));
                    painter->setFont(font);
                    painter->setPen(pen.color());
                    painter->drawText(
                        originScene + QPointF(2.0, attrOffset),
                        QString::fromStdString(value.empty() ? attrDef.tag : value));
                    attrOffset += attrDef.textHeight * 1.2 + 2.0;
                }
                return;
            }
        }
        const auto& block = static_cast<const BlockReferenceEntity&>(*entity);
        GeoShapePainter::Paint(painter, entity->Shaper(), pen, nullptr, &curveDisplay);
        const QPointF origin = ToScenePoint(block.InsertionPoint());
        const double size = 12.0 * block.ScaleFactor();
        painter->drawText(origin + QPointF(size * 0.6, 0.0), ToQString(block.BlockName()));
        return;
    }

    if (entity->Type() == EntityType::Group) {
        const auto& group = static_cast<const GroupEntity&>(*entity);
        painter->drawText(QPointF(0.0, 0.0), QStringLiteral("Group: ") + ToQString(group.Name()));
        return;
    }

    if (entity->Type() == EntityType::Ray || entity->Type() == EntityType::XLine) {
        const QRectF clipRect = option ? option->exposedRect : boundingRect();
        const GeoBounds clipBounds = SceneRectToModelBounds(clipRect);
        GeoShapePainter::Paint(painter, entity->Shaper(), pen, &clipBounds, &curveDisplay);
        return;
    }

    GeoShapePainter::Paint(painter, entity->Shaper(), pen, nullptr, &curveDisplay);
}

QRectF EntityGraphicsItem::TextRect() const
{
    const Entity* entity = EntityPtr();
    TextLayoutOptions options;
    options.document = &document_;
    options.style = &entity->Style();
    return TextEntityBounds(static_cast<const TextEntity&>(*entity), options);
}

QRectF EntityGraphicsItem::MTextRect() const
{
    const Entity* entity = EntityPtr();
    TextLayoutOptions options;
    options.document = &document_;
    options.style = &entity->Style();
    return MTextEntityBounds(static_cast<const MTextEntity&>(*entity), options);
}

QRectF EntityGraphicsItem::DimensionRect() const
{
    const Entity* entity = EntityPtr();
    const auto& dim = static_cast<const DimensionEntity&>(*entity);
    const DimensionStyle style = ResolveDimensionStyle(document_, dim.DimensionStyleName());
    const GeoBounds bounds = DimensionModelBounds(dim, style);
    if (!bounds.valid) {
        return {};
    }
    const QPointF topLeft(bounds.minX, -bounds.maxY);
    const QPointF bottomRight(bounds.maxX, -bounds.minY);
    return QRectF(topLeft, bottomRight).normalized().adjusted(-8.0, -8.0, 8.0, 8.0);
}

QRectF EntityGraphicsItem::GroupRect() const
{
    return QRectF(-30.0, -20.0, 120.0, 30.0);
}

} // namespace cad
