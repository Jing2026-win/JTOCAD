#include "Ui/LayerRibbonWidget.h"

#include "Ui/LayerControlBar.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QSizePolicy>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <vector>

namespace cad {

namespace {

constexpr int kPropsButtonWidth = 50;
constexpr int kQuickIconWidth = 20;
constexpr int kQuickRowHeight = 20;
constexpr int kQuickIconGap = 2;
constexpr int kLabeledMinWidth = 74;
// 下拉条宽度与下方图标行对齐：4 个图标 + 间距 +「置为当前」
constexpr int kControlColumnWidth =
    4 * kQuickIconWidth + 3 * kQuickIconGap + kLabeledMinWidth; // 160
constexpr int kDropdownHeight = 20;
constexpr int kContentVMargin = 2;
constexpr int kContentVSpacing = 1;
// 下拉 + 两行图标 + 间距，刚好落在面板标题「图层」上方，避免叠字
constexpr int kRibbonHeight =
    kContentVMargin * 2 + kDropdownHeight + kQuickRowHeight * 2 + kContentVSpacing * 2; // 66

enum class QuickActionKind {
    LayerOff,
    LayerIsolate,
    LayerFreeze,
    LayerLock,
    LayerSetCurrent,
    LayerOnAll,
    LayerUnisolate,
    LayerThaw,
    LayerUnlock,
    LayerMatch,
};

class LayerPropertiesButton : public QToolButton {
public:
    explicit LayerPropertiesButton(QWidget* parent = nullptr)
        : QToolButton(parent)
    {
        setToolTip(QObject::tr("图层特性管理器"));
        setAutoRaise(true);
        setFixedSize(kPropsButtonWidth, kRibbonHeight);
        setStyleSheet(QStringLiteral(
            "QToolButton { border: none; padding: 0px; }"
            "QToolButton:hover { background-color: #3e3e42; border-radius: 2px; }"
            "QToolButton:pressed { background-color: #094771; }"));
    }

protected:
    void paintEvent(QPaintEvent* event) override
    {
        Q_UNUSED(event);
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);

        if (underMouse()) {
            painter.fillRect(rect(), QColor(62, 62, 66));
        }

        const QRect iconRect(6, 4, 38, 30);
        painter.setPen(QColor(200, 200, 200));
        painter.setBrush(QColor(240, 240, 240));
        for (int i = 2; i >= 0; --i) {
            const QRect sheet(iconRect.left() + i * 3, iconRect.top() + (2 - i) * 3, 24, 14);
            painter.drawRect(sheet);
        }

        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(58, 142, 255));
        painter.drawRect(iconRect.left() + 18, iconRect.top() + 14, 13, 11);
        painter.setPen(QColor(255, 255, 255));
        painter.drawLine(iconRect.left() + 20, iconRect.top() + 18, iconRect.left() + 29, iconRect.top() + 18);
        painter.drawLine(iconRect.left() + 24, iconRect.top() + 15, iconRect.left() + 24, iconRect.top() + 23);

        painter.setPen(QColor(236, 236, 236));
        QFont font = painter.font();
        font.setPointSize(9);
        painter.setFont(font);
        painter.drawText(QRect(0, 38, width(), 12), Qt::AlignHCenter | Qt::AlignVCenter, QStringLiteral("图层"));
        painter.drawText(QRect(0, 50, width(), 12), Qt::AlignHCenter | Qt::AlignVCenter, QStringLiteral("特性"));
    }
};

void PaintLayerStack(QPainter& painter, const QRect& rect, const QColor& sheetColor)
{
    painter.save();
    painter.setPen(QPen(sheetColor.darker(120), 0.8));
    painter.setBrush(sheetColor);
    const QPoint base(rect.left() + 1, rect.top() + 1);
    for (int i = 0; i < 3; ++i) {
        painter.drawRect(base.x() + i * 2, base.y() + i * 2, 13, 8);
    }
    painter.restore();
}

class LayerQuickButton : public QToolButton {
public:
    LayerQuickButton(QuickActionKind kind, const QString& tooltip, QWidget* parent = nullptr)
        : QToolButton(parent)
        , kind_(kind)
    {
        setToolTip(tooltip);
        setAutoRaise(true);
        setToolButtonStyle(Qt::ToolButtonIconOnly);
        setFixedSize(kQuickIconWidth, kQuickRowHeight);
        setStyleSheet(QStringLiteral(
            "QToolButton { border: none; padding: 0px; background: transparent; }"
            "QToolButton:hover { background-color: #3e3e42; border-radius: 2px; }"
            "QToolButton:pressed { background-color: #094771; }"));
    }

protected:
    void paintEvent(QPaintEvent* event) override
    {
        QToolButton::paintEvent(event);

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);

