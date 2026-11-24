#include "View/BlockEditorDialog.h"

#include "Base/IdGenerator.h"
#include "DataModel/BlockReferenceEntity.h"
#include "DataModel/DynamicBlockEvaluator.h"
#include "DataModel/IDocumentObserver.h"
#include "Geometry/GeoPoint.h"

#include <QCloseEvent>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

namespace cad {

namespace {

GeoPoint BoundsCenter(const CadDocument& doc)
{
    bool has = false;
    double minX = 0, minY = 0, maxX = 0, maxY = 0;
    for (const auto& e : doc.Entities()) {
        if (!e) continue;
        const GeoBounds b = e->Shaper().Bounds();
        if (!b.valid) continue;
        if (!has) {
            minX = b.minX; minY = b.minY; maxX = b.maxX; maxY = b.maxY;
            has = true;
        } else {
            minX = std::min(minX, b.minX);
            minY = std::min(minY, b.minY);
            maxX = std::max(maxX, b.maxX);
            maxY = std::max(maxY, b.maxY);
        }
    }
    if (!has) {
        return GeoPoint(0.0, 0.0);
    }
    return GeoPoint(0.5 * (minX + maxX), 0.5 * (minY + maxY));
}

GeoBounds SelectionBounds(const CadDocument& doc)
{
    GeoBounds out;
    for (EntityId id : doc.SelectedEntityIds()) {
        const Entity* e = doc.FindEntity(id);
        if (!e) continue;
        const GeoBounds b = e->Shaper().Bounds();
        if (!b.valid) continue;
        if (!out.valid) {
            out = b;
            out.valid = true;
        } else {
            out.minX = std::min(out.minX, b.minX);
            out.minY = std::min(out.minY, b.minY);
            out.maxX = std::max(out.maxX, b.maxX);
            out.maxY = std::max(out.maxY, b.maxY);
        }
    }
    return out;
}

} // namespace

class BlockEditorDialog::DirtyObserver : public IDocumentObserver {
public:
    explicit DirtyObserver(bool& dirtyFlag)
        : dirtyFlag_(dirtyFlag)
    {
    }

    void OnEntityAdded(const Entity&) override { dirtyFlag_ = true; }
    void OnEntityRemoved(EntityId) override { dirtyFlag_ = true; }
    void OnEntitiesRemoved(const std::vector<EntityId>&) override { dirtyFlag_ = true; }
    void OnEntitiesChanged(const std::vector<EntityId>&) override { dirtyFlag_ = true; }
    void OnSelectionChanged() override {}
    void OnDocumentReset() override { dirtyFlag_ = true; }

private:
    bool& dirtyFlag_;
};

BlockEditorDialog::BlockEditorDialog(
    CadDocument& hostDocument,
    const std::string& blockName,
    QWidget* parent)
    : QDialog(parent)
    , hostDocument_(hostDocument)
    , blockName_(blockName)
    , editCommands_(editDocument_)
{
    setWindowTitle(QStringLiteral("块编辑器 - %1").arg(QString::fromStdString(blockName_)));
    setModal(true);
    resize(1100, 720);
    setStyleSheet(QStringLiteral(
        "QDialog { background-color: #2d2d30; color: #f0f0f0; }"
        "QLabel, QGroupBox, QListWidget { color: #f0f0f0; }"
        "QListWidget { background-color: #252526; border: 1px solid #555; }"
        "QGroupBox { border: 1px solid #555; margin-top: 8px; padding-top: 8px; }"
        "QPushButton {"
        "  background-color: #3e3e42; color: #f0f0f0; border: 1px solid #555555;"
        "  padding: 4px 8px; min-height: 22px;"
        "}"
        "QPushButton:hover { background-color: #094771; border-color: #007acc; }"));

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);
    root->setSpacing(6);

    titleLabel_ = new QLabel(
        QStringLiteral("块编辑器 (BEDIT) — 「%1」。左侧为块编写选项板（参数/动作/可见性/查表）。")
            .arg(QString::fromStdString(blockName_)),
        this);
    root->addWidget(titleLabel_);

