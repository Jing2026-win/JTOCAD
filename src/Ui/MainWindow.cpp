#include "View/CadOptionsDialog.h"
#include "View/DimensionStyleDialog.h"
#include "View/GraphicsConfigDialog.h"
#include "View/LayerManagerDialog.h"
#include "View/LineTypeManagerDialog.h"
#include "View/ObjectSnapSettingsDialog.h"
#include "View/PropertiesPaletteDialog.h"
#include "View/BlockDefinitionDialog.h"
#include "View/BlockInsertDialog.h"
#include "View/BlockEditDialog.h"
#include "View/BlockAttributeEditDialog.h"
#include "View/BlockEditorDialog.h"
#include "View/DesignCenterDialog.h"
#include "View/BlocksPaletteDialog.h"
#include "Ui/BlockRibbonWidget.h"
#include "Ui/LayerRibbonWidget.h"
#include "Ui/PropertiesRibbonWidget.h"
#include "Ui/MainWindow.h"

#include "Base/IdGenerator.h"
#include "Base/ViewDisplaySettings.h"
#include "Command/DrawToolEngine.h"
#include "DataModel/BlockReferenceEntity.h"
#include "DataModel/DynamicBlockEvaluator.h"
#include "DataModel/LineEntity.h"
#include "DataModel/RectangleEntity.h"
#include "Geometry/GeoLine.h"
#include "Geometry/GeoPrimitives.h"
#include "IO/DxfService.h"
#include "Ui/PrintService.h"
#include <QAction>
#include <QApplication>
#include <QEvent>
#include <QKeyEvent>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QInputDialog>
#include <QLineEdit>
#include <QMessageBox>
#include <QMenu>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QSpinBox>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QAbstractButton>
#include <QPalette>
#include <QToolTip>
#include <SARibbonBar.h>
#include <SARibbonCategory.h>
#include <SARibbonContextCategory.h>
#include <SARibbonPanel.h>
#include <SARibbonTitleIconWidget.h>
#include <SARibbonUtil.h>
#include <algorithm>
#include <cmath>
#include <map>
#include <vector>

