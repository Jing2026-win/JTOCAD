#pragma once

#include "Base/CadTypes.h"
#include "DataModel/IDocumentObserver.h"
#include "Geometry/GeoPoint.h"
#include <QGraphicsPathItem>
#include <QGraphicsScene>
#include <QHash>
#include <QList>
#include <functional>
#include <vector>

namespace cad {

class CadDocument;
class EntityGraphicsItem;
class GripItem;

class CadScene : public QGraphicsScene, public IDocumentObserver {
public:
    explicit CadScene(CadDocument& document, QObject* parent = nullptr);
    ~CadScene() override;

    void OnEntityAdded(const Entity& entity) override;
    void OnEntityRemoved(EntityId id) override;
    void OnEntitiesRemoved(const std::vector<EntityId>& ids) override;
    void OnEntitiesChanged(const std::vector<EntityId>& ids) override;
    void OnSelectionChanged() override;
    void OnDocumentReset() override;

    void SetSelectionDragOffset(double dx, double dy);
    void ClearSelectionDragOffset();
    void RebuildGrips();
    void InvalidateItemRegions(const QRectF& extra = QRectF());
    void SetViewportRefreshCallback(std::function<void()> callback);
    void UpdateGripPosition(EntityId entityId, int gripIndex, const GeoPoint& modelPoint);
    void SetHotGrip(EntityId entityId, int gripIndex);
    void ClearHotGrips();
    GripItem* HitGrip(const QPointF& scenePoint, double tolerance) const;
    void SetHoveredEntity(EntityId id);
    void SetEntityVisible(EntityId id, bool visible);

private:
    void ClearGrips();
    void RemoveGripsForEntity(EntityId id);
    void RebuildAllItems();
    void ExpandSceneRectForItem(EntityGraphicsItem* item);
    void ReleaseGraphicsItem(EntityGraphicsItem* item);
    void ReleaseGripItem(GripItem* grip);
    EntityGraphicsItem* FindItem(EntityId id) const;
    void RebuildSplineControlFrames();

    CadDocument& document_;
    QHash<EntityId, EntityGraphicsItem*> items_;
    QList<GripItem*> grips_;
    QGraphicsPathItem* splineControlFrameItem_ = nullptr;
    std::function<void()> viewportRefreshCallback_;
    EntityId hoveredEntityId_ = 0;
};

} // namespace cad
