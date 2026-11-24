#include "Ui/LayerControlBar.h"

#include "DataModel/Entity.h"
#include "View/LayerColorPickerDialog.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QListWidget>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QStyledItemDelegate>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidgetAction>
#include <algorithm>

namespace cad {

namespace {

constexpr int kIconSize = 14;
constexpr int kColorSize = 11;

QColor ColorFromRgb(const std::uint32_t color)
{
    return QColor(
        static_cast<int>((color >> 16) & 0xFF),
        static_cast<int>((color >> 8) & 0xFF),
        static_cast<int>(color & 0xFF));
}

class LayerGlyphButton : public QToolButton {
public:
    enum class Kind { On, Freeze, Lock, Color, Dropdown };

    explicit LayerGlyphButton(Kind kind, QWidget* parent = nullptr)
        : QToolButton(parent)
        , kind_(kind)
    {
        setAutoRaise(true);
        setCursor(Qt::PointingHandCursor);
        if (kind == Kind::Color) {
            setFixedSize(kColorSize + 8, kColorSize + 6);
        } else if (kind == Kind::Dropdown) {
            setFixedSize(12, 16);
        } else {
            setFixedSize(18, 18);
        }
    }

    void SetActive(bool active) { active_ = active; update(); }
    void SetLayerColor(const QColor& color)
    {
        layerColor_ = color;
        update();
    }

protected:
    void paintEvent(QPaintEvent* event) override
    {
        Q_UNUSED(event);
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);

        const QColor activeColor(255, 196, 0);
        const QColor inactiveColor(110, 110, 110);
        const QColor penColor = active_ ? activeColor : inactiveColor;

        if (kind_ == Kind::Color) {
            const QRect swatchRect((width() - kColorSize) / 2, (height() - kColorSize) / 2, kColorSize, kColorSize);
            painter.fillRect(swatchRect, layerColor_);
            painter.setPen(QColor(220, 220, 220));
            painter.drawRect(swatchRect);
            return;
        }

        if (kind_ == Kind::Dropdown) {
            painter.setPen(QColor(180, 180, 180));
            const int cx = width() / 2;
            const int cy = height() / 2 + 1;
            QPainterPath arrow;
            arrow.moveTo(cx - 3, cy - 1);
            arrow.lineTo(cx + 3, cy - 1);
            arrow.lineTo(cx, cy + 2);
            arrow.closeSubpath();
            painter.setBrush(QColor(180, 180, 180));
            painter.drawPath(arrow);
            return;
        }

        painter.setPen(Qt::NoPen);
        painter.setBrush(penColor);

        const QPoint center(width() / 2, height() / 2);
        if (kind_ == Kind::On) {
            painter.drawEllipse(QPoint(center.x(), center.y() + 2), 3, 4);
            painter.drawRect(center.x() - 1, center.y() - 5, 2, 4);
            QPainterPath bulb;
            bulb.moveTo(center.x() - 4, center.y() - 1);
            bulb.quadTo(center.x(), center.y() - 8, center.x() + 4, center.y() - 1);
            painter.setBrush(Qt::NoBrush);
            painter.setPen(QPen(penColor, 1.2));
            painter.drawPath(bulb);
            if (!active_) {
                painter.setPen(QPen(inactiveColor, 1.1));
                painter.drawLine(center.x() - 4, center.y() + 3, center.x() + 4, center.y() - 3);
            }
            return;
        }

        if (kind_ == Kind::Freeze) {
            painter.setPen(QPen(penColor, 1.2));
            painter.setBrush(active_ ? QColor(255, 196, 0, 60) : Qt::NoBrush);
            painter.drawEllipse(center, 5, 5);
            for (int i = 0; i < 8; ++i) {
                const double angle = i * 3.14159265358979323846 / 4.0;
                const QPointF from(
                    center.x() + std::cos(angle) * 5.5,
                    center.y() + std::sin(angle) * 5.5);
                const QPointF to(
                    center.x() + std::cos(angle) * 7.0,
                    center.y() + std::sin(angle) * 7.0);
                painter.drawLine(from, to);
            }
            if (!active_) {
                painter.setPen(QPen(inactiveColor, 1.1));
                painter.drawLine(center.x() - 5, center.y() + 5, center.x() + 5, center.y() - 5);
            }
            return;
        }

        painter.setPen(QPen(penColor, 1.2));
        painter.setBrush(active_ ? QColor(255, 196, 0, 40) : Qt::NoBrush);
        painter.drawRoundedRect(center.x() - 4, center.y(), 8, 6, 1, 1);
        painter.drawArc(center.x() - 4, center.y() - 6, 8, 8, 0, 180 * 16);
        if (!active_) {
            painter.drawLine(center.x() - 3, center.y() + 1, center.x() + 3, center.y() + 5);
            painter.drawLine(center.x() + 3, center.y() + 1, center.x() - 3, center.y() + 5);
        }
    }

private:
    Kind kind_;
    bool active_ = true;
    QColor layerColor_ = Qt::white;
};

class LayerSelectorDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        QStyleOptionViewItem opt = option;
        initStyleOption(&opt, index);