namespace cad {

MainWindow::MainWindow(QWidget* parent)
    : SARibbonMainWindow(parent,
                         SARibbonMainWindowStyleFlag::UseRibbonMenuBar
                             | SARibbonMainWindowStyleFlag::UseNativeFrame)
    , commandManager_(document_)
{
    setWindowTitle(tr("ChineseCAD - 2D Edit Kernel"));
    resize(1280, 800);
    refEditSession_ = std::make_unique<RefEditSession>(document_);

    scene_ = new CadScene(document_, this);
    view_ = new CadView(this);
    view_->SetScene(scene_);
    view_->SetDocument(&document_);
    view_->SetCommandManager(&commandManager_);
    view_->SetStatusCallback([this](const QString& text) {
        statusBar()->showMessage(text);
    });
    view_->SetToolModeChangedCallback([this](ToolMode mode) {
        RefreshDrawMethodCombo(mode);
    });
    setCentralWidget(view_);

    SetupRibbon();
    qApp->installEventFilter(this);

    QTimer::singleShot(0, this, [this]() {
        ApplyAutoCadTheme();
    });

    statusBar()->showMessage(tr("选择模式：左键框选；中/右键平移；绘制时 Enter/空格/右键结束当前图形，再按 Esc 返回选择；「栅格」控制栅格显示（默认显示）；「捕捉」控制对象捕捉，设置中可勾选「捕捉栅格」"));
}

bool MainWindow::event(QEvent* event)
{
    if (event && (event->type() == QEvent::KeyPress || event->type() == QEvent::ShortcutOverride)
        && !QApplication::activeModalWidget()
        && !QApplication::activePopupWidget()) {
        auto* keyEvent = static_cast<QKeyEvent*>(event);
        if (keyEvent->key() == Qt::Key_Escape && view_ && view_->CancelActiveCommand()) {
            event->accept();
            return true;
        }
    }
    return SARibbonMainWindow::event(event);
}

void MainWindow::keyPressEvent(QKeyEvent* event)
{
    if (event && event->key() == Qt::Key_Escape
        && !QApplication::activeModalWidget()
        && !QApplication::activePopupWidget()
        && view_ && view_->CancelActiveCommand()) {
        event->accept();
        return;
    }
    SARibbonMainWindow::keyPressEvent(event);
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event)
{
    if (event && event->type() == QEvent::KeyPress
        && !QApplication::activeModalWidget()
        && !QApplication::activePopupWidget()) {
        auto* keyEvent = static_cast<QKeyEvent*>(event);
        if (keyEvent->key() == Qt::Key_Escape && view_
            && view_->CurrentToolMode() != ToolMode::Select
            && view_->CancelActiveCommand()) {
            return true;
        }
    }
    return SARibbonMainWindow::eventFilter(watched, event);
}

void MainWindow::ApplyAutoCadTheme()
{
    setRibbonTheme(SARibbonTheme::RibbonThemeDark2);
    setStyleSheet(styleSheet() + QStringLiteral(R"(
        QStatusBar { background-color: #252526; color: #cccccc; border-top: 1px solid #007acc; }
        QStatusBar::item { border: none; }
        SARibbonMainWindow { background-color: #2d2d30; }
        QToolTip {
            color: #f3f3f3;
            background-color: #2d2d30;
            border: 1px solid #6a6a6a;
            padding: 4px 6px;
        }
        QComboBox, QSpinBox, QDoubleSpinBox {
            background-color: #333337;
            color: #f3f3f3;
            border: 1px solid #555555;
            border-radius: 2px;
            min-height: 20px;
            padding: 1px 4px;
        }
        QComboBox::drop-down, QSpinBox::up-button, QSpinBox::down-button,
        QDoubleSpinBox::up-button, QDoubleSpinBox::down-button {
            background-color: #3e3e42;
            border: none;
        }
        QMenu {
            background-color: #2d2d30;
            color: #f3f3f3;
            border: 1px solid #555555;
        }
        QMenu::item:selected { background-color: #094771; }
    )"));

    {
        QPalette tipPalette = QToolTip::palette();
        tipPalette.setColor(QPalette::ToolTipBase, QColor(0x2d, 0x2d, 0x30));
        tipPalette.setColor(QPalette::ToolTipText, QColor(0xf3, 0xf3, 0xf3));
        tipPalette.setColor(QPalette::Window, QColor(0x2d, 0x2d, 0x30));
        tipPalette.setColor(QPalette::WindowText, QColor(0xf3, 0xf3, 0xf3));
        tipPalette.setColor(QPalette::Text, QColor(0xf3, 0xf3, 0xf3));
        QToolTip::setPalette(tipPalette);
    }

    if (SARibbonBar* ribbon = ribbonBar()) {
        ribbon->setWindowTitleTextColor(QColor(0xf3, 0xf3, 0xf3));
        ribbon->setEnableShowPanelTitle(true);
        ribbon->setPanelToolButtonIconSize(QSize(18, 18), QSize(32, 32));
        // Native frame: hide ribbon title icon (OS title bar already shows app icon).
        if (SARibbonTitleIconWidget* iconWidget = ribbon->titleIconWidget()) {
            iconWidget->hide();
        }
        if (QAbstractButton* appBtn = ribbon->applicationButton()) {
            appBtn->setText(QStringLiteral("C"));
            appBtn->setToolTip(tr("ChineseCAD"));
        }
    }

    StyleRibbonInputWidgets();
}

void MainWindow::StyleRibbonInputWidgets()
{
    const auto styleInput = [](QWidget* widget) {
        if (!widget) {
            return;
        }
        widget->setMinimumHeight(24);
    };
    styleInput(drawMethodCombo_);
    styleInput(arcParamSpin_);
    styleInput(polygonSidesSpin_);
    styleInput(cornerDistanceSpin_);
}

void MainWindow::SetupRibbon()
{
    SARibbonBar* ribbon = ribbonBar();
    if (!ribbon) {
        return;
    }
    ribbon->setRibbonStyle(SARibbonBar::RibbonStyleLooseThreeRow);

    const auto activateTool = [this](ToolMode mode, const QString& text) {
        view_->SetToolMode(mode);
        view_->setFocus(Qt::OtherFocusReason);
        QTimer::singleShot(0, view_, [this]() {
            if (view_) {
                view_->setFocus(Qt::OtherFocusReason);
            }
        });
        RefreshDrawMethodCombo(mode);
        if (mode == ToolMode::Mirror) {
            statusBar()->showMessage(tr("镜像：选择对象 → 指定镜像线 → Y/N 是否删除源对象"));
        } else if (mode == ToolMode::DrawRay) {
            statusBar()->showMessage(tr("射线：指定起点 → 指定通过点（可连续创建多条，Enter/空格/右键结束当前；再按 Esc 返回选择）"));
        } else if (mode != ToolMode::Fillet && mode != ToolMode::Chamfer) {
            statusBar()->showMessage(tr("当前命令：%1").arg(text));
        }
    };

    const auto addToolAction = [this, &activateTool](SARibbonPanel* panel,
                                                     const QString& text,
                                                     const QKeySequence& shortcut,
                                                     ToolMode mode,
                                                     bool large = false) {
        QAction* action = new QAction(text, this);
        if (large) {
            panel->addLargeAction(action);
        } else {
            panel->addSmallAction(action);
        }
        if (!shortcut.isEmpty()) {
            action->setShortcut(shortcut);
            addAction(action);
        }
        connect(action, &QAction::triggered, this, [this, mode, text, activateTool]() {
            activateTool(mode, text);
        });
    };

    // --- 常用 ---
    SARibbonCategory* homeCategory = ribbon->addCategoryPage(tr("常用"));
    SARibbonPanel* selectPanel = homeCategory->addPanel(tr("选择"));
    addToolAction(selectPanel, tr("选择"), QKeySequence(Qt::Key_S), ToolMode::Select, true);

    SARibbonPanel* historyPanel = homeCategory->addPanel(tr("编辑"));
    QAction* deleteAction = new QAction(tr("删除"), this);
    historyPanel->addSmallAction(deleteAction);
    deleteAction->setShortcut(QKeySequence::Delete);
    addAction(deleteAction);
    connect(deleteAction, &QAction::triggered, this, [this]() {
        view_->ResetInteractionState();
        commandManager_.DeleteSelected();
    });

    QAction* undoAction = new QAction(tr("撤销"), this);
    historyPanel->addSmallAction(undoAction);
    undoAction->setShortcut(QKeySequence::Undo);
    addAction(undoAction);
    connect(undoAction, &QAction::triggered, this, [this]() {
        view_->ResetInteractionState();
        commandManager_.Undo();
    });

    QAction* redoAction = new QAction(tr("重做"), this);
    historyPanel->addSmallAction(redoAction);
    redoAction->setShortcut(QKeySequence::Redo);
    addAction(redoAction);
    connect(redoAction, &QAction::triggered, this, [this]() {
        view_->ResetInteractionState();
        commandManager_.Redo();
    });

    SARibbonPanel* layerPanel = homeCategory->addPanel(tr("图层"));
    layerPanel->setPanelLayoutMode(SARibbonPanel::ThreeRowMode);
    auto* layerRibbonWidget = new LayerRibbonWidget(document_, this);
    layerRibbonWidget->SetStatusCallback([this](const QString& text) {
        statusBar()->showMessage(text, 3000);
    });
    layerRibbonWidget->SetOpenLayerManagerCallback([this]() { ShowLayerManager(); });
    layerPanel->addLargeWidget(layerRibbonWidget);

    // 为图层面板内容区预留足够高度，避免快捷图标压住面板标题「图层」
    const int layerCategoryHeight =
        layerRibbonWidget->sizeHint().height() + ribbon->panelTitleHeight() + 6;
    if (ribbon->categoryHeight() < layerCategoryHeight) {
        ribbon->setCategoryHeight(layerCategoryHeight);
    }

    auto* layerManagerShortcut = new QAction(tr("图层特性"), this);
    layerManagerShortcut->setShortcut(QKeySequence(QStringLiteral("LA")));
    addAction(layerManagerShortcut);
    connect(layerManagerShortcut, &QAction::triggered, this, &MainWindow::ShowLayerManager);

    SARibbonPanel* propertiesPanel = homeCategory->addPanel(tr("特性"));
    propertiesPanel->setPanelLayoutMode(SARibbonPanel::ThreeRowMode);
    propertiesRibbonWidget_ = new PropertiesRibbonWidget(document_, commandManager_, this);
    propertiesRibbonWidget_->SetStatusCallback([this](const QString& text) {
        statusBar()->showMessage(text, 4000);
    });
    propertiesRibbonWidget_->SetOpenPropertiesPaletteCallback([this]() { ShowPropertiesPalette(); });
    propertiesPanel->addLargeWidget(propertiesRibbonWidget_);

    auto* propertiesOptionAction = new QAction(tr("特性选项板"), this);
    propertiesOptionAction->setToolTip(tr("打开完整特性选项板 (Ctrl+1)"));
    connect(propertiesOptionAction, &QAction::triggered, this, &MainWindow::ShowPropertiesPalette);
    propertiesPanel->setOptionAction(propertiesOptionAction);

    auto* propertiesShortcut = new QAction(tr("特性选项板"), this);
    propertiesShortcut->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_1));
    addAction(propertiesShortcut);
    connect(propertiesShortcut, &QAction::triggered, this, &MainWindow::ShowPropertiesPalette);

    auto* matchPropShortcut = new QAction(tr("特性匹配"), this);
    matchPropShortcut->setShortcut(QKeySequence(QStringLiteral("MA")));
    addAction(matchPropShortcut);
    connect(matchPropShortcut, &QAction::triggered, this, [this]() {
        if (propertiesRibbonWidget_) {
            propertiesRibbonWidget_->StartMatchProperties();
        }
    });

    auto* lineTypeManagerShortcut = new QAction(tr("线型管理器"), this);
    lineTypeManagerShortcut->setShortcut(QKeySequence(QStringLiteral("LT")));
    addAction(lineTypeManagerShortcut);
    connect(lineTypeManagerShortcut, &QAction::triggered, this, &MainWindow::ShowLineTypeManager);

    SARibbonPanel* blockPanel = homeCategory->addPanel(tr("块"));
    blockPanel->setPanelLayoutMode(SARibbonPanel::ThreeRowMode);
    blockRibbonWidget_ = new BlockRibbonWidget(this);
    blockRibbonWidget_->SetInsertCallback([this]() { OnInsertBlock(); });
    blockRibbonWidget_->SetDetectCallback([this]() { OnDetectBlocks(); });
    blockRibbonWidget_->SetCreateCallback([this]() { OnCreateBlock(); });
    blockRibbonWidget_->SetEditCallback([this]() { OnEditBlock(); });
    blockRibbonWidget_->SetEditAttributesCallback([this]() { OnEditBlockAttributes(); });
    blockRibbonWidget_->SetExplodeCallback([this]() { OnExplodeBlocks(); });
    blockRibbonWidget_->SetPurgeCallback([this]() { OnPurgeBlocks(); });
    blockRibbonWidget_->SetWblockCallback([this]() { OnWblock(); });
    blockRibbonWidget_->SetDemoDynamicBlockCallback([this]() { OnDemoDynamicBlock(); });
    blockRibbonWidget_->SetRefEditCallback([this]() { OnRefEdit(); });
    blockRibbonWidget_->SetDesignCenterCallback([this]() { ShowDesignCenter(); });
    blockRibbonWidget_->SetBlocksPaletteCallback([this]() { ShowBlocksPalette(); });
    blockPanel->addLargeWidget(blockRibbonWidget_);

    auto* createBlockShortcut = new QAction(tr("创建块"), this);
    createBlockShortcut->setShortcut(QKeySequence(Qt::Key_B));
    addAction(createBlockShortcut);
    connect(createBlockShortcut, &QAction::triggered, this, &MainWindow::OnCreateBlock);

    auto* insertBlockShortcut = new QAction(tr("插入块"), this);
    insertBlockShortcut->setShortcut(QKeySequence(Qt::Key_I));
    addAction(insertBlockShortcut);
    connect(insertBlockShortcut, &QAction::triggered, this, &MainWindow::OnInsertBlock);

    auto* wblockShortcut = new QAction(tr("写块"), this);
    wblockShortcut->setShortcut(QKeySequence(Qt::Key_W));
    addAction(wblockShortcut);
    connect(wblockShortcut, &QAction::triggered, this, &MainWindow::OnWblock);

    auto* adcShortcut = new QAction(tr("设计中心"), this);
    adcShortcut->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_2));
    addAction(adcShortcut);
    connect(adcShortcut, &QAction::triggered, this, &MainWindow::ShowDesignCenter);

    auto* paletteShortcut = new QAction(tr("块选项板"), this);
    paletteShortcut->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_3));
    addAction(paletteShortcut);
    connect(paletteShortcut, &QAction::triggered, this, &MainWindow::ShowBlocksPalette);

    refEditContext_ = ribbon->addContextCategory(tr("在位编辑 REFEDIT"), QColor(0, 122, 204));
    SARibbonCategory* refEditCategory = refEditContext_->addCategoryPage(tr("参照编辑"));
    SARibbonPanel* refEditPanel = refEditCategory->addPanel(tr("REFEDIT"));
    auto* refSaveAction = new QAction(tr("REFCLOSE 保存"), this);
    auto* refDiscardAction = new QAction(tr("REFCLOSE 放弃"), this);
    auto* refAddAction = new QAction(tr("REFSET 添加"), this);
    auto* refRemoveAction = new QAction(tr("REFSET 移除"), this);
    refEditPanel->addLargeAction(refSaveAction);
    refEditPanel->addLargeAction(refDiscardAction);
    refEditPanel->addLargeAction(refAddAction);
    refEditPanel->addLargeAction(refRemoveAction);
    connect(refSaveAction, &QAction::triggered, this, [this]() { OnRefEditClose(true); });
    connect(refDiscardAction, &QAction::triggered, this, [this]() { OnRefEditClose(false); });
    connect(refAddAction, &QAction::triggered, this, &MainWindow::OnRefEditSetAdd);
    connect(refRemoveAction, &QAction::triggered, this, &MainWindow::OnRefEditSetRemove);
    ribbon->hideContextCategory(refEditContext_);

    SARibbonPanel* assistPanel = homeCategory->addPanel(tr("辅助"));
    QAction* gridAction = new QAction(tr("栅格"), this);
    assistPanel->addSmallAction(gridAction);
    gridAction->setCheckable(true);
    gridAction->setChecked(true);
    connect(gridAction, &QAction::toggled, view_, &CadView::SetGridEnabled);

    QAction* snapAction = new QAction(tr("捕捉"), this);
    snapAction->setCheckable(true);
    snapAction->setChecked(true);
    connect(snapAction, &QAction::toggled, view_, &CadView::SetSnapEnabled);
    auto* snapMenu = new QMenu(this);
    snapMenu->addAction(tr("对象捕捉设置..."), this, &MainWindow::ShowObjectSnapSettings);
    assistPanel->addSmallAction(snapAction, QToolButton::MenuButtonPopup);
    snapAction->setMenu(snapMenu);

    QAction* orthoAction = new QAction(tr("正交"), this);
    assistPanel->addSmallAction(orthoAction);
    orthoAction->setCheckable(true);
    orthoAction->setShortcut(QKeySequence(Qt::Key_F8));
    addAction(orthoAction);
    connect(orthoAction, &QAction::toggled, view_, &CadView::SetOrthoEnabled);

    SARibbonPanel* viewPanel = homeCategory->addPanel(tr("视图"));
    QAction* zoomExtentsAction = new QAction(tr("范围缩放"), this);
    zoomExtentsAction->setShortcut(QKeySequence(QStringLiteral("ZE")));
    addAction(zoomExtentsAction);
    viewPanel->addSmallAction(zoomExtentsAction);
    connect(zoomExtentsAction, &QAction::triggered, view_, [this]() {
        view_->ZoomExtents();
        statusBar()->showMessage(tr("已缩放到图形范围（双击中键同样可触发）"), 3000);
    });

    QAction* zoomAllAction = new QAction(tr("全部缩放"), this);
    viewPanel->addSmallAction(zoomAllAction);
    connect(zoomAllAction, &QAction::triggered, view_, &CadView::ZoomAll);

    QAction* zoomInAction = new QAction(tr("放大"), this);
    zoomInAction->setShortcut(QKeySequence(QStringLiteral("ZI")));
    addAction(zoomInAction);
    viewPanel->addSmallAction(zoomInAction);
    connect(zoomInAction, &QAction::triggered, view_, &CadView::ZoomIn);

    QAction* zoomOutAction = new QAction(tr("缩小"), this);
    zoomOutAction->setShortcut(QKeySequence(QStringLiteral("ZO")));
    addAction(zoomOutAction);
    viewPanel->addSmallAction(zoomOutAction);
    connect(zoomOutAction, &QAction::triggered, view_, &CadView::ZoomOut);

    QAction* viewResAction = new QAction(tr("视图分辨率"), this);
    viewResAction->setShortcut(QKeySequence(QStringLiteral("VR")));
    addAction(viewResAction);
    viewPanel->addSmallAction(viewResAction);
    connect(viewResAction, &QAction::triggered, this, &MainWindow::ShowViewResDialog);

    QAction* graphicsConfigAction = new QAction(tr("图形性能"), this);
    graphicsConfigAction->setShortcut(QKeySequence(QStringLiteral("GC")));
    addAction(graphicsConfigAction);
    viewPanel->addSmallAction(graphicsConfigAction);
    connect(graphicsConfigAction, &QAction::triggered, this, &MainWindow::ShowGraphicsConfig);

    // --- 绘图 ---
    SARibbonCategory* drawCategory = ribbon->addCategoryPage(tr("绘图"));
    SARibbonPanel* basicDrawPanel = drawCategory->addPanel(tr("基本"));
    addToolAction(basicDrawPanel, tr("点"), QKeySequence(Qt::Key_D), ToolMode::DrawPoint);
    addToolAction(basicDrawPanel, tr("直线"), QKeySequence(Qt::Key_L), ToolMode::DrawLine);
    addToolAction(basicDrawPanel, tr("射线"), QKeySequence(), ToolMode::DrawRay);
    addToolAction(basicDrawPanel, tr("构造线"), QKeySequence(), ToolMode::DrawXLine);

    SARibbonPanel* shapeDrawPanel = drawCategory->addPanel(tr("形状"));
    addToolAction(shapeDrawPanel, tr("矩形"), QKeySequence(), ToolMode::DrawRectangle);
    addToolAction(shapeDrawPanel, tr("多边形"), QKeySequence(), ToolMode::DrawPolygon);
    addToolAction(shapeDrawPanel, tr("圆"), QKeySequence(Qt::Key_C), ToolMode::DrawCircle);
    addToolAction(shapeDrawPanel, tr("圆弧"), QKeySequence(Qt::Key_A), ToolMode::DrawArc);

    SARibbonPanel* curveDrawPanel = drawCategory->addPanel(tr("曲线"));
    addToolAction(curveDrawPanel, tr("椭圆"), QKeySequence(), ToolMode::DrawEllipse);
    addToolAction(curveDrawPanel, tr("抛物线"), QKeySequence(), ToolMode::DrawParabola);
    addToolAction(curveDrawPanel, tr("双曲线"), QKeySequence(), ToolMode::DrawHyperbola);
    addToolAction(curveDrawPanel, tr("样条"), QKeySequence(), ToolMode::DrawSpline);
    addToolAction(curveDrawPanel, tr("多段线"), QKeySequence(Qt::Key_P), ToolMode::DrawPolyline);

    SARibbonPanel* drawOptionPanel = drawCategory->addPanel(tr("选项"));
    drawMethodCombo_ = new QComboBox(this);
    drawMethodCombo_->setMinimumWidth(140);
    drawOptionPanel->addSmallWidget(drawMethodCombo_);
    connect(drawMethodCombo_, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (index < 0) {
            return;
        }
        const DrawMethod method = static_cast<DrawMethod>(drawMethodCombo_->itemData(index).toInt());
        view_->SetDrawMethod(method);
        UpdateArcParamSpin(method);
    });

    arcParamSpin_ = new QDoubleSpinBox(this);
    arcParamSpin_->setRange(-100000.0, 100000.0);
    arcParamSpin_->setDecimals(3);
    arcParamSpin_->setValue(90.0);
    arcParamSpin_->setPrefix(tr("角度:"));
    arcParamSpin_->setToolTip(tr("圆弧参数：角度(°)、弦长或半径；弦长/半径正数为劣弧，负数为优弧"));
    drawOptionPanel->addSmallWidget(arcParamSpin_);
    connect(arcParamSpin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double value) {
        DrawToolOptions options = view_->DrawOptions();
        options.numericValue = value;
        view_->SetDrawOptions(options);
    });
    UpdateArcParamSpin(view_->CurrentDrawMethod());

    polygonSidesSpin_ = new QSpinBox(this);
    polygonSidesSpin_->setRange(3, 64);
    polygonSidesSpin_->setValue(6);
    polygonSidesSpin_->setPrefix(tr("边数:"));
    drawOptionPanel->addSmallWidget(polygonSidesSpin_);
    connect(polygonSidesSpin_, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int value) {
        view_->SetPolygonSides(value);
    });

    RefreshDrawMethodCombo(ToolMode::Select);

    SARibbonPanel* movePanel = drawCategory->addPanel(tr("变换"));
    addToolAction(movePanel, tr("移动"), QKeySequence(Qt::Key_M), ToolMode::Move);

    // --- 修改 ---
    SARibbonCategory* modifyCategory = ribbon->addCategoryPage(tr("修改"));
    const auto addEditTool = [this](SARibbonPanel* panel,
                                    const QString& text,
                                    ToolMode mode,
                                    bool executeWhenReady = false) {
        QAction* action = new QAction(text, this);
        panel->addSmallAction(action);
        connect(action, &QAction::triggered, this, [this, mode, text, executeWhenReady]() {
            if (executeWhenReady) {
                bool ok = false;
                if (mode == ToolMode::BooleanUnion) {
                    ok = commandManager_.BooleanUnionSelected();
                    if (ok) {
                        statusBar()->showMessage(tr("并集完成"));
                        view_->SetToolMode(ToolMode::Select);
                        return;
                    }
                    statusBar()->showMessage(
                        tr("并集：请选中至少 2 个可闭合对象（圆、矩形、面域等），再点击「并集」"));
                    view_->SetToolMode(ToolMode::Select);
                    return;
                }
                if (mode == ToolMode::BooleanSubtract) {
                    ok = commandManager_.BooleanSubtractSelected();
                    if (ok) {
                        statusBar()->showMessage(tr("差集完成"));
                        view_->SetToolMode(ToolMode::Select);
                        return;
                    }
                    statusBar()->showMessage(
                        tr("差集：请先选被减对象，再选减去对象（至少 2 个可闭合对象），再点击「差集」"));
                    view_->SetToolMode(ToolMode::Select);
                    return;
                }
                if (mode == ToolMode::Hatch) {
                    ok = commandManager_.HatchSelected(10.0);
                    if (ok) {
                        statusBar()->showMessage(tr("填充线完成"));
                        view_->SetToolMode(ToolMode::Select);
                        return;
                    }
                    statusBar()->showMessage(
                        tr("填充线：请选中可闭合对象（圆、矩形、面域等），再点击「填充线」"));
                    view_->SetToolMode(ToolMode::Select);
                    return;
                }
                if (mode == ToolMode::DrawSolidFill || mode == ToolMode::DrawHatchFill) {
                    if (!document_.SelectedEntityIds().empty()) {
                        DrawToolResult fillResult = DrawToolEngine::ProcessInput(
                            mode,
                            view_->DrawOptions(),
                            {},
                            GeoPoint(),
                            &document_);
                        if (fillResult.entity) {
                            commandManager_.AddEntity(std::move(fillResult.entity));
                            statusBar()->showMessage(
                                mode == ToolMode::DrawSolidFill ? tr("实体填充完成") : tr("图案填充完成"));
                            view_->SetToolMode(ToolMode::Select);
                            return;
                        }
                    }
                    view_->SetToolMode(mode);
                    statusBar()->showMessage(
                        mode == ToolMode::DrawSolidFill
                            ? tr("实体填充：选中闭合对象后点击工具栏，或选中后点击图元")
                            : tr("图案填充：选中闭合对象后点击工具栏，或选中后点击图元"));
                    return;
                }
            }
            view_->SetToolMode(mode);
            if (mode == ToolMode::Mirror) {
                statusBar()->showMessage(tr("镜像：选择对象 → 指定镜像线 → Y/N 是否删除源对象"));
            } else if (mode != ToolMode::Fillet && mode != ToolMode::Chamfer) {
                statusBar()->showMessage(tr("当前命令：%1").arg(text));
            }
        });
    };

    SARibbonPanel* transformPanel = modifyCategory->addPanel(tr("变换"));
    addEditTool(transformPanel, tr("镜像"), ToolMode::Mirror);
    addEditTool(transformPanel, tr("旋转"), ToolMode::Rotate);
    addEditTool(transformPanel, tr("缩放"), ToolMode::Scale);
    addEditTool(transformPanel, tr("偏移"), ToolMode::Offset);
    QAction* arrayAction = new QAction(tr("阵列"), this);
    auto* arrayMenu = new QMenu(this);
    arrayMenu->addAction(tr("矩形阵列"), this, &MainWindow::StartRectangularArray);
    arrayMenu->addAction(tr("路径阵列"), this, &MainWindow::StartPathArray);
    arrayMenu->addAction(tr("环形阵列"), this, &MainWindow::StartPolarArray);
    arrayAction->setMenu(arrayMenu);
    transformPanel->addLargeAction(arrayAction, QToolButton::MenuButtonPopup);
    connect(arrayAction, &QAction::triggered, this, &MainWindow::StartRectangularArray);

    SARibbonPanel* editPanel = modifyCategory->addPanel(tr("编辑"));
    addEditTool(editPanel, tr("修剪"), ToolMode::Trim);
    addEditTool(editPanel, tr("延伸"), ToolMode::Extend);
    addEditTool(editPanel, tr("打断"), ToolMode::Break);
    addEditTool(editPanel, tr("圆角"), ToolMode::Fillet);
    addEditTool(editPanel, tr("倒角"), ToolMode::Chamfer);

    cornerDistanceSpin_ = new QDoubleSpinBox(this);
    cornerDistanceSpin_->setRange(0.0, 100000.0);
    cornerDistanceSpin_->setDecimals(4);
    cornerDistanceSpin_->setValue(10.0);
    cornerDistanceSpin_->setPrefix(tr("半径:"));
    cornerDistanceSpin_->setToolTip(tr("圆角半径 / 倒角第一距离"));
    editPanel->addSmallWidget(cornerDistanceSpin_);
    connect(cornerDistanceSpin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double value) {
        if (view_->CurrentToolMode() == ToolMode::Chamfer) {
            view_->SetChamferDistances(value, view_->ChamferDistance2());
        } else {
            view_->SetFilletRadius(value);
        }
    });
    chamferDistance2Spin_ = new QDoubleSpinBox(this);
    chamferDistance2Spin_->setRange(0.0, 100000.0);
    chamferDistance2Spin_->setDecimals(4);
    chamferDistance2Spin_->setValue(10.0);
    chamferDistance2Spin_->setPrefix(tr("D2:"));
    chamferDistance2Spin_->setToolTip(tr("倒角第二距离"));
    chamferDistance2Spin_->hide();
    editPanel->addSmallWidget(chamferDistance2Spin_);
    connect(chamferDistance2Spin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double value) {
        view_->SetChamferDistances(view_->ChamferDistance1(), value);
    });
    view_->SetCornerDistance(cornerDistanceSpin_->value());
    view_->SetChamferDistances(10.0, 10.0);

    SARibbonPanel* booleanPanel = modifyCategory->addPanel(tr("布尔"));
    addEditTool(booleanPanel, tr("并集"), ToolMode::BooleanUnion, true);
    addEditTool(booleanPanel, tr("差集"), ToolMode::BooleanSubtract, true);
    addEditTool(booleanPanel, tr("填充线"), ToolMode::Hatch, true);
    addEditTool(booleanPanel, tr("实体填充"), ToolMode::DrawSolidFill, true);
    addEditTool(booleanPanel, tr("图案填充"), ToolMode::DrawHatchFill, true);

    // --- 标注 ---
    SARibbonCategory* annotateCategory = ribbon->addCategoryPage(tr("标注"));
    const auto addAnnotateTool = [this](SARibbonPanel* panel,
                                        const QString& text,
                                        const QKeySequence& shortcut,
                                        ToolMode mode) {
        QAction* action = new QAction(text, this);
        panel->addSmallAction(action);
        if (!shortcut.isEmpty()) {
            action->setShortcut(shortcut);
            addAction(action);
        }
        connect(action, &QAction::triggered, this, [this, mode, text]() {
            view_->SetToolMode(mode);
            RefreshDrawMethodCombo(mode);
            statusBar()->showMessage(tr("当前命令：%1").arg(text));
        });
    };

    SARibbonPanel* textPanel = annotateCategory->addPanel(tr("文字"));
    addAnnotateTool(textPanel, tr("单行文字"), QKeySequence(Qt::Key_T), ToolMode::DrawText);
    addAnnotateTool(textPanel, tr("多行文字"), QKeySequence(), ToolMode::DrawMText);

    SARibbonPanel* dimPanel = annotateCategory->addPanel(tr("尺寸"));
    addAnnotateTool(dimPanel, tr("线性标注"), QKeySequence(), ToolMode::DrawDimLinear);
    addAnnotateTool(dimPanel, tr("对齐标注"), QKeySequence(), ToolMode::DrawDimAligned);
    addAnnotateTool(dimPanel, tr("半径标注"), QKeySequence(QStringLiteral("DRA")), ToolMode::DrawDimRadius);
    addAnnotateTool(dimPanel, tr("折弯半径"), QKeySequence(QStringLiteral("DJO")), ToolMode::DrawDimJoggedRadius);
    addAnnotateTool(dimPanel, tr("直径标注"), QKeySequence(QStringLiteral("DDI")), ToolMode::DrawDimDiameter);
    addAnnotateTool(dimPanel, tr("角度标注"), QKeySequence(QStringLiteral("DAN")), ToolMode::DrawDimAngular);
    addAnnotateTool(dimPanel, tr("坐标标注"), QKeySequence(QStringLiteral("DOR")), ToolMode::DrawDimOrdinate);

    SARibbonPanel* dimStylePanel = annotateCategory->addPanel(tr("样式"));
    QAction* dimStyleAction = new QAction(tr("标注样式..."), this);
    connect(dimStyleAction, &QAction::triggered, this, &MainWindow::ShowDimensionStyleDialog);
    dimStylePanel->addSmallAction(dimStyleAction);

    // --- 文件 ---
    SARibbonCategory* fileCategory = ribbon->addCategoryPage(tr("文件"));
    SARibbonPanel* dxfPanel = fileCategory->addPanel(tr("DXF"));
    QAction* importDxfAction = new QAction(tr("导入DXF"), this);
    dxfPanel->addSmallAction(importDxfAction);
    connect(importDxfAction, &QAction::triggered, this, [this]() {
        const QString fileName = QFileDialog::getOpenFileName(this, tr("导入 DXF"), QString(), tr("DXF 文件 (*.dxf)"));
        if (!fileName.isEmpty() && !DxfService::ImportFile(fileName.toStdString(), document_)) {
            QMessageBox::warning(this, tr("DXF"), tr("导入失败"));
        }
    });

    QAction* exportDxfAction = new QAction(tr("导出DXF"), this);
    dxfPanel->addSmallAction(exportDxfAction);
    connect(exportDxfAction, &QAction::triggered, this, [this]() {
        const QString fileName = QFileDialog::getSaveFileName(this, tr("导出 DXF"), QString(), tr("DXF 文件 (*.dxf)"));
        if (!fileName.isEmpty() && !DxfService::ExportFile(fileName.toStdString(), document_)) {
            QMessageBox::warning(this, tr("DXF"), tr("导出失败"));
        }
    });

    SARibbonPanel* printPanel = fileCategory->addPanel(tr("输出"));
    QAction* layoutAction = new QAction(tr("页面布局"), this);
    connect(layoutAction, &QAction::triggered, this, &MainWindow::ShowPageLayoutDialog);
    printPanel->addSmallAction(layoutAction);

    QAction* printAction = new QAction(tr("打印"), this);
    printPanel->addSmallAction(printAction);
    connect(printAction, &QAction::triggered, this, [this]() {
        PrintService::PrintView(*view_, document_.CurrentLayout());
    });

    // --- 工具 ---
    SARibbonCategory* toolsCategory = ribbon->addCategoryPage(tr("工具"));
    SARibbonPanel* settingsPanel = toolsCategory->addPanel(tr("系统"));
    QAction* optionsAction = new QAction(tr("选项"), this);
    settingsPanel->addSmallAction(optionsAction);
    optionsAction->setShortcut(QKeySequence(QStringLiteral("OP")));
    addAction(optionsAction);
    connect(optionsAction, &QAction::triggered, this, &MainWindow::ShowCadOptions);

    QAction* graphicsConfigToolsAction = new QAction(tr("图形性能..."), this);
    settingsPanel->addSmallAction(graphicsConfigToolsAction);
    connect(graphicsConfigToolsAction, &QAction::triggered, this, &MainWindow::ShowGraphicsConfig);

    QAction* snapSettingsAction = new QAction(tr("对象捕捉设置..."), this);
    connect(snapSettingsAction, &QAction::triggered, this, &MainWindow::ShowObjectSnapSettings);
    settingsPanel->addSmallAction(snapSettingsAction);

    QAction* dimStyleSettingsAction = new QAction(tr("标注样式..."), this);
    connect(dimStyleSettingsAction, &QAction::triggered, this, &MainWindow::ShowDimensionStyleDialog);
    settingsPanel->addSmallAction(dimStyleSettingsAction);
}