    auto* toolbar = new QHBoxLayout();
    auto* saveButton = new QPushButton(QStringLiteral("BSAVE 保存"), this);
    auto* saveAsButton = new QPushButton(QStringLiteral("BSAVEAS 另存为"), this);
    auto* closeButton = new QPushButton(QStringLiteral("BCLOSE 关闭"), this);
    toolbar->addWidget(saveButton);
    toolbar->addWidget(saveAsButton);
    toolbar->addStretch();
    toolbar->addWidget(closeButton);
    root->addLayout(toolbar);

    auto* body = new QHBoxLayout();

    // ---- Authoring palette (left) ----
    auto* paletteScroll = new QScrollArea(this);
    paletteScroll->setWidgetResizable(true);
    paletteScroll->setFixedWidth(280);
    auto* paletteHost = new QWidget(paletteScroll);
    auto* palette = new QVBoxLayout(paletteHost);

    auto* paramGroup = new QGroupBox(QStringLiteral("参数 (BPARAMETER)"), paletteHost);
    auto* paramLayout = new QVBoxLayout(paramGroup);
    auto addParamBtn = [&](const QString& text, auto slot) {
        auto* b = new QPushButton(text, paramGroup);
        connect(b, &QPushButton::clicked, this, slot);
        paramLayout->addWidget(b);
    };
    addParamBtn(QStringLiteral("线性"), [this]() { OnAddLinearParam(); });
    addParamBtn(QStringLiteral("点"), [this]() { OnAddPointParam(); });
    addParamBtn(QStringLiteral("旋转"), [this]() { OnAddRotationParam(); });
    addParamBtn(QStringLiteral("翻转"), [this]() { OnAddFlipParam(); });
    addParamBtn(QStringLiteral("可见性"), [this]() { OnAddVisibilityParam(); });
    addParamBtn(QStringLiteral("查询"), [this]() { OnAddLookupParam(); });
    paramList_ = new QListWidget(paramGroup);
    paramLayout->addWidget(paramList_, 1);
    auto* delParam = new QPushButton(QStringLiteral("删除参数"), paramGroup);
    connect(delParam, &QPushButton::clicked, this, [this]() { OnDeleteSelectedParam(); });
    paramLayout->addWidget(delParam);
    palette->addWidget(paramGroup);

    auto* actionGroup = new QGroupBox(QStringLiteral("动作 (BACTION)"), paletteHost);
    auto* actionLayout = new QVBoxLayout(actionGroup);
    auto addActBtn = [&](const QString& text, auto slot) {
        auto* b = new QPushButton(text, actionGroup);
        connect(b, &QPushButton::clicked, this, slot);
        actionLayout->addWidget(b);
    };
    addActBtn(QStringLiteral("拉伸（需先选图元）"), [this]() { OnAddStretchAction(); });
    addActBtn(QStringLiteral("移动"), [this]() { OnAddMoveAction(); });
    addActBtn(QStringLiteral("缩放"), [this]() { OnAddScaleAction(); });
    addActBtn(QStringLiteral("旋转"), [this]() { OnAddRotateAction(); });
    addActBtn(QStringLiteral("翻转"), [this]() { OnAddFlipAction(); });
    addActBtn(QStringLiteral("阵列"), [this]() { OnAddArrayAction(); });
    actionList_ = new QListWidget(actionGroup);
    actionLayout->addWidget(actionList_, 1);
    auto* delAct = new QPushButton(QStringLiteral("删除动作"), actionGroup);
    connect(delAct, &QPushButton::clicked, this, [this]() { OnDeleteSelectedAction(); });
    actionLayout->addWidget(delAct);
    palette->addWidget(actionGroup);

    auto* moreGroup = new QGroupBox(QStringLiteral("可见性 / 查表"), paletteHost);
    auto* moreLayout = new QVBoxLayout(moreGroup);
    auto* visBtn = new QPushButton(QStringLiteral("BVSTATE 编辑可见性状态"), moreGroup);
    auto* lookupBtn = new QPushButton(QStringLiteral("BLOOKUPTABLE 编辑查表"), moreGroup);
    connect(visBtn, &QPushButton::clicked, this, [this]() { OnEditVisibilityStates(); });
    connect(lookupBtn, &QPushButton::clicked, this, [this]() { OnEditLookupTable(); });
    moreLayout->addWidget(visBtn);
    moreLayout->addWidget(lookupBtn);
    palette->addWidget(moreGroup);
    palette->addStretch();