        painter->save();
        if (opt.state & QStyle::State_Selected) {
            painter->fillRect(opt.rect, QColor(9, 71, 113));
        } else {
            painter->fillRect(opt.rect, QColor(45, 45, 48));
        }

        const bool isCurrent = index.data(Qt::UserRole + 1).toBool();
        const std::uint32_t rgb = static_cast<std::uint32_t>(index.data(Qt::UserRole + 2).toULongLong() & 0xFFFFFFU);
        const QString name = index.data(Qt::DisplayRole).toString();
        const QColor textColor = (opt.state & QStyle::State_Selected) ? QColor(255, 255, 255) : QColor(230, 230, 230);

        const QRect swatchRect(opt.rect.left() + 8, opt.rect.center().y() - 7, 14, 14);
        painter->fillRect(swatchRect, ColorFromRgb(rgb));
        painter->setPen(QColor(180, 180, 180));
        painter->drawRect(swatchRect);

        if (isCurrent) {
            painter->setPen(QColor(255, 196, 0));
            painter->drawText(QRect(opt.rect.left() + 26, opt.rect.top(), 18, opt.rect.height()), Qt::AlignCenter, QStringLiteral("✓"));
        }

        painter->setPen(textColor);
        painter->drawText(opt.rect.adjusted(44, 0, -8, 0), Qt::AlignLeft | Qt::AlignVCenter, name);
        painter->restore();
    }

    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        Q_UNUSED(index);
        QSize size = QStyledItemDelegate::sizeHint(option, index);
        size.setHeight(std::max(size.height(), 26));
        return size;
    }
};

} // namespace

LayerControlBar::LayerControlBar(CadDocument& document, QWidget* parent)
    : QWidget(parent)
    , document_(document)
{
    setObjectName(QStringLiteral("LayerControlBar"));
    setStyleSheet(QStringLiteral(
        "#LayerControlBar {"
        "  background-color: #1a1a1e;"
        "  border: 1px solid #5e5e64;"
        "  border-radius: 3px;"
        "}"
        "QToolButton { border: none; background: transparent; padding: 0px; }"
        "QToolButton:hover { background-color: #3a3a40; }"));

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(4, 1, 3, 1);
    layout->setSpacing(2);

    onButton_ = new LayerGlyphButton(LayerGlyphButton::Kind::On, this);
    onButton_->setFixedSize(14, 14);
    onButton_->setToolTip(tr("开关图层"));
    freezeButton_ = new LayerGlyphButton(LayerGlyphButton::Kind::Freeze, this);
    freezeButton_->setFixedSize(14, 14);
    freezeButton_->setToolTip(tr("冻结/解冻图层"));
    lockButton_ = new LayerGlyphButton(LayerGlyphButton::Kind::Lock, this);
    lockButton_->setFixedSize(14, 14);
    lockButton_->setToolTip(tr("锁定/解锁图层"));
    colorButton_ = new LayerGlyphButton(LayerGlyphButton::Kind::Color, this);
    colorButton_->setFixedSize(12, 12);
    colorButton_->setToolTip(tr("图层颜色"));
    layerButton_ = new QToolButton(this);
    layerButton_->setToolTip(tr("当前图层"));
    layerButton_->setStyleSheet(QStringLiteral(
        "QToolButton { color: #f0f0f0; font-size: 11px; padding: 0px 2px; border: none; background: transparent; text-align: left; }"
        "QToolButton:hover { background-color: #3e3e42; }"));
    layerButton_->setMinimumWidth(36);
    layerButton_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    dropdownButton_ = new LayerGlyphButton(LayerGlyphButton::Kind::Dropdown, this);
    dropdownButton_->setFixedSize(10, 14);
    dropdownButton_->setToolTip(tr("选择图层"));

    layout->addWidget(onButton_);
    layout->addWidget(freezeButton_);
    layout->addWidget(lockButton_);
    layout->addWidget(colorButton_);
    layout->addWidget(layerButton_, 1);
    layout->addWidget(dropdownButton_);

    connect(onButton_, &QToolButton::clicked, this, [this]() { ToggleOn(); });
    connect(freezeButton_, &QToolButton::clicked, this, [this]() { ToggleFreeze(); });
    connect(lockButton_, &QToolButton::clicked, this, [this]() { ToggleLock(); });
    connect(colorButton_, &QToolButton::clicked, this, [this]() { PickColor(); });
    connect(layerButton_, &QToolButton::clicked, this, [this]() { ShowLayerSelector(); });
    connect(dropdownButton_, &QToolButton::clicked, this, [this]() { ShowLayerSelector(); });

    document_.AddObserver(this);
    RefreshFromDocument();
}

LayerControlBar::~LayerControlBar()
{
    document_.RemoveObserver(this);
}

void LayerControlBar::OnEntityAdded(const Entity& entity)
{
    Q_UNUSED(entity);
    RefreshFromDocument();
}

void LayerControlBar::OnEntityRemoved(EntityId id)
{
    Q_UNUSED(id);
    RefreshFromDocument();
}

