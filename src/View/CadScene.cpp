#include "View/CadScene.h"

#include "DataModel/BlockReferenceEntity.h"
#include "DataModel/CadDocument.h"
#include "DataModel/Entity.h"
#include "View/EntityGraphicsItem.h"
#include "View/GripItem.h"
#include <QLineF>
#include <QPainterPath>
#include <QPen>
#include <algorithm>
#include <map>

namespace cad {

CadScene::CadScene(CadDocument& document, QObject* parent)
    : QGraphicsScene(parent)
    , document_(document)
{
    setBackgroundBrush(QColor(33, 40, 48));
    setItemIndexMethod(QGraphicsScene::NoIndex);
    setSceneRect(-5000.0, -5000.0, 10000.0, 10000.0);
    document_.AddObserver(this);
}

CadScene::~CadScene()
{
    document_.RemoveObserver(this);
}

void CadScene::ReleaseGraphicsItem(EntityGraphicsItem* item)
{
    if (!item) {
        return;
    }
    if (item->scene() == this) {
        removeItem(item);
    }
    delete item;
}

void CadScene::ReleaseGripItem(GripItem* grip)
{
    if (!grip) {
        return;
    }
    if (grip->scene() == this) {
        removeItem(grip);
    }
    delete grip;
}

void CadScene::SetViewportRefreshCallback(std::function<void()> callback)
{
    viewportRefreshCallback_ = std::move(callback);
}

void CadScene::InvalidateItemRegions(const QRectF& extra)
{
    Q_UNUSED(extra);
    if (viewportRefreshCallback_) {
        viewportRefreshCallback_();
        return;
    }

    QRectF dirtyRect = extra;
    for (EntityGraphicsItem* item : items_) {
        if (item && item->scene() == this) {
            dirtyRect = dirtyRect.united(item->sceneBoundingRect());
        }
    }
    for (GripItem* grip : grips_) {
        if (grip && grip->scene() == this) {
            dirtyRect = dirtyRect.united(grip->sceneBoundingRect());
        }
    }
    if (!dirtyRect.isValid()) {
        return;
    }
    invalidate(dirtyRect.adjusted(-200.0, -200.0, 200.0, 200.0), QGraphicsScene::AllLayers);
}

void CadScene::OnEntityAdded(const Entity& entity)
{
    if (EntityGraphicsItem* existing = items_.take(entity.Id())) {
        ReleaseGraphicsItem(existing);
    }

    auto* item = new EntityGraphicsItem(entity.Id(), document_);
    items_.insert(entity.Id(), item);
    addItem(item);
    ExpandSceneRectForItem(item);
    InvalidateItemRegions(item->sceneBoundingRect());

    if (entity.IsSelected()) {
        RebuildGrips();
    }
}

void CadScene::OnEntityRemoved(EntityId id)
{
    OnEntitiesRemoved({id});
}

void CadScene::OnEntitiesRemoved(const std::vector<EntityId>& ids)
{
    for (EntityId id : ids) {
        RemoveGripsForEntity(id);
        if (EntityGraphicsItem* item = items_.take(id)) {
            ReleaseGraphicsItem(item);
        }
    }
}

void CadScene::OnEntitiesChanged(const std::vector<EntityId>& ids)
{
    ClearSelectionDragOffset();
    for (EntityId id : ids) {
        if (EntityGraphicsItem* item = FindItem(id)) {
            item->SyncFromDocument();
            ExpandSceneRectForItem(item);
        }
    }
    RebuildGrips();
    InvalidateItemRegions();
}

void CadScene::OnSelectionChanged()
{
    ClearSelectionDragOffset();

    for (EntityGraphicsItem* item : items_) {
        if (item && item->scene() == this) {
            item->update();
        }
    }
    RebuildGrips();
    InvalidateItemRegions();
}

void CadScene::OnDocumentReset()
{
    RebuildAllItems();
    InvalidateItemRegions();
}

void CadScene::SetSelectionDragOffset(double dx, double dy)
{
    for (const auto& entity : document_.Entities()) {
        if (!entity->IsSelected() || !document_.IsEntityEditable(entity->Id())) {
            continue;
        }
        if (EntityGraphicsItem* item = FindItem(entity->Id())) {
            item->setCacheMode(QGraphicsItem::NoCache);
            item->setPos(QPointF(dx, -dy));
        }
    }
}

void CadScene::ClearSelectionDragOffset()
{
    for (EntityGraphicsItem* item : items_) {
        if (item) {
            item->setPos(0.0, 0.0);
        }
    }
    if (viewportRefreshCallback_) {
        viewportRefreshCallback_();
    }
}

void CadScene::RebuildAllItems()
{
    ClearGrips();
    for (EntityGraphicsItem* item : items_) {
        ReleaseGraphicsItem(item);
    }
    items_.clear();

    for (const auto& entity : document_.Entities()) {
        auto* item = new EntityGraphicsItem(entity->Id(), document_);
        items_.insert(entity->Id(), item);
        addItem(item);
        ExpandSceneRectForItem(item);
    }
    RebuildGrips();
}

void CadScene::ExpandSceneRectForItem(EntityGraphicsItem* item)
{
    if (!item) {
        return;
    }

    const QRectF itemRect = item->sceneBoundingRect().adjusted(-500.0, -500.0, 500.0, 500.0);
    if (itemRect.isEmpty()) {
        return;
    }
    if (!sceneRect().contains(itemRect)) {
        setSceneRect(sceneRect().united(itemRect));
    }
}

void CadScene::RemoveGripsForEntity(EntityId id)
{
    for (int i = grips_.size() - 1; i >= 0; --i) {
        if (grips_[i]->EntityIdValue() != id) {
            continue;
        }
        ReleaseGripItem(grips_.takeAt(i));
    }
}

void CadScene::ClearGrips()
{
    for (GripItem* grip : grips_) {
        ReleaseGripItem(grip);
    }
    grips_.clear();
    if (splineControlFrameItem_) {
        if (splineControlFrameItem_->scene()) {
            removeItem(splineControlFrameItem_);
        }
        delete splineControlFrameItem_;
        splineControlFrameItem_ = nullptr;
    }
}

void CadScene::RebuildGrips()
{
    ClearGrips();
    for (const auto& entity : document_.Entities()) {
        if (!entity->IsSelected() || !document_.IsEntityEditable(entity->Id())) {
            continue;
        }
        for (const GripInfo& grip : entity->Grips()) {
            auto* gripItem = new GripItem(entity->Id(), grip.index, grip.position, grip.kind);
            grips_.push_back(gripItem);
            addItem(gripItem);
        }
        if (entity->Type() == EntityType::BlockReference) {
            const auto& block = static_cast<const BlockReferenceEntity&>(*entity);
            if (const BlockDefinition* def = document_.FindBlockDefinition(block.BlockName())) {
                for (const GripInfo& grip : block.DynamicGrips(def)) {
                    auto* gripItem = new GripItem(entity->Id(), grip.index, grip.position, grip.kind);
                    grips_.push_back(gripItem);
                    addItem(gripItem);
                }
            }
        }
    }
    RebuildSplineControlFrames();
}

void CadScene::RebuildSplineControlFrames()
{
    QPainterPath hull;
    std::map<EntityId, std::vector<std::pair<int, QPointF>>> byEntity;
    for (GripItem* grip : grips_) {
        if (!grip || grip->Kind() != GripKind::ControlVertex) {
            continue;
        }
        byEntity[grip->EntityIdValue()].push_back({grip->GripIndex(), grip->pos()});
    }
    for (auto& entry : byEntity) {
        auto& pts = entry.second;
        std::sort(pts.begin(), pts.end(), [](const auto& a, const auto& b) {
            return a.first < b.first;
        });
        if (pts.size() < 2) {
            continue;
        }
        hull.moveTo(pts.front().second);
        for (std::size_t i = 1; i < pts.size(); ++i) {
            hull.lineTo(pts[i].second);
        }
    }

    if (hull.isEmpty()) {
        if (splineControlFrameItem_) {
            splineControlFrameItem_->hide();
        }
        return;
    }

    if (!splineControlFrameItem_ || splineControlFrameItem_->scene() != this) {
        if (splineControlFrameItem_) {
            delete splineControlFrameItem_;
        }
        splineControlFrameItem_ = addPath(QPainterPath(), QPen());
        splineControlFrameItem_->setZValue(1990.0);
        splineControlFrameItem_->setAcceptedMouseButtons(Qt::NoButton);
        splineControlFrameItem_->setFlag(QGraphicsItem::ItemIsSelectable, false);
    }
    QPen hullPen(QColor(210, 140, 70), 0);
    hullPen.setCosmetic(true);
    hullPen.setStyle(Qt::DashLine);
    hullPen.setCapStyle(Qt::FlatCap);
    splineControlFrameItem_->setPen(hullPen);
    splineControlFrameItem_->setBrush(Qt::NoBrush);
    splineControlFrameItem_->setPath(hull);
    splineControlFrameItem_->show();
}

GripItem* CadScene::HitGrip(const QPointF& scenePoint, double tolerance) const
{
    GripItem* best = nullptr;
    GripItem* toggle = nullptr;
    double bestDist = tolerance;
    double toggleDist = tolerance;
    for (GripItem* grip : grips_) {
        if (!grip || grip->scene() != this) {
            continue;
        }
        const double dist = QLineF(grip->scenePos(), scenePoint).length();
        if (dist > tolerance) {
            continue;
        }
        if (grip->Kind() == GripKind::MethodToggle) {
            if (dist <= toggleDist) {
                toggle = grip;
                toggleDist = dist;
            }
            continue;
        }
        if (dist <= bestDist) {
            best = grip;
            bestDist = dist;
        }
    }
    return best ? best : toggle;
}

void CadScene::UpdateGripPosition(EntityId entityId, int gripIndex, const GeoPoint& modelPoint)
{
    for (GripItem* grip : grips_) {
        if (grip && grip->EntityIdValue() == entityId && grip->GripIndex() == gripIndex) {
            grip->SetModelPoint(modelPoint);
            RebuildSplineControlFrames();
            return;
        }
    }
}

void CadScene::SetHotGrip(EntityId entityId, int gripIndex)
{
    for (GripItem* grip : grips_) {
        if (!grip) {
            continue;
        }
        grip->SetHot(grip->EntityIdValue() == entityId && grip->GripIndex() == gripIndex);
    }
}

void CadScene::ClearHotGrips()
{
    for (GripItem* grip : grips_) {
        if (grip) {
            grip->SetHot(false);
        }
    }
}

EntityGraphicsItem* CadScene::FindItem(EntityId id) const
{
    return items_.value(id, nullptr);
}

void CadScene::SetHoveredEntity(EntityId id)
{
    if (hoveredEntityId_ == id) {
        return;
    }

    if (EntityGraphicsItem* previous = items_.value(hoveredEntityId_, nullptr)) {
        previous->SetHovered(false);
    }

    hoveredEntityId_ = id;

    if (EntityGraphicsItem* current = items_.value(hoveredEntityId_, nullptr)) {
        current->SetHovered(true);
    }
}

void CadScene::SetEntityVisible(EntityId id, bool visible)
{
    if (EntityGraphicsItem* item = items_.value(id, nullptr)) {
        item->setVisible(visible);
    }
    for (GripItem* grip : grips_) {
        if (grip && grip->EntityIdValue() == id) {
            grip->setVisible(visible);
        }
    }
}

} // namespace cad