    paletteScroll->setWidget(paletteHost);
    body->addWidget(paletteScroll);

    editScene_ = new CadScene(editDocument_, this);
    editView_ = new CadView(this);
    editView_->SetDocument(&editDocument_);
    editView_->SetScene(editScene_);
    editView_->SetCommandManager(&editCommands_);
    editView_->SetStatusCallback([this](const QString& text) {
        titleLabel_->setText(
            QStringLiteral("块编辑器 (BEDIT) — %1 | %2")
                .arg(QString::fromStdString(blockName_), text));
    });
    body->addWidget(editView_, 1);
    root->addLayout(body, 1);

    LoadDefinitionIntoEditor();

    dirtyObserver_ = new DirtyObserver(dirty_);
    editDocument_.AddObserver(dirtyObserver_);

    connect(saveButton, &QPushButton::clicked, this, [this]() { OnSave(); });
    connect(saveAsButton, &QPushButton::clicked, this, [this]() { OnSaveAs(); });
    connect(closeButton, &QPushButton::clicked, this, [this]() { OnCloseEditor(); });

    QTimer::singleShot(0, this, [this]() {
        if (editView_) {
            editView_->ZoomExtents();
            editView_->SetToolMode(ToolMode::Select);
        }
    });
}

BlockEditorDialog::~BlockEditorDialog()
{
    if (dirtyObserver_) {
        editDocument_.RemoveObserver(dirtyObserver_);
        delete dirtyObserver_;
        dirtyObserver_ = nullptr;
    }
}

void BlockEditorDialog::LoadDefinitionIntoEditor()
{
    editDocument_.Clear();
    for (const Layer& layer : hostDocument_.Layers()) {
        Layer& dst = editDocument_.EnsureLayer(layer.Name());
        dst.SetColor(layer.Color());
        dst.SetLineTypeName(layer.LineTypeName());
        dst.SetLineWeightMm(layer.LineWeightMm());
        dst.SetFrozen(layer.IsFrozen());
        dst.SetLocked(layer.IsLocked());
        dst.SetOn(layer.IsOn());
        dst.SetPlottable(layer.IsPlottable());
    }
    for (const LineTypeDefinition& lt : hostDocument_.LineTypes()) {
        editDocument_.AddLineType(lt);
    }

    const BlockDefinition* def = hostDocument_.FindBlockDefinition(blockName_);
    if (!def) {
        return;
    }

    dynamicData_ = def->dynamic;
    paramSerial_ = static_cast<int>(dynamicData_.parameters.size()) + 1;
    actionSerial_ = static_cast<int>(dynamicData_.actions.size()) + 1;

    for (const std::shared_ptr<Entity>& source : def->entities) {
        if (!source) {
            continue;
        }
        std::unique_ptr<Entity> clone = source->Clone();
        if (!clone) {
            continue;
        }
        clone->SetId(IdGenerator::Next());
        clone->SetSelected(false);
        editDocument_.AddEntity(std::move(clone));
    }

    RefreshDynamicLists();
    dirty_ = false;
}

bool BlockEditorDialog::CollectEditorEntities(std::vector<std::shared_ptr<Entity>>& out) const
{
    out.clear();
    for (const auto& entity : editDocument_.Entities()) {
        if (!entity) {
            continue;
        }
        std::unique_ptr<Entity> clone = entity->Clone();
        if (!clone) {
            continue;
        }
        clone->SetId(IdGenerator::Next());
        clone->SetSelected(false);
        out.push_back(std::shared_ptr<Entity>(std::move(clone)));
    }
    return !out.empty();
}

std::vector<std::size_t> BlockEditorDialog::SelectedEntityIndices() const
{
    std::vector<std::size_t> indices;
    const auto& entities = editDocument_.Entities();
    const auto selected = editDocument_.SelectedEntityIds();
    for (EntityId id : selected) {
        for (std::size_t i = 0; i < entities.size(); ++i) {
            if (entities[i] && entities[i]->Id() == id) {
                indices.push_back(i);
                break;
            }
        }
    }
    return indices;
}