        const QRect iconRect((width() - 17) / 2, (height() - 15) / 2, 17, 15);

        const QColor sheet(238, 238, 238);
        const QColor accentCyan(76, 198, 255);
        const QColor accentYellow(255, 192, 0);

        PaintLayerStack(painter, iconRect, sheet);

        painter.setPen(QPen(accentCyan, 1.4));
        painter.setBrush(Qt::NoBrush);

        switch (kind_) {
        case QuickActionKind::LayerOff: {
            painter.setPen(QPen(accentCyan, 1.0));
            painter.setBrush(accentCyan);
            painter.drawEllipse(QPoint(iconRect.center().x(), iconRect.bottom() - 2), 2, 2);
            break;
        }
        case QuickActionKind::LayerIsolate: {
            painter.drawLine(iconRect.left() + 3, iconRect.top() + 5, iconRect.right() - 2, iconRect.bottom() - 3);
            painter.drawLine(iconRect.right() - 5, iconRect.bottom() - 3, iconRect.right() - 2, iconRect.bottom() - 3);
            painter.drawLine(iconRect.right() - 2, iconRect.bottom() - 6, iconRect.right() - 2, iconRect.bottom() - 3);
            break;
        }
        case QuickActionKind::LayerFreeze: {
            painter.setPen(QPen(accentCyan, 1.0));
            for (int i = 0; i < 6; ++i) {
                const double angle = i * 3.14159265358979323846 / 3.0;
                const QPointF c = iconRect.center();
                painter.drawLine(
                    c.x() + std::cos(angle) * 3.0, c.y() + std::sin(angle) * 3.0,
                    c.x() + std::cos(angle) * 5.5, c.y() + std::sin(angle) * 5.5);
            }
            break;
        }
        case QuickActionKind::LayerLock: {
            painter.setPen(QPen(accentCyan, 1.1));
            painter.drawRoundedRect(iconRect.center().x() - 3, iconRect.center().y(), 6, 5, 1, 1);
            painter.drawArc(iconRect.center().x() - 3, iconRect.center().y() - 5, 6, 6, 0, 180 * 16);
            break;
        }
        case QuickActionKind::LayerSetCurrent: {
            painter.setPen(Qt::NoPen);
            painter.setBrush(accentCyan);
            painter.drawEllipse(iconRect.center(), 3, 3);
            break;
        }
        case QuickActionKind::LayerOnAll: {
            painter.setPen(QPen(accentYellow, 1.0));
            painter.setBrush(accentYellow);
            painter.drawEllipse(QPoint(iconRect.center().x(), iconRect.bottom() - 2), 2, 2);
            break;
        }
        case QuickActionKind::LayerUnisolate: {
            painter.setPen(QPen(accentYellow, 1.0));
            painter.drawLine(iconRect.left() + 3, iconRect.bottom() - 3, iconRect.right() - 2, iconRect.top() + 5);
            painter.drawLine(iconRect.right() - 5, iconRect.top() + 5, iconRect.right() - 2, iconRect.top() + 5);
            painter.drawLine(iconRect.right() - 2, iconRect.top() + 2, iconRect.right() - 2, iconRect.top() + 5);
            break;
        }
        case QuickActionKind::LayerThaw: {
            painter.setPen(QPen(accentYellow, 1.0));
            painter.setBrush(QColor(255, 196, 0, 50));
            painter.drawEllipse(iconRect.center(), 4, 4);
            for (int i = 0; i < 8; ++i) {
                const double angle = i * 3.14159265358979323846 / 4.0;
                const QPointF c = iconRect.center();
                painter.drawLine(
                    c.x() + std::cos(angle) * 5.0, c.y() + std::sin(angle) * 5.0,
                    c.x() + std::cos(angle) * 6.5, c.y() + std::sin(angle) * 6.5);
            }
            break;
        }
        case QuickActionKind::LayerUnlock: {
            painter.setPen(QPen(accentYellow, 1.1));
            painter.drawRoundedRect(iconRect.center().x() - 3, iconRect.center().y(), 6, 5, 1, 1);
            painter.drawArc(iconRect.center().x() - 3, iconRect.center().y() - 5, 6, 6, 0, 180 * 16);
            painter.drawLine(iconRect.center().x() - 1, iconRect.center().y() + 1, iconRect.center().x() + 1, iconRect.center().y() + 4);
            break;
        }
        case QuickActionKind::LayerMatch: {
            painter.setPen(QPen(accentYellow, 1.1));
            painter.drawLine(iconRect.left() + 4, iconRect.bottom() - 2, iconRect.right() - 3, iconRect.top() + 4);
            painter.drawLine(iconRect.right() - 6, iconRect.top() + 2, iconRect.right() - 2, iconRect.top() + 2);
            painter.drawLine(iconRect.right() - 4, iconRect.top(), iconRect.right() - 2, iconRect.top() + 2);
            break;
        }
        }
    }

