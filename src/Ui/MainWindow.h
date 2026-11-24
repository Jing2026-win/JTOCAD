#pragma once

#include "Command/CommandManager.h"
#include "DataModel/CadDocument.h"
#include "Edit/RefEditSession.h"
#include "View/CadScene.h"
#include "View/CadView.h"
#include <SARibbonMainWindow.h>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <memory>

class SARibbonContextCategory;

namespace cad {

class LayerManagerDialog;
class PropertiesRibbonWidget;
class BlockRibbonWidget;
class DesignCenterDialog;
class BlocksPaletteDialog;

class MainWindow : public SARibbonMainWindow {
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

protected:
    bool event(QEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    void SetupRibbon();
    void ApplyAutoCadTheme();
    void StyleRibbonInputWidgets();
    void ShowPageLayoutDialog();
    void ShowObjectSnapSettings();
    void ShowDimensionStyleDialog();
    void ShowCadOptions();
    void ShowGraphicsConfig();
    void ShowViewResDialog();
    void ShowLayerManager();
    void ShowLineTypeManager();
    void ShowPropertiesPalette();
    void ShowDesignCenter();
    void ShowBlocksPalette();
    void RefreshDrawMethodCombo(ToolMode mode);
    void UpdateArcParamSpin(DrawMethod method);

    void OnCreateBlock();
    void OnInsertBlock();
    void OnEditBlock();
    void OnEditBlockAttributes();
    void OnDetectBlocks();
    void OnExplodeBlocks();
    void OnPurgeBlocks();
    void OnWblock();
    void OnDemoDynamicBlock();
    void OnRefEdit();
    void OnRefEditClose(bool save);
    void OnRefEditSetAdd();
    void OnRefEditSetRemove();
    void UpdateRefEditRibbon(bool active);
    void StartRectangularArray();
    void StartPathArray();
    void StartPolarArray();

    CadDocument document_;
    CommandManager commandManager_;
    CadScene* scene_ = nullptr;
    CadView* view_ = nullptr;
    PropertiesRibbonWidget* propertiesRibbonWidget_ = nullptr;
    BlockRibbonWidget* blockRibbonWidget_ = nullptr;
    QComboBox* drawMethodCombo_ = nullptr;
    QSpinBox* polygonSidesSpin_ = nullptr;
    QDoubleSpinBox* cornerDistanceSpin_ = nullptr;
    QDoubleSpinBox* chamferDistance2Spin_ = nullptr;
    QDoubleSpinBox* arcParamSpin_ = nullptr;
    RectangularArrayOptions rectangularArrayOptions_;
    PathArrayOptions pathArrayOptions_;
    PolarArrayOptions polarArrayOptions_;
    LayerManagerDialog* layerManagerDialog_ = nullptr;
    DesignCenterDialog* designCenterDialog_ = nullptr;
    BlocksPaletteDialog* blocksPaletteDialog_ = nullptr;
    std::unique_ptr<RefEditSession> refEditSession_;
    SARibbonContextCategory* refEditContext_ = nullptr;
};

} // namespace cad
