#pragma once

#include "Base/CadTypes.h"
#include <QGraphicsItem>
#include <QPainterPath>

namespace cad {

class CadDocument;
class Entity;

class EntityGraphicsItem : public QGraphicsItem {
public:
    EntityGraphicsItem(EntityId id, CadDocument& document);

    EntityId EntityIdValue() const { return entityId_; }

    void SyncFromDocument();
    void SetHovered(bool hovered);

    QRectF boundingRect() const override;
    QPainterPath shape() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

private:
    const Entity* EntityPtr() const;

    QRectF TextRect() const;
    QRectF MTextRect() const;
    QRectF DimensionRect() const;
    QRectF GroupRect() const;

    EntityId entityId_;
    CadDocument& document_;
    bool hovered_ = false;
};

} // namespace cad