private:
    QuickActionKind kind_;
};

class LayerLabeledAction : public QWidget {
public:
    LayerLabeledAction(
        QuickActionKind kind,
        const QString& label,
        const QString& tooltip,
        QWidget* parent = nullptr)
        : QWidget(parent)
    {
        setFixedHeight(kQuickRowHeight);
        setToolTip(tooltip);
        setStyleSheet(QStringLiteral(
            "LayerLabeledAction:hover { background-color: #3e3e42; border-radius: 2px; }"));

        auto* layout = new QHBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(2);

        button_ = new LayerQuickButton(kind, tooltip, this);
        auto* text = new QLabel(label, this);
        text->setStyleSheet(QStringLiteral("color: #f0f0f0; font-size: 11px; background: transparent;"));
        text->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        text->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        text->setAttribute(Qt::WA_TransparentForMouseEvents, true);

        layout->addWidget(button_, 0, Qt::AlignVCenter);
        layout->addWidget(text, 0, Qt::AlignVCenter);
    }

    LayerQuickButton* button() const { return button_; }

private:
    LayerQuickButton* button_ = nullptr;
};

QToolButton* MakeQuickButton(QuickActionKind kind, const QString& tooltip, QWidget* parent)
{
    return new LayerQuickButton(kind, tooltip, parent);
}

QWidget* MakeIconCell(QToolButton* button)
{
    auto* cell = new QWidget(button->parentWidget());
    cell->setFixedSize(kQuickIconWidth, kQuickRowHeight);
    auto* layout = new QHBoxLayout(cell);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(button, 0, Qt::AlignCenter);
    return cell;
}

QWidget* MakeLabeledCell(LayerLabeledAction* action)
{
    action->setFixedHeight(kQuickRowHeight);
    action->setMinimumWidth(kLabeledMinWidth);
    return action;
}

void AddActionRow(
    QHBoxLayout* row,
    const std::vector<QWidget*>& cells)
{
    row->setSpacing(kQuickIconGap);
    row->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    for (QWidget* cell : cells) {
        row->addWidget(cell, 0, Qt::AlignLeft | Qt::AlignVCenter);
    }
    row->addStretch(1);
}

std::vector<std::string> UniqueLayersFromSelection(const CadDocument& document)
{
    std::vector<std::string> layers;
    for (const EntityId id : document.SelectedEntityIds()) {
        if (const Entity* entity = document.FindEntity(id)) {
            layers.push_back(entity->LayerName());
        }
    }
    std::sort(layers.begin(), layers.end());
    layers.erase(std::unique(layers.begin(), layers.end()), layers.end());
    return layers;
}

} // namespace

