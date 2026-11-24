#pragma once

#include "Command/CommandManager.h"
#include "DataModel/CadDocument.h"
#include "DataModel/IDocumentObserver.h"
#include <QWidget>
#include <functional>
#include <optional>

class QToolButton;

namespace cad {

class PropertiesRibbonWidget : public QWidget, public IDocumentObserver {
public:
    PropertiesRibbonWidget(
        CadDocument& document,
        CommandManager& commandManager,
        QWidget* parent = nullptr);
    ~PropertiesRibbonWidget() override;

    void SetStatusCallback(std::function<void(const QString&)> callback);
    void SetOpenPropertiesPaletteCallback(std::function<void()> callback);
    void OpenPropertiesPalette();
    void StartMatchProperties();
    void CancelMatchProperties();

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

    void OnEntityAdded(const Entity& entity) override;
    void OnEntityRemoved(EntityId id) override;
    void OnEntitiesRemoved(const std::vector<EntityId>& ids) override;
    void OnEntitiesChanged(const std::vector<EntityId>& ids) override;
    void OnSelectionChanged() override;
    void OnDocumentReset() override;

private:
    enum class MatchPhase {
        Idle,
        WaitingSource,
        WaitingTargets
    };

    void NotifyStatus(const QString& text) const;
    void RefreshDisplay();
    EntityStyle DisplayStyle() const;
    std::string DisplayLayerName() const;
    void ApplyStyleMutation(const std::function<void(EntityStyle&)>& mutator);
    void ShowColorMenu();
    void ShowLineWeightMenu();
    void ShowLineTypeMenu();

    CadDocument& document_;
    CommandManager& commandManager_;
    QToolButton* colorButton_ = nullptr;
    QToolButton* lineWeightButton_ = nullptr;
    QToolButton* lineTypeButton_ = nullptr;
    MatchPhase matchPhase_ = MatchPhase::Idle;
    std::optional<EntityId> matchSourceId_;
    std::function<void(const QString&)> statusCallback_;
    std::function<void()> openPropertiesPaletteCallback_;
};

} // namespace cad