void BlockEditorDialog::RefreshDynamicLists()
{
    if (paramList_) {
        paramList_->clear();
        for (const DynParameter& p : dynamicData_.parameters) {
            paramList_->addItem(
                QStringLiteral("%1  %2  [%3]")
                    .arg(QString::fromUtf8(DynParamTypeName(p.type)),
                         QString::fromStdString(p.name),
                         QString::fromStdString(p.id)));
        }
    }
    if (actionList_) {
        actionList_->clear();
        for (const DynAction& a : dynamicData_.actions) {
            actionList_->addItem(
                QStringLiteral("%1  %2 → %3  (%4 图元)")
                    .arg(QString::fromUtf8(DynActionTypeName(a.type)),
                         QString::fromStdString(a.name),
                         QString::fromStdString(a.parameterId))
                    .arg(a.entityIndices.size()));
        }
    }
}

void BlockEditorDialog::PersistDynamicToHost(BlockDefinition& def) const
{
    def.dynamic = dynamicData_;
}

void BlockEditorDialog::OnAddLinearParam()
{
    const GeoBounds b = SelectionBounds(editDocument_);
    DynParameter p;
    p.id = DynamicBlockEvaluator::MakeParameterId(DynParamType::Linear, paramSerial_++);
    p.name = "距离" + std::to_string(paramSerial_ - 1);
    p.type = DynParamType::Linear;
    if (b.valid) {
        p.basePoint = GeoPoint(b.minX, 0.5 * (b.minY + b.maxY));
        p.endPoint = GeoPoint(b.maxX, 0.5 * (b.minY + b.maxY));
        p.defaultValue = b.maxX - b.minX;
    } else {
        const GeoPoint c = BoundsCenter(editDocument_);
        p.basePoint = c;
        p.endPoint = GeoPoint(c.X() + 100.0, c.Y());
        p.defaultValue = 100.0;
    }
    p.minValue = 0.0;
    p.maxValue = 1.0e6;
    dynamicData_.parameters.push_back(p);
    dirty_ = true;
    RefreshDynamicLists();
}

void BlockEditorDialog::OnAddPointParam()
{
    const GeoPoint c = BoundsCenter(editDocument_);
    DynParameter p;
    p.id = DynamicBlockEvaluator::MakeParameterId(DynParamType::Point, paramSerial_++);
    p.name = "位置" + std::to_string(paramSerial_ - 1);
    p.type = DynParamType::Point;
    p.basePoint = c;
    p.endPoint = GeoPoint(c.X() + 50.0, c.Y());
    p.defaultValue = 50.0;
    dynamicData_.parameters.push_back(p);
    dirty_ = true;
    RefreshDynamicLists();
}

void BlockEditorDialog::OnAddRotationParam()
{
    const GeoPoint c = BoundsCenter(editDocument_);
    DynParameter p;
    p.id = DynamicBlockEvaluator::MakeParameterId(DynParamType::Rotation, paramSerial_++);
    p.name = "角度" + std::to_string(paramSerial_ - 1);
    p.type = DynParamType::Rotation;
    p.basePoint = c;
    p.endPoint = GeoPoint(c.X() + 50.0, c.Y());
    p.defaultValue = 0.0;
    p.minValue = -6.283185307179586;
    p.maxValue = 6.283185307179586;
    dynamicData_.parameters.push_back(p);
    dirty_ = true;
    RefreshDynamicLists();
}

void BlockEditorDialog::OnAddFlipParam()
{
    const GeoBounds b = SelectionBounds(editDocument_);
    DynParameter p;
    p.id = DynamicBlockEvaluator::MakeParameterId(DynParamType::Flip, paramSerial_++);
    p.name = "翻转" + std::to_string(paramSerial_ - 1);
    p.type = DynParamType::Flip;
    if (b.valid) {
        p.basePoint = GeoPoint(0.5 * (b.minX + b.maxX), b.minY);
        p.endPoint = GeoPoint(0.5 * (b.minX + b.maxX), b.maxY);
    } else {
        const GeoPoint c = BoundsCenter(editDocument_);
        p.basePoint = GeoPoint(c.X(), c.Y() - 50.0);
        p.endPoint = GeoPoint(c.X(), c.Y() + 50.0);
    }
    p.defaultValue = 0.0;
    dynamicData_.parameters.push_back(p);
    dirty_ = true;
    RefreshDynamicLists();
}