LayerRibbonWidget::LayerRibbonWidget(CadDocument& document, QWidget* parent)
    : QWidget(parent)
    , document_(document)
{
    setObjectName(QStringLiteral("LayerRibbonWidget"));
    setStyleSheet(QStringLiteral("background: transparent;"));

    auto* root = new QHBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(2);

    auto* propsButton = new LayerPropertiesButton(this);
    connect(propsButton, &QToolButton::clicked, this, [this]() {
        if (openLayerManagerCallback_) {
            openLayerManagerCallback_();
        }
    });
    root->addWidget(propsButton);

    auto* rightColumn = new QWidget(this);
    rightColumn->setFixedWidth(kControlColumnWidth);
    auto* rightLayout = new QVBoxLayout(rightColumn);
    rightLayout->setContentsMargins(0, kContentVMargin, 0, kContentVMargin);
    rightLayout->setSpacing(kContentVSpacing);

    controlBar_ = new LayerControlBar(document_, rightColumn);
    controlBar_->setFixedHeight(kDropdownHeight);
    controlBar_->setFixedWidth(kControlColumnWidth);
    rightLayout->addWidget(controlBar_);

    auto* topRow = new QHBoxLayout();
    topRow->setContentsMargins(0, 0, 0, 0);
    auto* bottomRow = new QHBoxLayout();
    bottomRow->setContentsMargins(0, 0, 0, 0);

    auto* offButton = MakeQuickButton(QuickActionKind::LayerOff, tr("关闭所选对象的图层"), rightColumn);
    auto* isoButton = MakeQuickButton(QuickActionKind::LayerIsolate, tr("图层隔离"), rightColumn);
    auto* freezeButton = MakeQuickButton(QuickActionKind::LayerFreeze, tr("冻结所选对象的图层"), rightColumn);
    auto* lockButton = MakeQuickButton(QuickActionKind::LayerLock, tr("锁定所选对象的图层"), rightColumn);
    auto* currentAction = new LayerLabeledAction(
        QuickActionKind::LayerSetCurrent, QStringLiteral("置为当前"), tr("将所选对象所在图层置为当前图层"), rightColumn);

    auto* onAllButton = MakeQuickButton(QuickActionKind::LayerOnAll, tr("打开所有图层"), rightColumn);
    auto* unisoButton = MakeQuickButton(QuickActionKind::LayerUnisolate, tr("取消图层隔离"), rightColumn);
    auto* thawButton = MakeQuickButton(QuickActionKind::LayerThaw, tr("解冻所有图层"), rightColumn);
    auto* unlockButton = MakeQuickButton(QuickActionKind::LayerUnlock, tr("解锁所有图层"), rightColumn);
    auto* matchAction = new LayerLabeledAction(
        QuickActionKind::LayerMatch, QStringLiteral("匹配图层"), tr("将选中对象匹配到源对象图层"), rightColumn);

    AddActionRow(topRow, {
        MakeIconCell(offButton),
        MakeIconCell(isoButton),
        MakeIconCell(freezeButton),
        MakeIconCell(lockButton),
        MakeLabeledCell(currentAction),
    });
    AddActionRow(bottomRow, {
        MakeIconCell(onAllButton),
        MakeIconCell(unisoButton),
        MakeIconCell(thawButton),
        MakeIconCell(unlockButton),
        MakeLabeledCell(matchAction),
    });

    rightLayout->addLayout(topRow);
    rightLayout->addLayout(bottomRow);
    root->addWidget(rightColumn);

    connect(offButton, &QToolButton::clicked, this, [this]() { TurnOffSelectedLayers(); });
    connect(isoButton, &QToolButton::clicked, this, [this]() { IsolateSelectedLayers(); });
    connect(freezeButton, &QToolButton::clicked, this, [this]() { FreezeSelectedLayers(); });
    connect(lockButton, &QToolButton::clicked, this, [this]() { LockSelectedLayers(); });
    connect(currentAction->button(), &QToolButton::clicked, this, [this]() { SetCurrentFromSelection(); });
    connect(onAllButton, &QToolButton::clicked, this, [this]() { TurnOnAllLayers(); });
    connect(unisoButton, &QToolButton::clicked, this, [this]() { RestoreIsolation(); });
    connect(thawButton, &QToolButton::clicked, this, [this]() { ThawAllLayers(); });
    connect(unlockButton, &QToolButton::clicked, this, [this]() { UnlockAllLayers(); });
    connect(matchAction->button(), &QToolButton::clicked, this, [this]() { MatchSelectedLayers(); });

    setFixedHeight(kRibbonHeight);
    setMinimumWidth(kPropsButtonWidth + kControlColumnWidth + 4);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
}

QSize LayerRibbonWidget::sizeHint() const
{
    return QSize(kPropsButtonWidth + kControlColumnWidth + 4, kRibbonHeight);
}

QSize LayerRibbonWidget::minimumSizeHint() const
{
    return sizeHint();
}

void LayerRibbonWidget::SetStatusCallback(std::function<void(const QString&)> callback)
{
    statusCallback_ = std::move(callback);
}

void LayerRibbonWidget::SetOpenLayerManagerCallback(std::function<void()> callback)
{
    openLayerManagerCallback_ = std::move(callback);
}

void LayerRibbonWidget::NotifyStatus(const QString& text) const
{
    if (statusCallback_) {
        statusCallback_(text);
    }
}

void LayerRibbonWidget::IsolateSelectedLayers()
{
    const std::vector<EntityId> selected = document_.SelectedEntityIds();
    if (selected.empty()) {
        NotifyStatus(tr("图层隔离：请先选择对象"));
        return;
    }
    if (!document_.BeginLayerIsolation()) {
        NotifyStatus(tr("图层隔离失败"));
        return;
    }
    const std::vector<std::string> activeLayers = UniqueLayersFromSelection(document_);
    for (Layer& layer : document_.Layers()) {
        layer.SetOn(std::find(activeLayers.begin(), activeLayers.end(), layer.Name()) != activeLayers.end());
    }
    document_.NotifyAllEntitiesChanged();
    NotifyStatus(tr("已隔离选中对象所在图层"));
}

