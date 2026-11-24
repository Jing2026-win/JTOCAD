#pragma once

#include "DataModel/CadDocument.h"
#include "DataModel/IDocumentObserver.h"
#include <QWidget>

class QLabel;
class QToolButton;

namespace cad {

class Entity;

class LayerControlBar : public QWidget, public IDocumentObserver {
public:
    explicit LayerControlBar(CadDocument& document, QWidget* parent = nullptr);
    ~LayerControlBar() override;

    void OnEntityAdded(const Entity& entity) override;
    void OnEntityRemoved(EntityId id) override;
    void OnEntitiesRemoved(const std::vector<EntityId>& ids) override;
    void OnEntitiesChanged(const std::vector<EntityId>& ids) override;
    void OnSelectionChanged() override;
    void OnDocumentReset() override;

private:
    void RefreshFromDocument();
    Layer* CurrentLayer();
    const Layer* CurrentLayer() const;
    void ToggleOn();
    void ToggleFreeze();
    void ToggleLock();
    void PickColor();
    void ShowLayerSelector();

    CadDocument& document_;
    QToolButton* onButton_ = nullptr;
    QToolButton* freezeButton_ = nullptr;
    QToolButton* lockButton_ = nullptr;
    QToolButton* colorButton_ = nullptr;
    QToolButton* layerButton_ = nullptr;
    QToolButton* dropdownButton_ = nullptr;
};

} // namespace cad