void BlockEditorDialog::OnAddVisibilityParam()
{
    DynParameter p;
    p.id = DynamicBlockEvaluator::MakeParameterId(DynParamType::Visibility, paramSerial_++);
    p.name = "可见性";
    p.type = DynParamType::Visibility;
    p.visibilityStates = {"状态1", "状态2"};
    p.defaultValue = 0.0;
    // Default overrides: all visible in both states.
    const std::size_t n = editDocument_.Entities().size();
    for (const std::string& state : p.visibilityStates) {
        DynVisibilityOverride ov;
        ov.stateName = state;
        ov.entityVisible.assign(n, true);
        dynamicData_.visibilityOverrides.push_back(ov);
    }
    dynamicData_.parameters.push_back(p);
    dirty_ = true;
    RefreshDynamicLists();
    QMessageBox::information(
        this,
        QStringLiteral("可见性"),
        QStringLiteral("已添加可见性参数。用「BVSTATE 编辑可见性状态」为每个状态勾选显示的图元。"));
}

void BlockEditorDialog::OnAddLookupParam()
{
    DynParameter p;
    p.id = DynamicBlockEvaluator::MakeParameterId(DynParamType::Lookup, paramSerial_++);
    p.name = "查表";
    p.type = DynParamType::Lookup;
    p.defaultValue = 0.0;
    dynamicData_.parameters.push_back(p);

    DynLookupTable table;
    table.parameterId = p.id;
    // Bind first linear param as column if any.
    for (const DynParameter& other : dynamicData_.parameters) {
        if (other.type == DynParamType::Linear) {
            table.columns.push_back({other.id, other.name});
            break;
        }
    }
    if (!table.columns.empty()) {
        DynLookupRow r1;
        r1.label = "规格A";
        r1.values = {100.0};
        DynLookupRow r2;
        r2.label = "规格B";
        r2.values = {200.0};
        table.rows = {r1, r2};
    }
    dynamicData_.lookupTables.push_back(table);
    dirty_ = true;
    RefreshDynamicLists();
}

DynParameter* FindEditableParam(DynamicBlockData& data, DynParamType type)
{
    for (auto it = data.parameters.rbegin(); it != data.parameters.rend(); ++it) {
        if (it->type == type) {
            return &(*it);
        }
    }
    return nullptr;
}

void BlockEditorDialog::OnAddStretchAction()
{
    const auto indices = SelectedEntityIndices();
    if (indices.empty()) {
        QMessageBox::information(this, QStringLiteral("拉伸"), QStringLiteral("请先选择要拉伸的图元。"));
        return;
    }
    DynParameter* param = FindEditableParam(dynamicData_, DynParamType::Linear);
    if (!param) {
        param = FindEditableParam(dynamicData_, DynParamType::Polar);
    }
    if (!param) {
        QMessageBox::information(this, QStringLiteral("拉伸"), QStringLiteral("请先添加线性（或极轴）参数。"));
        return;
    }
    DynAction a;
    a.id = DynamicBlockEvaluator::MakeActionId(DynActionType::Stretch, actionSerial_++);
    a.name = "拉伸";
    a.type = DynActionType::Stretch;
    a.parameterId = param->id;
    a.entityIndices = indices;
    const GeoBounds b = SelectionBounds(editDocument_);
    if (b.valid) {
        // Stretch frame = right half of selection (AutoCAD-like for width stretch).
        a.hasStretchFrame = true;
        a.stretchMin = GeoPoint(0.5 * (b.minX + b.maxX), b.minY - 1.0);
        a.stretchMax = GeoPoint(b.maxX + 1.0, b.maxY + 1.0);
    }
    dynamicData_.actions.push_back(a);
    dirty_ = true;
    RefreshDynamicLists();
}