void MainWindow::StartRectangularArray()
{
    if (document_.EditableEntityIds(document_.SelectedEntityIds()).empty()) {
        statusBar()->showMessage(tr("矩形阵列：请先选择要阵列的对象"));
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(tr("矩形阵列"));
    auto* form = new QFormLayout(&dialog);
    auto* rows = new QSpinBox(&dialog);
    auto* columns = new QSpinBox(&dialog);
    auto* rowSpacing = new QDoubleSpinBox(&dialog);
    auto* columnSpacing = new QDoubleSpinBox(&dialog);
    auto* angle = new QDoubleSpinBox(&dialog);
    rows->setRange(1, 1000);
    columns->setRange(1, 1000);
    rows->setValue(rectangularArrayOptions_.rows);
    columns->setValue(rectangularArrayOptions_.columns);
    rowSpacing->setRange(-1000000.0, 1000000.0);
    columnSpacing->setRange(-1000000.0, 1000000.0);
    rowSpacing->setDecimals(4);
    columnSpacing->setDecimals(4);
    rowSpacing->setValue(rectangularArrayOptions_.rowSpacing);
    columnSpacing->setValue(rectangularArrayOptions_.columnSpacing);
    angle->setRange(-360.0, 360.0);
    angle->setDecimals(3);
    angle->setSuffix(QStringLiteral("°"));
    angle->setValue(rectangularArrayOptions_.angleDegrees);
    form->addRow(tr("行数:"), rows);
    form->addRow(tr("列数:"), columns);
    form->addRow(tr("行间距:"), rowSpacing);
    form->addRow(tr("列间距:"), columnSpacing);
    form->addRow(tr("阵列角度:"), angle);
    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    rectangularArrayOptions_.rows = rows->value();
    rectangularArrayOptions_.columns = columns->value();
    rectangularArrayOptions_.rowSpacing = rowSpacing->value();
    rectangularArrayOptions_.columnSpacing = columnSpacing->value();
    rectangularArrayOptions_.angleDegrees = angle->value();
    view_->CreateRectangularArray(rectangularArrayOptions_);
}

void MainWindow::StartPathArray()
{
    if (document_.EditableEntityIds(document_.SelectedEntityIds()).empty()) {
        statusBar()->showMessage(tr("路径阵列：请先选择要阵列的对象"));
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(tr("路径阵列"));
    auto* form = new QFormLayout(&dialog);
    auto* method = new QComboBox(&dialog);
    method->addItem(tr("定数（沿全路径均匀分布）"), static_cast<int>(PathArrayMethod::Divide));
    method->addItem(tr("定距（按固定间距分布）"), static_cast<int>(PathArrayMethod::Measure));
    method->setCurrentIndex(pathArrayOptions_.method == PathArrayMethod::Measure ? 1 : 0);
    auto* count = new QSpinBox(&dialog);
    count->setRange(1, 10000);
    count->setValue(pathArrayOptions_.itemCount);
    auto* spacing = new QDoubleSpinBox(&dialog);
    spacing->setRange(0.0001, 1000000.0);
    spacing->setDecimals(4);
    spacing->setValue(std::max(0.0001, pathArrayOptions_.spacing));
    auto* align = new QCheckBox(tr("沿路径方向对齐项目"), &dialog);
    align->setChecked(pathArrayOptions_.alignItems);
    form->addRow(tr("分布方法:"), method);
    form->addRow(tr("项目总数:"), count);
    form->addRow(tr("项目间距:"), spacing);
    form->addRow(QString(), align);
    const auto updateFields = [=]() {
        const bool measure = method->currentData().toInt()
            == static_cast<int>(PathArrayMethod::Measure);
        count->setEnabled(!measure);
        spacing->setEnabled(measure);
    };
    connect(method, &QComboBox::currentIndexChanged, &dialog, updateFields);
    updateFields();
    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    pathArrayOptions_.method = static_cast<PathArrayMethod>(method->currentData().toInt());
    pathArrayOptions_.itemCount = count->value();
    pathArrayOptions_.spacing = spacing->value();
    pathArrayOptions_.alignItems = align->isChecked();
    if (view_->BeginPathArray(pathArrayOptions_)) {
        view_->setFocus(Qt::OtherFocusReason);
    }
}

void MainWindow::StartPolarArray()
{
    if (document_.EditableEntityIds(document_.SelectedEntityIds()).empty()) {
        statusBar()->showMessage(tr("环形阵列：请先选择要阵列的对象"));
        return;
    }

    if (view_->BeginPolarArray(polarArrayOptions_)) {
        view_->setFocus(Qt::OtherFocusReason);
    }
}

void MainWindow::RefreshDrawMethodCombo(ToolMode mode)
{
    if (!drawMethodCombo_) {
        return;
    }
    drawMethodCombo_->blockSignals(true);
    drawMethodCombo_->clear();
    const std::vector<DrawMethod> methods = DrawToolEngine::AvailableMethods(mode);
    for (DrawMethod method : methods) {
        drawMethodCombo_->addItem(QString::fromUtf8(DrawToolEngine::MethodLabel(method).c_str()), static_cast<int>(method));
    }
    drawMethodCombo_->setEnabled(!methods.empty());
    if (!methods.empty()) {
        drawMethodCombo_->setCurrentIndex(0);
        view_->SetDrawMethod(methods.front());
    }
    drawMethodCombo_->blockSignals(false);

    if (polygonSidesSpin_) {
        polygonSidesSpin_->setVisible(mode == ToolMode::DrawPolygon);
    }
    if (arcParamSpin_) {
        const bool showArcParam = mode == ToolMode::DrawArc;
        arcParamSpin_->setVisible(showArcParam);
        if (showArcParam && !methods.empty()) {
            UpdateArcParamSpin(methods.front());
        }
    }
    if (drawMethodCombo_) {
        drawMethodCombo_->setVisible(!methods.empty());
    }
    if (cornerDistanceSpin_) {
        const QSignalBlocker block(cornerDistanceSpin_);
        if (mode == ToolMode::Chamfer) {
            cornerDistanceSpin_->setPrefix(tr("D1:"));
            cornerDistanceSpin_->setToolTip(tr("倒角第一距离（按选择顺序）"));
            cornerDistanceSpin_->setValue(view_->ChamferDistance1());
        } else {
            cornerDistanceSpin_->setPrefix(tr("半径:"));
            cornerDistanceSpin_->setToolTip(tr("圆角半径。0 = 锐角；Shift+选择第二对象也可得到锐角"));
            cornerDistanceSpin_->setValue(view_->FilletRadius());
        }
    }
    if (chamferDistance2Spin_) {
        chamferDistance2Spin_->setVisible(mode == ToolMode::Chamfer);
        if (mode == ToolMode::Chamfer) {
            const QSignalBlocker block(chamferDistance2Spin_);
            chamferDistance2Spin_->setValue(view_->ChamferDistance2());
        }
    }
}

void MainWindow::UpdateArcParamSpin(DrawMethod method)
{
    if (!arcParamSpin_) {
        return;
    }
    const bool usesNumeric = DrawToolEngine::ArcMethodUsesNumericParameter(method);
    arcParamSpin_->setEnabled(usesNumeric);
    if (!usesNumeric) {
        return;
    }

    arcParamSpin_->blockSignals(true);
    switch (method) {
    case DrawMethod::ArcStartCenterAngle:
    case DrawMethod::ArcCenterStartAngle:
    case DrawMethod::ArcStartEndAngle:
        arcParamSpin_->setPrefix(tr("角度:"));
        arcParamSpin_->setValue(90.0);
        break;
    case DrawMethod::ArcStartCenterChord:
    case DrawMethod::ArcCenterStartChord:
        arcParamSpin_->setPrefix(tr("长度:"));
        arcParamSpin_->setValue(100.0);
        arcParamSpin_->setToolTip(tr("弦长：正数绘制劣弧，负数绘制优弧；第三点可用鼠标指定长度，Ctrl 切换为同一端点的优弧"));
        break;
    case DrawMethod::ArcStartEndRadius:
        arcParamSpin_->setPrefix(tr("半径:"));
        arcParamSpin_->setValue(100.0);
        arcParamSpin_->setToolTip(tr("圆弧半径：正数绘制劣弧，负数绘制优弧；第三点可用鼠标指定半径距离，Ctrl 切换优弧/劣弧"));
        break;
    default:
        break;
    }
    arcParamSpin_->blockSignals(false);

    DrawToolOptions options = view_->DrawOptions();
    options.numericValue = arcParamSpin_->value();
    view_->SetDrawOptions(options);
}

void MainWindow::ShowLayerManager()
{
    if (!layerManagerDialog_) {
        layerManagerDialog_ = new LayerManagerDialog(document_, this);
        layerManagerDialog_->setAttribute(Qt::WA_DeleteOnClose);
        connect(layerManagerDialog_, &QObject::destroyed, this, [this]() {
            layerManagerDialog_ = nullptr;
        });
    }
    layerManagerDialog_->show();
    layerManagerDialog_->raise();
    layerManagerDialog_->activateWindow();
}

void MainWindow::ShowLineTypeManager()
{
    LineTypeManagerDialog dialog(document_, this);
    dialog.exec();
}

void MainWindow::ShowPropertiesPalette()
{
    PropertiesPaletteDialog dialog(document_, this);
    dialog.exec();
}

void MainWindow::ShowPageLayoutDialog()
{
    PageLayout& layout = document_.CurrentLayout();

    QDialog dialog(this);
    dialog.setWindowTitle(tr("页面布局设置"));

    auto* layoutCombo = new QComboBox(&dialog);
    int currentIndex = 0;
    for (std::size_t i = 0; i < document_.Layouts().size(); ++i) {
        layoutCombo->addItem(QString::fromUtf8(document_.Layouts()[i].name.c_str()));
        if (&document_.Layouts()[i] == &layout) {
            currentIndex = static_cast<int>(i);
        }
    }
    layoutCombo->setCurrentIndex(currentIndex);

    auto* widthSpin = new QDoubleSpinBox(&dialog);
    widthSpin->setRange(1.0, 10000.0);
    widthSpin->setSuffix(tr(" mm"));
    widthSpin->setValue(layout.widthMm);

    auto* heightSpin = new QDoubleSpinBox(&dialog);
    heightSpin->setRange(1.0, 10000.0);
    heightSpin->setSuffix(tr(" mm"));
    heightSpin->setValue(layout.heightMm);

    auto* marginSpin = new QDoubleSpinBox(&dialog);
    marginSpin->setRange(0.0, 1000.0);
    marginSpin->setSuffix(tr(" mm"));
    marginSpin->setValue(layout.marginMm);

    auto* scaleSpin = new QDoubleSpinBox(&dialog);
    scaleSpin->setRange(0.001, 1000.0);
    scaleSpin->setDecimals(3);
    scaleSpin->setValue(layout.scale);

    auto* landscapeCheck = new QCheckBox(tr("横向"), &dialog);
    landscapeCheck->setChecked(layout.landscape);

    auto* form = new QFormLayout();
    form->addRow(tr("布局"), layoutCombo);
    form->addRow(tr("宽度"), widthSpin);
    form->addRow(tr("高度"), heightSpin);
    form->addRow(tr("页边距"), marginSpin);
    form->addRow(tr("比例"), scaleSpin);
    form->addRow(QString(), landscapeCheck);

    auto syncFields = [&](int index) {
        if (index < 0 || index >= static_cast<int>(document_.Layouts().size())) {
            return;
        }
        const PageLayout& selected = document_.Layouts()[static_cast<std::size_t>(index)];
        widthSpin->setValue(selected.widthMm);
        heightSpin->setValue(selected.heightMm);
        marginSpin->setValue(selected.marginMm);
        scaleSpin->setValue(selected.scale);
        landscapeCheck->setChecked(selected.landscape);
    };
    connect(layoutCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), &dialog, syncFields);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    auto* mainLayout = new QVBoxLayout(&dialog);
    mainLayout->addLayout(form);
    mainLayout->addWidget(buttons);

    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    const int chosen = layoutCombo->currentIndex();
    if (chosen >= 0 && chosen < static_cast<int>(document_.Layouts().size())) {
        document_.SetCurrentLayout(static_cast<std::size_t>(chosen));
    }

    PageLayout& target = document_.CurrentLayout();
    target.widthMm = widthSpin->value();
    target.heightMm = heightSpin->value();
    target.marginMm = marginSpin->value();
    target.scale = scaleSpin->value();
    target.landscape = landscapeCheck->isChecked();

    statusBar()->showMessage(tr("已更新页面布局：%1").arg(QString::fromUtf8(target.name.c_str())));
}

void MainWindow::ShowObjectSnapSettings()
{
    ObjectSnapSettingsDialog dialog(view_->SnapModes(), this);
    if (dialog.exec() == QDialog::Accepted) {
        view_->SetObjectSnapModes(dialog.Modes());
        statusBar()->showMessage(tr("对象捕捉模式已更新"));
    }
}

void MainWindow::ShowCadOptions()
{
    CadOptionsDialog dialog(view_->GetCursorSettings(), this);
    if (dialog.exec() == QDialog::Accepted) {
        view_->SetCursorSettings(dialog.Settings());
        statusBar()->showMessage(tr("选项已更新"));
    }
}

void MainWindow::ShowGraphicsConfig()
{
    GraphicsConfigDialog dialog(view_->GetGraphicsConfig(), this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    view_->SetGraphicsConfig(dialog.Config());
    const GraphicsConfig& cfg = view_->GetGraphicsConfig();
    if (cfg.hardwareAcceleration && view_->IsHardwareAccelerationActive()) {
        statusBar()->showMessage(
            tr("硬件加速已开启（MSAA %1x，HQGEOM %2）")
                .arg(cfg.msaaSamples)
                .arg(cfg.highQualityGeometry ? tr("开") : tr("关")),
            5000);
    } else if (cfg.hardwareAcceleration && !view_->IsHardwareAccelerationActive()) {
        statusBar()->showMessage(
            tr("已请求硬件加速，但当前未能创建 OpenGL 视口（已回退软件渲染）"),
            6000);
    } else {
        statusBar()->showMessage(tr("硬件加速已关闭（CPU 软件渲染）"), 4000);
    }
}

void MainWindow::ShowViewResDialog()
{
    bool ok = false;
    const int current = view_->ViewRes();
    const int value = QInputDialog::getInt(
        this,
        tr("VIEWRES 视图分辨率"),
        tr("输入圆的缩放百分比 (1–20000)。\n"
           "数值越大，圆/圆弧/椭圆在屏幕上的矢量拟合越平滑；\n"
           "不影响几何精度与打印输出。\n"
           "当前值：%1")
            .arg(current),
        current,
        ViewDisplaySettings::kMinViewRes,
        ViewDisplaySettings::kMaxViewRes,
        1,
        &ok);
    if (!ok) {
        return;
    }

    view_->SetViewRes(value);
    statusBar()->showMessage(
        tr("VIEWRES = %1（已重生成曲线显示）").arg(view_->ViewRes()),
        4000);
}

void MainWindow::ShowDimensionStyleDialog()
{
    DimensionStyleDialog dialog(document_, this);
    if (dialog.exec() == QDialog::Accepted) {
        document_.NotifyChanged();
        statusBar()->showMessage(
            tr("标注样式已更新，当前样式：%1")
                .arg(QString::fromUtf8(document_.CurrentDimensionStyleName().c_str())));
    }
}

void MainWindow::OnCreateBlock()
{
    const std::vector<EntityId> selected = document_.SelectedEntityIds();
    if (selected.empty()) {
        QMessageBox::information(this, tr("创建块"), tr("请先选择要写入块定义的图元。"));
        return;
    }

    double minX = 0.0;
    double minY = 0.0;
    bool hasBounds = false;
    for (EntityId id : selected) {
        const Entity* entity = document_.FindEntity(id);
        if (!entity) {
            continue;
        }
        const GeoBounds bounds = entity->Shaper().Bounds();
        if (!bounds.valid) {
            continue;
        }
        if (!hasBounds) {
            minX = bounds.minX;
            minY = bounds.minY;
            hasBounds = true;
        } else {
            minX = std::min(minX, bounds.minX);
            minY = std::min(minY, bounds.minY);
        }
    }

    BlockDefinitionDialog dialog(this);
    dialog.SetSuggestedName(QStringLiteral("块1"));
    if (hasBounds) {
        dialog.SetBasePoint(minX, minY);
    }
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    const BlockDefinitionDialogResult result = dialog.Result();
    if (document_.HasBlockDefinition(result.name)) {
        bool includesSameBlockRef = false;
        for (EntityId id : selected) {
            const Entity* entity = document_.FindEntity(id);
            if (entity && entity->Type() == EntityType::BlockReference
                && static_cast<const BlockReferenceEntity&>(*entity).BlockName() == result.name) {
                includesSameBlockRef = true;
                break;
            }
        }
        const QString detail = includesSameBlockRef
            ? tr("将把选中的原块几何与新图元一并写入新定义，所有参照将更新。")
            : tr("选择集中未包含该块的参照：重定义将用当前选择完全替换原定义（与 AutoCAD 相同）。\n"
                 "若要在原块上增加图形，请同时选中原块参照和新图元，或使用块编辑器/在位编辑。");
        const auto answer = QMessageBox::question(
            this,
            tr("重定义块"),
            tr("块「%1」已存在。%2\n\n是否重定义？")
                .arg(QString::fromStdString(result.name))
                .arg(detail));
        if (answer != QMessageBox::Yes) {
            return;
        }
    }

    if (!commandManager_.CreateBlock(
            result.name,
            result.baseX,
            result.baseY,
            selected,
            result.mode,
            result.allowExplode,
            result.uniformScale,
            result.annotative,
            result.attributeDefs)) {
        QMessageBox::warning(this, tr("创建块"), tr("创建块失败。"));
        return;
    }
    statusBar()->showMessage(tr("已创建块「%1」").arg(QString::fromStdString(result.name)), 4000);
}

void MainWindow::OnInsertBlock()
{
    if (document_.BlockDefinitions().empty()) {
        QMessageBox::information(this, tr("插入块"), tr("当前图纸没有块定义，请先创建块。"));
        return;
    }

    BlockInsertDialog dialog(document_, this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    const BlockInsertDialogResult result = dialog.Result();
    CadView::PendingBlockInsert pending;
    pending.blockName = result.blockName;
    pending.scale = result.scale;
    pending.rotationRadians = result.rotationDegrees * 3.14159265358979323846 / 180.0;
    pending.explodeOnInsert = result.explodeOnInsert;
    pending.attributes = result.attributes;
    view_->BeginInsertBlock(std::move(pending));
}

void MainWindow::OnEditBlock()
{
    if (refEditSession_ && refEditSession_->IsActive()) {
        QMessageBox::information(this, tr("块编辑器"), tr("请先结束当前 REFEDIT 在位编辑。"));
        return;
    }

    std::string blockName;
    for (EntityId id : document_.SelectedEntityIds()) {
        const Entity* entity = document_.FindEntity(id);
        if (entity && entity->Type() == EntityType::BlockReference) {
            blockName = static_cast<const BlockReferenceEntity&>(*entity).BlockName();
            break;
        }
    }
    if (blockName.empty() && !document_.BlockDefinitions().empty()) {
        blockName = document_.BlockDefinitions().front().name;
    }
    if (blockName.empty()) {
        QMessageBox::information(this, tr("块编辑器"), tr("请选择一个块参照，或先创建块定义。"));
        return;
    }

    BlockEditorDialog dialog(document_, blockName, this);
    dialog.exec();
    if (dialog.definitionChanged()) {
        statusBar()->showMessage(tr("块定义「%1」已更新").arg(QString::fromStdString(blockName)), 4000);
    }
}

void MainWindow::OnRefEdit()
{
    if (!refEditSession_) {
        return;
    }
    if (refEditSession_->IsActive()) {
        QMessageBox::information(this, tr("REFEDIT"), tr("已在在位编辑中。请使用 REFCLOSE / REFSET。"));
        UpdateRefEditRibbon(true);
        return;
    }

    EntityId targetId = 0;
    for (EntityId id : document_.SelectedEntityIds()) {
        const Entity* entity = document_.FindEntity(id);
        if (entity && entity->Type() == EntityType::BlockReference) {
            targetId = id;
            break;
        }
    }
    if (targetId == 0) {
        QMessageBox::information(this, tr("REFEDIT"), tr("请先选择一个块参照。"));
        return;
    }

    if (!refEditSession_->Begin(targetId)) {
        QMessageBox::warning(this, tr("REFEDIT"), tr("无法进入在位编辑（块定义可能为空）。"));
        return;
    }
    UpdateRefEditRibbon(true);
    view_->SetToolMode(ToolMode::Select);
    statusBar()->showMessage(
        tr("REFEDIT：可编辑高亮图元；灰色为锁定。REFCLOSE 保存/放弃，REFSET 添加/移除。"),
        8000);
}

void MainWindow::OnRefEditClose(bool save)
{
    if (!refEditSession_ || !refEditSession_->IsActive()) {
        return;
    }
    if (!refEditSession_->Close(save)) {
        if (save) {
            QMessageBox::warning(this, tr("REFCLOSE"), tr("保存失败：工作集不能为空。"));
            return;
        }
    }
    UpdateRefEditRibbon(false);
    statusBar()->showMessage(save ? tr("已保存并退出 REFEDIT") : tr("已放弃并退出 REFEDIT"), 4000);
}

void MainWindow::OnRefEditSetAdd()
{
    if (!refEditSession_ || !refEditSession_->IsActive()) {
        return;
    }
    const int n = refEditSession_->AddSelectionToWorkingSet(document_.SelectedEntityIds());
    statusBar()->showMessage(tr("REFSET 添加：%1 个对象").arg(n), 3000);
}

void MainWindow::OnRefEditSetRemove()
{
    if (!refEditSession_ || !refEditSession_->IsActive()) {
        return;
    }
    const int n = refEditSession_->RemoveSelectionFromWorkingSet(document_.SelectedEntityIds());
    statusBar()->showMessage(tr("REFSET 移除：%1 个对象").arg(n), 3000);
}

void MainWindow::UpdateRefEditRibbon(bool active)
{
    if (!refEditContext_) {
        return;
    }
    SARibbonBar* ribbon = ribbonBar();
    if (!ribbon) {
        return;
    }
    if (active) {
        ribbon->showContextCategory(refEditContext_);
    } else {
        ribbon->hideContextCategory(refEditContext_);
    }
}

void MainWindow::ShowDesignCenter()
{
    if (!designCenterDialog_) {
        designCenterDialog_ = new DesignCenterDialog(document_, commandManager_, view_, this);
        connect(designCenterDialog_, &QObject::destroyed, this, [this]() {
            designCenterDialog_ = nullptr;
        });
    }
    designCenterDialog_->show();
    designCenterDialog_->raise();
    designCenterDialog_->activateWindow();
}

void MainWindow::ShowBlocksPalette()
{
    if (!blocksPaletteDialog_) {
        blocksPaletteDialog_ = new BlocksPaletteDialog(document_, commandManager_, view_, this);
        blocksPaletteDialog_->SetCreateBlockCallback([this]() { OnCreateBlock(); });
        blocksPaletteDialog_->SetWblockCallback([this]() { OnWblock(); });
        connect(blocksPaletteDialog_, &QObject::destroyed, this, [this]() {
            blocksPaletteDialog_ = nullptr;
        });
    }
    blocksPaletteDialog_->Refresh();
    blocksPaletteDialog_->show();
    blocksPaletteDialog_->raise();
    blocksPaletteDialog_->activateWindow();
}

void MainWindow::OnEditBlockAttributes()
{
    EntityId targetId = 0;
    for (EntityId id : document_.SelectedEntityIds()) {
        const Entity* entity = document_.FindEntity(id);
        if (entity && entity->Type() == EntityType::BlockReference) {
            targetId = id;
            break;
        }
    }
    if (targetId == 0) {
        QMessageBox::information(this, tr("编辑属性"), tr("请先选择一个块参照。"));
        return;
    }

    auto* ref = static_cast<BlockReferenceEntity*>(document_.FindEntity(targetId));
    if (!ref) {
        return;
    }
    const BlockDefinition* def = document_.FindBlockDefinition(ref->BlockName());
    BlockAttributeEditDialog dialog(*ref, def, this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    ref->SetAttributeValues(dialog.Result());
    document_.NotifyEntitiesUpdated({targetId});
    statusBar()->showMessage(tr("块属性已更新"), 3000);
}

void MainWindow::OnDetectBlocks()
{
    struct GroupKey {
        EntityType type = EntityType::Line;
        int widthBucket = 0;
        int heightBucket = 0;
        bool operator<(const GroupKey& other) const
        {
            if (type != other.type) {
                return static_cast<int>(type) < static_cast<int>(other.type);
            }
            if (widthBucket != other.widthBucket) {
                return widthBucket < other.widthBucket;
            }
            return heightBucket < other.heightBucket;
        }
    };

    std::map<GroupKey, std::vector<EntityId>> groups;
    for (const auto& entity : document_.Entities()) {
        if (!entity || entity->Type() == EntityType::BlockReference || entity->Type() == EntityType::Group) {
            continue;
        }
        const GeoBounds bounds = entity->Shaper().Bounds();
        if (!bounds.valid) {
            continue;
        }
        const double w = bounds.maxX - bounds.minX;
        const double h = bounds.maxY - bounds.minY;
        GroupKey key;
        key.type = entity->Type();
        key.widthBucket = static_cast<int>(std::lround(w * 10.0));
        key.heightBucket = static_cast<int>(std::lround(h * 10.0));
        groups[key].push_back(entity->Id());
    }

    QStringList lines;
    std::vector<std::vector<EntityId>> candidates;
    for (const auto& entry : groups) {
        if (entry.second.size() < 2) {
            continue;
        }
        candidates.push_back(entry.second);
        lines << tr("类型 %1，近似尺寸组：%2 个")
                     .arg(static_cast<int>(entry.first.type))
                     .arg(static_cast<int>(entry.second.size()));
    }

    if (candidates.empty()) {
        QMessageBox::information(this, tr("块检测"), tr("未发现明显重复图形组。"));
        return;
    }

    const QString message = tr("检测到以下可做成块的重复组：\n\n%1\n\n是否用第一组创建块？")
                                .arg(lines.join(QLatin1Char('\n')));
    if (QMessageBox::question(this, tr("块检测"), message) != QMessageBox::Yes) {
        return;
    }

    document_.ClearSelection();
    for (EntityId id : candidates.front()) {
        document_.ToggleSelection(id);
    }
    OnCreateBlock();
}

void MainWindow::OnExplodeBlocks()
{
    const int count = commandManager_.ExplodeSelectedBlocks();
    if (count == 0) {
        QMessageBox::information(this, tr("分解"), tr("未分解任何块（请选择允许分解的块参照）。"));
        return;
    }
    statusBar()->showMessage(tr("已分解 %1 个块").arg(count), 3000);
}

void MainWindow::OnPurgeBlocks()
{
    const int removed = document_.PurgeUnusedBlocks();
    statusBar()->showMessage(tr("已清理 %1 个未使用块定义").arg(removed), 3000);
}

void MainWindow::OnDemoDynamicBlock()
{
    std::string selfTestReport;
    const bool selfOk = DynamicBlockEvaluator::RunSelfTest(selfTestReport);

    constexpr const char* kName = "动态块演示";
    if (document_.HasBlockDefinition(kName)) {
        // Remove existing inserts of the demo block for a clean test.
        std::vector<EntityId> toRemove;
        for (const auto& e : document_.Entities()) {
            if (e && e->Type() == EntityType::BlockReference
                && static_cast<const BlockReferenceEntity&>(*e).BlockName() == kName) {
                toRemove.push_back(e->Id());
            }
        }
        if (!toRemove.empty()) {
            document_.TakeEntities(toRemove);
        }
        document_.RemoveBlockDefinition(kName);
    }

    BlockDefinition def;
    def.name = kName;
    def.baseX = 0.0;
    def.baseY = 0.0;
    // Left body (fixed) + right edge (stretchable) — like a simple door/window width.
    def.entities.push_back(std::make_shared<RectangleEntity>(
        IdGenerator::Next(), GeoRectangle(GeoPoint(0.0, 0.0), GeoPoint(60.0, 40.0))));
    def.entities.push_back(std::make_shared<LineEntity>(
        IdGenerator::Next(), GeoLine(GeoPoint(120.0, 0.0), GeoPoint(120.0, 40.0))));
    def.entities.push_back(std::make_shared<LineEntity>(
        IdGenerator::Next(), GeoLine(GeoPoint(60.0, 0.0), GeoPoint(120.0, 0.0))));
    def.entities.push_back(std::make_shared<LineEntity>(
        IdGenerator::Next(), GeoLine(GeoPoint(60.0, 40.0), GeoPoint(120.0, 40.0))));

    DynParameter linear;
    linear.id = DynamicBlockEvaluator::MakeParameterId(DynParamType::Linear, 1);
    linear.name = "宽度";
    linear.type = DynParamType::Linear;
    linear.basePoint = GeoPoint(0.0, 20.0);
    linear.endPoint = GeoPoint(120.0, 20.0);
    linear.defaultValue = 120.0;
    linear.minValue = 40.0;
    linear.maxValue = 400.0;
    def.dynamic.parameters.push_back(linear);

    DynAction stretch;
    stretch.id = DynamicBlockEvaluator::MakeActionId(DynActionType::Stretch, 1);
    stretch.name = "拉伸右侧";
    stretch.type = DynActionType::Stretch;
    stretch.parameterId = linear.id;
    stretch.entityIndices = {1, 2, 3}; // right line + top/bottom connectors
    stretch.hasStretchFrame = true;
    stretch.stretchMin = GeoPoint(50.0, -5.0);
    stretch.stretchMax = GeoPoint(140.0, 50.0);
    def.dynamic.actions.push_back(stretch);

    document_.AddBlockDefinition(def);

    const EntityId insertId = commandManager_.InsertBlock(kName, GeoPoint(0.0, 0.0), 1.0, 0.0, false, {});
    if (insertId != 0) {
        document_.ClearSelection();
        document_.SelectOnly(insertId);
    }

    // Programmatic grip stretch verification on the inserted instance.
    QString gripResult = tr("夹点实机未测（无插入实例）");
    if (Entity* ent = document_.FindEntity(insertId)) {
        auto& block = static_cast<BlockReferenceEntity&>(*ent);
        const BlockDefinition* live = document_.FindBlockDefinition(kName);
        if (live) {
            const auto grips = block.DynamicGrips(live);
            if (!grips.empty()) {
                const GeoPoint stretchedWorld = block.InsertionPoint().Translated(180.0, 20.0);
                const bool ok = block.ApplyDynamicGrip(live, grips.front().index, stretchedWorld);
                document_.NotifyEntitiesUpdated({insertId});
                const double w = block.GetDynamicValue(linear.id, -1.0);
                gripResult = ok
                    ? tr("夹点拉伸成功，当前宽度≈%1").arg(w, 0, 'f', 1)
                    : tr("夹点拉伸失败");
            }
        }
    }

    if (view_) {
        view_->ZoomExtents();
    }

    QMessageBox::information(
        this,
        tr("动态块演示"),
        tr("自检结果：%1\n\n%2\n\n%3\n\n"
           "用法：\n"
           "1. 图上已插入并选中「%4」\n"
           "2. 拖动青色动态夹点改变宽度\n"
           "3. 也可：块 → 编辑 ▼ → 块编辑器 BEDIT 查看参数/动作")
            .arg(selfOk ? tr("通过") : tr("失败"))
            .arg(QString::fromStdString(selfTestReport))
            .arg(gripResult)
            .arg(QString::fromUtf8(kName)));

    statusBar()->showMessage(
        selfOk ? tr("动态块演示已创建，请拖动青色夹点测试") : tr("动态块自检失败，请查看对话框详情"),
        8000);
}

void MainWindow::OnWblock()
{
    std::string blockName;
    if (blocksPaletteDialog_) {
        blockName = blocksPaletteDialog_->SelectedBlockName();
    }
    for (EntityId id : document_.SelectedEntityIds()) {
        if (!blockName.empty()) {
            break;
        }
        const Entity* entity = document_.FindEntity(id);
        if (entity && entity->Type() == EntityType::BlockReference) {
            blockName = static_cast<const BlockReferenceEntity&>(*entity).BlockName();
            break;
        }
    }
    if (blockName.empty() && document_.BlockDefinitions().size() == 1) {
        blockName = document_.BlockDefinitions().front().name;
    }
    if (blockName.empty() && !document_.BlockDefinitions().empty()) {
        QStringList names;
        for (const BlockDefinition& block : document_.BlockDefinitions()) {
            names << QString::fromStdString(block.name);
        }
        bool ok = false;
        const QString chosen = QInputDialog::getItem(
            this, tr("写块"), tr("选择要导出的块："), names, 0, false, &ok);
        if (!ok || chosen.isEmpty()) {
            return;
        }
        blockName = chosen.toStdString();
    }

    CadDocument exportDoc;
    if (!blockName.empty()) {
        const BlockDefinition* def = document_.FindBlockDefinition(blockName);
        if (!def) {
            QMessageBox::warning(this, tr("写块"), tr("找不到块定义。"));
            return;
        }
        BlockDefinition exported;
        exported.name = def->name;
        exported.baseX = def->baseX;
        exported.baseY = def->baseY;
        exported.allowExplode = def->allowExplode;
        exported.uniformScale = def->uniformScale;
        exported.annotative = def->annotative;
        exported.attributeDefs = def->attributeDefs;
        for (const std::shared_ptr<Entity>& source : def->entities) {
            if (!source) {
                continue;
            }
            std::unique_ptr<Entity> clone = source->Clone();
            if (!clone) {
                continue;
            }
            clone->SetId(IdGenerator::Next());
            // Keep geometry relative to block base inside the definition.
            exported.entities.push_back(std::shared_ptr<Entity>(std::move(clone)));
        }
        exportDoc.AddBlockDefinition(exported);

        if (exported.entities.empty()) {
            QMessageBox::warning(this, tr("写块"), tr("块「%1」内没有可导出的几何。").arg(QString::fromStdString(exported.name)));
            return;
        }

        // Model-space INSERT so the DXF also opens as a visible drawing.
        auto insert = std::make_unique<BlockReferenceEntity>(
            IdGenerator::Next(),
            exported.name,
            GeoPoint(exported.baseX, exported.baseY),
            1.0,
            0.0);
        insert->RebuildCache(exportDoc.FindBlockDefinition(exported.name));
        exportDoc.AddEntity(std::move(insert));
    } else {
        const std::vector<EntityId> selected = document_.SelectedEntityIds();
        if (selected.empty()) {
            QMessageBox::information(this, tr("写块"), tr("请选择块参照、选择图元，或确保图纸中存在块定义。"));
            return;
        }

        bool ok = false;
        const QString chosen = QInputDialog::getText(
            this, tr("写块"), tr("块名称："), QLineEdit::Normal, tr("WBLOCK"), &ok);
        if (!ok || chosen.trimmed().isEmpty()) {
            return;
        }
        blockName = chosen.trimmed().toStdString();

        BlockDefinition exported;
        exported.name = blockName;
        for (EntityId id : selected) {
            const Entity* entity = document_.FindEntity(id);
            if (!entity) {
                continue;
            }
            std::unique_ptr<Entity> forDef = entity->Clone();
            if (!forDef) {
                continue;
            }
            forDef->SetId(IdGenerator::Next());
            exported.entities.push_back(std::shared_ptr<Entity>(std::move(forDef)));
        }
        exportDoc.AddBlockDefinition(exported);
        auto insert = std::make_unique<BlockReferenceEntity>(
            IdGenerator::Next(),
            exported.name,
            GeoPoint(0.0, 0.0),
            1.0,
            0.0);
        insert->RebuildCache(exportDoc.FindBlockDefinition(exported.name));
        exportDoc.AddEntity(std::move(insert));
    }

    const QString fileName = QFileDialog::getSaveFileName(
        this, tr("写块 (WBLOCK)"), QString(), tr("DXF 文件 (*.dxf)"));
    if (fileName.isEmpty()) {
        return;
    }
    if (!DxfService::ExportFile(fileName.toStdString(), exportDoc)) {
        QMessageBox::warning(this, tr("写块"), tr("导出失败。"));
        return;
    }
    statusBar()->showMessage(tr("已导出写块：%1").arg(fileName), 4000);
}

MainWindow::~MainWindow()
{
    qApp->removeEventFilter(this);
    if (refEditSession_ && refEditSession_->IsActive()) {
        refEditSession_->Cancel();
    }

    if (QWidget* central = takeCentralWidget()) {
        delete central;
        view_ = nullptr;
    }

    delete scene_;
    scene_ = nullptr;
}

} // namespace cad