void LayerControlBar::OnEntitiesRemoved(const std::vector<EntityId>& ids)
{
    Q_UNUSED(ids);
    RefreshFromDocument();
}

void LayerControlBar::OnEntitiesChanged(const std::vector<EntityId>& ids)
{
    Q_UNUSED(ids);
    RefreshFromDocument();
}

void LayerControlBar::OnSelectionChanged()
{
    RefreshFromDocument();
}

void LayerControlBar::OnDocumentReset()
{
    RefreshFromDocument();
}

Layer* LayerControlBar::CurrentLayer()
{
    return document_.FindLayer(document_.CurrentLayerName());
}

const Layer* LayerControlBar::CurrentLayer() const
{
    return document_.FindLayer(document_.CurrentLayerName());
}

void LayerControlBar::RefreshFromDocument()
{
    const Layer* layer = CurrentLayer();
    const QString name = layer
        ? QString::fromUtf8(layer->Name().c_str())
        : QStringLiteral("0");

    layerButton_->setText(name);

    const bool on = layer ? layer->IsOn() : true;
    const bool frozen = layer ? layer->IsFrozen() : false;
    const bool locked = layer ? layer->IsLocked() : false;
    const QColor color = layer ? ColorFromRgb(layer->Color()) : QColor(255, 255, 255);

    if (auto* button = dynamic_cast<LayerGlyphButton*>(onButton_)) {
        button->SetActive(on);
    }
    if (auto* button = dynamic_cast<LayerGlyphButton*>(freezeButton_)) {
        button->SetActive(!frozen);
    }
    if (auto* button = dynamic_cast<LayerGlyphButton*>(lockButton_)) {
        button->SetActive(!locked);
    }
    if (auto* button = dynamic_cast<LayerGlyphButton*>(colorButton_)) {
        button->SetLayerColor(color);
    }
}

void LayerControlBar::ToggleOn()
{
    Layer* layer = CurrentLayer();
    if (!layer) {
        return;
    }
    layer->SetOn(!layer->IsOn());
    document_.NotifyAllEntitiesChanged();
    RefreshFromDocument();
}

void LayerControlBar::ToggleFreeze()
{
    Layer* layer = CurrentLayer();
    if (!layer) {
        return;
    }
    if (!layer->IsFrozen() && layer->Name() == document_.CurrentLayerName()) {
        return;
    }
    layer->SetFrozen(!layer->IsFrozen());
    document_.NotifyAllEntitiesChanged();
    RefreshFromDocument();
}

void LayerControlBar::ToggleLock()
{
    Layer* layer = CurrentLayer();
    if (!layer) {
        return;
    }
    layer->SetLocked(!layer->IsLocked());
    document_.NotifyAllEntitiesChanged();
    RefreshFromDocument();
}

void LayerControlBar::PickColor()
{
    Layer* layer = CurrentLayer();
    if (!layer) {
        return;
    }
    LayerColorPickerDialog dialog(layer->Color(), this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    layer->SetColor(dialog.selectedRgb());
    document_.NotifyAllEntitiesChanged();
    RefreshFromDocument();
}

void LayerControlBar::ShowLayerSelector()
{
    auto* menu = new QMenu(this);
    menu->setStyleSheet(QStringLiteral(
        "QMenu { background-color: #2d2d30; color: #f3f3f3; border: 1px solid #555555; }"
        "QMenu::item:selected { background-color: #094771; }"));

    auto* container = new QFrame(menu);
    container->setFrameShape(QFrame::NoFrame);
    auto* containerLayout = new QVBoxLayout(container);
    containerLayout->setContentsMargins(0, 0, 0, 0);

    auto* list = new QListWidget(container);
    list->setItemDelegate(new LayerSelectorDelegate(list));
    list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    list->setMinimumWidth(180);
    list->setMaximumHeight(280);
    list->setStyleSheet(QStringLiteral(
        "QListWidget { background-color: #2d2d30; border: none; outline: none; }"));

    const std::string currentName = document_.CurrentLayerName();
    for (const Layer& layer : document_.Layers()) {
        const QString name = QString::fromUtf8(layer.Name().c_str());
        auto* item = new QListWidgetItem(name);
        item->setData(Qt::UserRole, name);
        item->setData(Qt::UserRole + 1, layer.Name() == currentName);
        item->setData(Qt::UserRole + 2, static_cast<qulonglong>(layer.Color() & 0xFFFFFFU));
        list->addItem(item);
    }

    containerLayout->addWidget(list);

    auto* action = new QWidgetAction(menu);
    action->setDefaultWidget(container);
    menu->addAction(action);

    connect(list, &QListWidget::itemClicked, this, [this, menu](QListWidgetItem* item) {
        if (!item) {
            return;
        }
        const std::string layerName = item->data(Qt::UserRole).toString().toStdString();
        if (document_.SetCurrentLayerName(layerName)) {
            RefreshFromDocument();
        }
        menu->close();
    });

    const QPoint globalPos = mapToGlobal(QPoint(0, height()));
    menu->popup(globalPos);
}

} // namespace cad