void BlockEditorDialog::OnAddMoveAction()
{
    const auto indices = SelectedEntityIndices();
    DynParameter* param = FindEditableParam(dynamicData_, DynParamType::Point);
    if (!param) {
        param = FindEditableParam(dynamicData_, DynParamType::Linear);
    }
    if (!param) {
        QMessageBox::information(this, QStringLiteral("移动"), QStringLiteral("请先添加点或线性参数。"));
        return;
    }
    DynAction a;
    a.id = DynamicBlockEvaluator::MakeActionId(DynActionType::Move, actionSerial_++);
    a.name = "移动";
    a.type = DynActionType::Move;
    a.parameterId = param->id;
    a.entityIndices = indices;
    dynamicData_.actions.push_back(a);
    dirty_ = true;
    RefreshDynamicLists();
}

void BlockEditorDialog::OnAddScaleAction()
{
    const auto indices = SelectedEntityIndices();
    DynParameter* param = FindEditableParam(dynamicData_, DynParamType::Linear);
    if (!param) {
        QMessageBox::information(this, QStringLiteral("缩放"), QStringLiteral("请先添加线性参数。"));
        return;
    }
    DynAction a;
    a.id = DynamicBlockEvaluator::MakeActionId(DynActionType::Scale, actionSerial_++);
    a.name = "缩放";
    a.type = DynActionType::Scale;
    a.parameterId = param->id;
    a.entityIndices = indices;
    dynamicData_.actions.push_back(a);
    dirty_ = true;
    RefreshDynamicLists();
}

void BlockEditorDialog::OnAddRotateAction()
{
    const auto indices = SelectedEntityIndices();
    DynParameter* param = FindEditableParam(dynamicData_, DynParamType::Rotation);
    if (!param) {
        QMessageBox::information(this, QStringLiteral("旋转"), QStringLiteral("请先添加旋转参数。"));
        return;
    }
    DynAction a;
    a.id = DynamicBlockEvaluator::MakeActionId(DynActionType::Rotate, actionSerial_++);
    a.name = "旋转";
    a.type = DynActionType::Rotate;
    a.parameterId = param->id;
    a.entityIndices = indices;
    dynamicData_.actions.push_back(a);
    dirty_ = true;
    RefreshDynamicLists();
}

void BlockEditorDialog::OnAddFlipAction()
{
    const auto indices = SelectedEntityIndices();
    DynParameter* param = FindEditableParam(dynamicData_, DynParamType::Flip);
    if (!param) {
        QMessageBox::information(this, QStringLiteral("翻转"), QStringLiteral("请先添加翻转参数。"));
        return;
    }
    DynAction a;
    a.id = DynamicBlockEvaluator::MakeActionId(DynActionType::Flip, actionSerial_++);
    a.name = "翻转";
    a.type = DynActionType::Flip;
    a.parameterId = param->id;
    a.entityIndices = indices;
    dynamicData_.actions.push_back(a);
    dirty_ = true;
    RefreshDynamicLists();
}

void BlockEditorDialog::OnAddArrayAction()
{
    const auto indices = SelectedEntityIndices();
    if (indices.empty()) {
        QMessageBox::information(this, QStringLiteral("阵列"), QStringLiteral("请先选择要阵列的图元。"));
        return;
    }
    DynParameter* param = FindEditableParam(dynamicData_, DynParamType::Linear);
    if (!param) {
        QMessageBox::information(this, QStringLiteral("阵列"), QStringLiteral("请先添加线性参数。"));
        return;
    }
    DynAction a;
    a.id = DynamicBlockEvaluator::MakeActionId(DynActionType::Array, actionSerial_++);
    a.name = "阵列";
    a.type = DynActionType::Array;
    a.parameterId = param->id;
    a.entityIndices = indices;
    a.arraySpacing = 20.0;
    dynamicData_.actions.push_back(a);
    dirty_ = true;
    RefreshDynamicLists();
}