void LayerRibbonWidget::RestoreIsolation()
{
    if (!document_.RestoreLayerIsolation()) {
        NotifyStatus(tr("没有可恢复的图层隔离状态"));
        return;
    }
    NotifyStatus(tr("已取消图层隔离"));
}

void LayerRibbonWidget::TurnOffSelectedLayers()
{
    const std::vector<std::string> layers = UniqueLayersFromSelection(document_);
    if (layers.empty()) {
        NotifyStatus(tr("请先选择对象"));
        return;
    }
    for (Layer& layer : document_.Layers()) {
        if (std::find(layers.begin(), layers.end(), layer.Name()) != layers.end()) {
            layer.SetOn(false);
        }
    }
    document_.NotifyAllEntitiesChanged();
    NotifyStatus(tr("已关闭所选对象的图层"));
}

void LayerRibbonWidget::FreezeSelectedLayers()
{
    const std::vector<std::string> layers = UniqueLayersFromSelection(document_);
    if (layers.empty()) {
        NotifyStatus(tr("请先选择对象"));
        return;
    }
    for (Layer& layer : document_.Layers()) {
        if (std::find(layers.begin(), layers.end(), layer.Name()) != layers.end()
            && layer.Name() != document_.CurrentLayerName()) {
            layer.SetFrozen(true);
        }
    }
    document_.NotifyAllEntitiesChanged();
    NotifyStatus(tr("已冻结所选对象的图层"));
}

void LayerRibbonWidget::LockSelectedLayers()
{
    const std::vector<std::string> layers = UniqueLayersFromSelection(document_);
    if (layers.empty()) {
        NotifyStatus(tr("请先选择对象"));
        return;
    }
    for (Layer& layer : document_.Layers()) {
        if (std::find(layers.begin(), layers.end(), layer.Name()) != layers.end()) {
            layer.SetLocked(true);
        }
    }
    document_.NotifyAllEntitiesChanged();
    NotifyStatus(tr("已锁定所选对象的图层"));
}

void LayerRibbonWidget::SetCurrentFromSelection()
{
    const std::vector<EntityId> selected = document_.SelectedEntityIds();
    if (selected.empty()) {
        NotifyStatus(tr("请先选择对象"));
        return;
    }
    if (const Entity* entity = document_.FindEntity(selected.front())) {
        if (document_.SetCurrentLayerName(entity->LayerName())) {
            NotifyStatus(tr("已将 %1 置为当前图层").arg(QString::fromUtf8(entity->LayerName().c_str())));
            return;
        }
    }
    NotifyStatus(tr("不能将冻结图层设为当前图层"));
}

void LayerRibbonWidget::TurnOnAllLayers()
{
    for (Layer& layer : document_.Layers()) {
        layer.SetOn(true);
    }
    document_.NotifyAllEntitiesChanged();
    NotifyStatus(tr("已打开所有图层"));
}

void LayerRibbonWidget::ThawAllLayers()
{
    for (Layer& layer : document_.Layers()) {
        layer.SetFrozen(false);
    }
    document_.NotifyAllEntitiesChanged();
    NotifyStatus(tr("已解冻所有图层"));
}

void LayerRibbonWidget::UnlockAllLayers()
{
    for (Layer& layer : document_.Layers()) {
        layer.SetLocked(false);
    }
    document_.NotifyAllEntitiesChanged();
    NotifyStatus(tr("已解锁所有图层"));
}

void LayerRibbonWidget::MatchSelectedLayers()
{
    const std::vector<EntityId> selected = document_.SelectedEntityIds();
    if (selected.size() < 2) {
        NotifyStatus(tr("匹配图层：请至少选择两个对象"));
        return;
    }
    if (const Entity* source = document_.FindEntity(selected.front())) {
        const std::string layerName = source->LayerName();
        std::vector<EntityId> targets(selected.begin() + 1, selected.end());
        if (document_.SetEntitiesLayer(targets, layerName)) {
            NotifyStatus(tr("已将选中对象匹配到图层 %1").arg(QString::fromUtf8(layerName.c_str())));
            return;
        }
    }
    NotifyStatus(tr("匹配图层失败"));
}

} // namespace cad
