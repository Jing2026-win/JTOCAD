#pragma once

#include "Command/CommandManager.h"
#include "DataModel/CadDocument.h"
#include "DataModel/DynamicBlock.h"
#include "View/CadScene.h"
#include "View/CadView.h"
#include <QDialog>
#include <string>

class QLabel;
class QListWidget;

namespace cad {

// AutoCAD-like BEDIT: isolated editor with BSAVE / BSAVEAS / BCLOSE
// and a "块编写选项板" for dynamic-block parameters / actions / visibility.
class BlockEditorDialog : public QDialog {
public:
    BlockEditorDialog(
        CadDocument& hostDocument,
        const std::string& blockName,
        QWidget* parent = nullptr);
    ~BlockEditorDialog() override;

    bool definitionChanged() const { return definitionChanged_; }

private:
    class DirtyObserver;

    void LoadDefinitionIntoEditor();
    bool CollectEditorEntities(std::vector<std::shared_ptr<Entity>>& out) const;
    std::vector<std::size_t> SelectedEntityIndices() const;
    void RefreshDynamicLists();
    void PersistDynamicToHost(BlockDefinition& def) const;

    void OnAddLinearParam();
    void OnAddPointParam();
    void OnAddRotationParam();
    void OnAddFlipParam();
    void OnAddVisibilityParam();
    void OnAddLookupParam();
    void OnAddStretchAction();
    void OnAddMoveAction();
    void OnAddScaleAction();
    void OnAddRotateAction();
    void OnAddFlipAction();
    void OnAddArrayAction();
    void OnEditVisibilityStates();
    void OnEditLookupTable();
    void OnDeleteSelectedParam();
    void OnDeleteSelectedAction();

    void OnSave();
    void OnSaveAs();
    void OnCloseEditor();
    void reject() override;

    CadDocument& hostDocument_;
    std::string blockName_;
    CadDocument editDocument_;
    CommandManager editCommands_;
    CadScene* editScene_ = nullptr;
    CadView* editView_ = nullptr;
    QLabel* titleLabel_ = nullptr;
    QListWidget* paramList_ = nullptr;
    QListWidget* actionList_ = nullptr;
    DirtyObserver* dirtyObserver_ = nullptr;
    DynamicBlockData dynamicData_;
    bool dirty_ = false;
    bool definitionChanged_ = false;
    bool closing_ = false;
    int paramSerial_ = 1;
    int actionSerial_ = 1;
};

} // namespace cad