void BlockEditorDialog::OnEditVisibilityStates()
{
    DynParameter* vis = FindEditableParam(dynamicData_, DynParamType::Visibility);
    if (!vis) {
        QMessageBox::information(this, QStringLiteral("BVSTATE"), QStringLiteral("请先添加可见性参数。"));
        return;
    }
    bool ok = false;
    QStringList states;
    for (const std::string& s : vis->visibilityStates) {
        states << QString::fromStdString(s);
    }
    const QString joined = QInputDialog::getText(
        this,
        QStringLiteral("BVSTATE"),
        QStringLiteral("状态名称（用逗号分隔）："),
        QLineEdit::Normal,
        states.join(QLatin1Char(',')),
        &ok);
    if (!ok) {
        return;
    }
    vis->visibilityStates.clear();
    for (QString part : joined.split(QLatin1Char(','))) {
        part = part.trimmed();
        if (!part.isEmpty()) {
            vis->visibilityStates.push_back(part.toStdString());
        }
    }
    if (vis->visibilityStates.empty()) {
        vis->visibilityStates.push_back("状态1");
    }

    const std::size_t n = editDocument_.Entities().size();
    const auto selected = SelectedEntityIndices();
    dynamicData_.visibilityOverrides.clear();
    for (std::size_t si = 0; si < vis->visibilityStates.size(); ++si) {
        DynVisibilityOverride ov;
        ov.stateName = vis->visibilityStates[si];
        ov.entityVisible.assign(n, si == 0); // state0: all; others: only selected if any
        if (si == 0) {
            ov.entityVisible.assign(n, true);
        } else if (!selected.empty()) {
            ov.entityVisible.assign(n, false);
            for (std::size_t idx : selected) {
                if (idx < n) {
                    ov.entityVisible[idx] = true;
                }
            }
        } else {
            ov.entityVisible.assign(n, true);
        }
        dynamicData_.visibilityOverrides.push_back(ov);
    }
    dirty_ = true;
    RefreshDynamicLists();
    QMessageBox::information(
        this,
        QStringLiteral("BVSTATE"),
        QStringLiteral("已更新可见性状态。\n状态1：全部显示；\n后续状态：仅当前选中图元显示（若有选择）。"));
}

void BlockEditorDialog::OnEditLookupTable()
{
    if (dynamicData_.lookupTables.empty()) {
        QMessageBox::information(this, QStringLiteral("查表"), QStringLiteral("请先添加查询参数。"));
        return;
    }
    DynLookupTable& table = dynamicData_.lookupTables.back();
    bool ok = false;
    const QString text = QInputDialog::getMultiLineText(
        this,
        QStringLiteral("BLOOKUPTABLE"),
        QStringLiteral("每行：标签,数值1,数值2...\n（列对应当前绑定的线性参数）"),
        QStringLiteral("规格A,100\n规格B,200"),
        &ok);
    if (!ok) {
        return;
    }
    table.rows.clear();
    for (QString line : text.split(QLatin1Char('\n'))) {
        line = line.trimmed();
        if (line.isEmpty()) continue;
        const QStringList parts = line.split(QLatin1Char(','));
        if (parts.isEmpty()) continue;
        DynLookupRow row;
        row.label = parts[0].trimmed().toStdString();
        for (int i = 1; i < parts.size(); ++i) {
            row.values.push_back(parts[i].trimmed().toDouble());
        }
        table.rows.push_back(row);
    }
    dirty_ = true;
    RefreshDynamicLists();
}

void BlockEditorDialog::OnDeleteSelectedParam()
{
    const int row = paramList_ ? paramList_->currentRow() : -1;
    if (row < 0 || row >= static_cast<int>(dynamicData_.parameters.size())) {
        return;
    }
    const std::string id = dynamicData_.parameters[static_cast<std::size_t>(row)].id;
    dynamicData_.parameters.erase(dynamicData_.parameters.begin() + row);
    dynamicData_.actions.erase(
        std::remove_if(
            dynamicData_.actions.begin(),
            dynamicData_.actions.end(),
            [&](const DynAction& a) { return a.parameterId == id; }),
        dynamicData_.actions.end());
    dirty_ = true;
    RefreshDynamicLists();
}

void BlockEditorDialog::OnDeleteSelectedAction()
{
    const int row = actionList_ ? actionList_->currentRow() : -1;
    if (row < 0 || row >= static_cast<int>(dynamicData_.actions.size())) {
        return;
    }
    dynamicData_.actions.erase(dynamicData_.actions.begin() + row);
    dirty_ = true;
    RefreshDynamicLists();
}

void BlockEditorDialog::OnSave()
{
    std::vector<std::shared_ptr<Entity>> entities;
    if (!CollectEditorEntities(entities)) {
        QMessageBox::warning(this, QStringLiteral("BSAVE"), QStringLiteral("块定义不能为空。"));
        return;
    }

    BlockDefinition* def = hostDocument_.FindBlockDefinition(blockName_);
    if (!def) {
        QMessageBox::warning(this, QStringLiteral("BSAVE"), QStringLiteral("宿主文档中找不到该块定义。"));
        return;
    }

    const double baseX = def->baseX;
    const double baseY = def->baseY;
    PersistDynamicToHost(*def);
    if (!hostDocument_.ReplaceBlockDefinitionEntities(blockName_, std::move(entities), baseX, baseY)) {
        QMessageBox::warning(this, QStringLiteral("BSAVE"), QStringLiteral("保存失败。"));
        return;
    }
    // ReplaceBlockDefinitionEntities keeps dynamic if we set it before; ensure again after entity replace.
    if (BlockDefinition* updated = hostDocument_.FindBlockDefinition(blockName_)) {
        updated->dynamic = dynamicData_;
        hostDocument_.RefreshBlockReferenceCaches(blockName_);
    }

    dirty_ = false;
    definitionChanged_ = true;
    titleLabel_->setText(
        QStringLiteral("块编辑器 (BEDIT) — 已保存「%1」（含动态块数据），全部参照已更新。")
            .arg(QString::fromStdString(blockName_)));
}

void BlockEditorDialog::OnSaveAs()
{
    bool ok = false;
    const QString name = QInputDialog::getText(
        this,
        QStringLiteral("BSAVEAS"),
        QStringLiteral("新块名称："),
        QLineEdit::Normal,
        QString::fromStdString(blockName_ + "_copy"),
        &ok).trimmed();
    if (!ok || name.isEmpty()) {
        return;
    }
    if (hostDocument_.HasBlockDefinition(name.toStdString())) {
        QMessageBox::warning(this, QStringLiteral("BSAVEAS"), QStringLiteral("该名称已存在。"));
        return;
    }

    std::vector<std::shared_ptr<Entity>> entities;
    if (!CollectEditorEntities(entities)) {
        QMessageBox::warning(this, QStringLiteral("BSAVEAS"), QStringLiteral("块定义不能为空。"));
        return;
    }

    const BlockDefinition* src = hostDocument_.FindBlockDefinition(blockName_);
    BlockDefinition neu;
    neu.name = name.toStdString();
    neu.baseX = src ? src->baseX : 0.0;
    neu.baseY = src ? src->baseY : 0.0;
    neu.allowExplode = src ? src->allowExplode : true;
    neu.uniformScale = src ? src->uniformScale : true;
    neu.annotative = src ? src->annotative : false;
    neu.attributeDefs = src ? src->attributeDefs : std::vector<BlockAttributeDef>{};
    neu.entities = std::move(entities);
    neu.dynamic = dynamicData_;
    hostDocument_.AddBlockDefinition(std::move(neu));

    dirty_ = false;
    definitionChanged_ = true;
    blockName_ = name.toStdString();
    setWindowTitle(QStringLiteral("块编辑器 - %1").arg(name));
    titleLabel_->setText(
        QStringLiteral("块编辑器 (BEDIT) — 已另存为「%1」。").arg(name));
}

void BlockEditorDialog::OnCloseEditor()
{
    if (dirty_) {
        const auto answer = QMessageBox::question(
            this,
            QStringLiteral("BCLOSE"),
            QStringLiteral("块定义已修改，是否保存？"),
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
            QMessageBox::Save);
        if (answer == QMessageBox::Cancel) {
            return;
        }
        if (answer == QMessageBox::Save) {
            OnSave();
            if (dirty_) {
                return;
            }
        }
    }
    closing_ = true;
    accept();
}

void BlockEditorDialog::reject()
{
    if (closing_) {
        QDialog::reject();
        return;
    }
    OnCloseEditor();
}

} // namespace cad
