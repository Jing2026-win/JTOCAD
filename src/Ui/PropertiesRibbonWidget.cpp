#include "Ui/PropertiesRibbonWidget.h"

#include "View/AcadColorPalette.h"
#include "View/LayerColorPickerDialog.h"
#include "View/LineTypeManagerDialog.h"
#include "View/LayerLineWeightPickerDialog.h"
#include "View/LineWeightUtil.h"

#include <QAction>
#include <QColor>
#include <QConicalGradient>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QSizePolicy>
#include <QToolButton>
#include <QVBoxLayout>

namespace cad {

namespace {

constexpr int kRibbonHeight = 66;
constexpr int kMatchButtonWidth = 50;
constexpr int kRowHeight = 18;
constexpr int kDropdownWidth = 128;
constexpr int kIconColWidth = 18;
constexpr int kContentVMargin = 2;
constexpr int kContentVSpacing = 2;

class MatchPropertiesButton : public QToolButton {
public:
    explicit MatchPropertiesButton(QWidget* parent = nullptr)
        : QToolButton(parent)
    {
        setToolTip(QObject::tr("特性匹配 (MA)：复制源对象属性到目标对象"));
        setAutoRaise(true);
        setFixedSize(kMatchButtonWidth, kRibbonHeight);
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

        // Monitor / properties panel icon
        const QRect monitor(8, 6, 28, 20);
        painter.setPen(QPen(QColor(70, 150, 230), 1.2));
        painter.setBrush(QColor(40, 90, 160));
        painter.drawRoundedRect(monitor, 2, 2);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(80, 170, 240));
        painter.drawRect(monitor.adjusted(3, 3, -3, -7));
        // Sliders on panel
        painter.setBrush(QColor(220, 220, 220));
        painter.drawRect(monitor.left() + 5, monitor.bottom() - 5, 8, 2);
        painter.drawRect(monitor.left() + 15, monitor.bottom() - 5, 6, 2);

        // Paintbrush overlay
        QPainterPath brush;
        brush.moveTo(30, 22);
        brush.lineTo(36, 16);
        brush.lineTo(38, 18);
        brush.lineTo(32, 24);
        brush.closeSubpath();
        painter.setPen(QPen(QColor(200, 180, 120), 1.0));
        painter.setBrush(QColor(230, 210, 160));
        painter.drawPath(brush);
        painter.setPen(QPen(QColor(240, 240, 240), 1.4));
        painter.drawLine(36, 16, 42, 10);

        painter.setPen(QColor(236, 236, 236));
        QFont font = painter.font();
        font.setPointSize(9);
        painter.setFont(font);
        painter.drawText(QRect(0, 38, width(), 12), Qt::AlignHCenter | Qt::AlignVCenter, QStringLiteral("特性"));
        painter.drawText(QRect(0, 50, width(), 12), Qt::AlignHCenter | Qt::AlignVCenter, QStringLiteral("匹配"));
    }
};

class RowGlyphButton : public QToolButton {
public:
    enum class Kind { ColorWheel, LineWeight, LineType };

    RowGlyphButton(Kind kind, QWidget* parent = nullptr)
        : QToolButton(parent)
        , kind_(kind)
    {
        setFixedSize(kIconColWidth, kRowHeight);
        setAutoRaise(true);
        setStyleSheet(QStringLiteral(
            "QToolButton { border: none; background: transparent; }"
            "QToolButton:hover { background-color: #3e3e42; }"));
    }

protected:
    void paintEvent(QPaintEvent* event) override
    {
        Q_UNUSED(event);
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        const QPoint c = rect().center();

        if (kind_ == Kind::ColorWheel) {
            for (int i = 0; i < 6; ++i) {
                const double a0 = i * 60.0;
                QConicalGradient g(c, -90);
                g.setColorAt(0.0, QColor(255, 0, 0));
                g.setColorAt(0.16, QColor(255, 255, 0));
                g.setColorAt(0.33, QColor(0, 255, 0));
                g.setColorAt(0.5, QColor(0, 255, 255));
                g.setColorAt(0.66, QColor(0, 0, 255));
                g.setColorAt(0.83, QColor(255, 0, 255));
                g.setColorAt(1.0, QColor(255, 0, 0));
                Q_UNUSED(a0);
                painter.setBrush(g);
                painter.setPen(QPen(QColor(80, 80, 80), 0.8));
                painter.drawEllipse(c, 7, 7);
                painter.setBrush(QColor(45, 45, 48));
                painter.setPen(Qt::NoPen);
                painter.drawEllipse(c, 3, 3);
                break;
            }
            return;
        }

        if (kind_ == Kind::LineWeight) {
            painter.setPen(QPen(QColor(190, 190, 190), 1.0));
            painter.drawLine(3, 4, width() - 3, 4);
            painter.setPen(QPen(QColor(190, 190, 190), 1.6));
            painter.drawLine(3, 8, width() - 3, 8);
            painter.setPen(QPen(QColor(190, 190, 190), 2.2));
            painter.drawLine(3, 12, width() - 3, 12);
            painter.setPen(QPen(QColor(190, 190, 190), 2.8));
            painter.drawLine(3, 16, width() - 3, 16);
            return;
        }

        painter.setPen(QPen(QColor(190, 190, 190), 1.0));
        painter.drawLine(2, 4, 7, 4);
        painter.drawLine(10, 4, 15, 4);
        painter.drawLine(2, 9, 5, 9);
        painter.drawPoint(8, 9);
        painter.drawLine(11, 9, 15, 9);
        painter.drawLine(2, 14, 4, 14);
        painter.drawLine(7, 14, 10, 14);
        painter.drawLine(13, 14, 15, 14);
    }

private:
    Kind kind_;
};

QString ColorLabel(const EntityStyle& style)
{
    if (style.colorSource == PropertySource::ByLayer) {
        return QStringLiteral("ByLayer");
    }
    if (style.colorSource == PropertySource::ByBlock) {
        return QStringLiteral("ByBlock");
    }
    const std::uint32_t rgb =
        (static_cast<std::uint32_t>(style.red) << 16)
        | (static_cast<std::uint32_t>(style.green) << 8)
        | static_cast<std::uint32_t>(style.blue);
    return AcadColorLabel(RgbToAcadColorIndex(rgb), rgb);
}

QString LineWeightLabel(const EntityStyle& style)
{
    if (style.lineWeightSource == PropertySource::ByLayer) {
        return QStringLiteral("ByLayer");
    }
    if (style.lineWeightSource == PropertySource::ByBlock) {
        return QStringLiteral("ByBlock");
    }
    return LineWeightDialogLabel(style.lineWidth);
}

QString LineTypeLabel(const EntityStyle& style)
{
    if (style.lineTypeSource == PropertySource::ByLayer) {
        return QStringLiteral("ByLayer");
    }
    if (style.lineTypeSource == PropertySource::ByBlock) {
        return QStringLiteral("ByBlock");
    }
    if (!style.lineTypeName.empty()) {
        return QString::fromUtf8(style.lineTypeName.c_str());
    }
    return QString::fromUtf8(LineTypeNameFromPattern(style.linePattern).c_str());
}

EntityStyle ResolveDisplayStyle(
    const EntityStyle& stored,
    const CadDocument& document,
    const std::string& layerName)
{
    EntityStyle style = stored;
    const Layer* layer = document.FindLayer(layerName);

    if (style.colorSource == PropertySource::ByLayer && layer) {
        style.red = static_cast<std::uint8_t>((layer->Color() >> 16) & 0xFF);
        style.green = static_cast<std::uint8_t>((layer->Color() >> 8) & 0xFF);
        style.blue = static_cast<std::uint8_t>(layer->Color() & 0xFF);
    } else if (style.colorSource == PropertySource::ByBlock) {
        style.red = 255;
        style.green = 255;
        style.blue = 255;
    }

    if (style.lineTypeSource == PropertySource::ByLayer && layer) {
        style.lineTypeName = layer->LineTypeName();
        style.linePattern = LinePatternFromLineTypeName(layer->LineTypeName());
    } else if (style.lineTypeSource == PropertySource::ByBlock) {
        style.lineTypeName = "Continuous";
        style.linePattern = LinePattern::Continuous;
    } else if (style.lineTypeName.empty()) {
        style.lineTypeName = LineTypeNameFromPattern(style.linePattern);
    }

    if (style.lineWeightSource == PropertySource::ByLayer && layer) {
        style.lineWidth = EffectiveLineWeightMm(layer->LineWeightMm());
    } else if (style.lineWeightSource == PropertySource::ByBlock) {
        style.lineWidth = 0.0;
    }
    return style;
}

QIcon MakeColorIcon(const EntityStyle& effective)
{
    QPixmap px(12, 12);
    const QColor color(effective.red, effective.green, effective.blue);
    px.fill(color);
    QPainter p(&px);
    p.setPen(QColor(180, 180, 180));
    p.drawRect(0, 0, 11, 11);
    return QIcon(px);
}

QIcon MakeLineWeightIcon(const EntityStyle& effective)
{
    QPixmap px(28, 12);
    px.fill(Qt::transparent);
    QPainter p(&px);
    p.setRenderHint(QPainter::Antialiasing, true);
    const qreal penW = LineWeightPreviewPenWidth(
        effective.lineWidth <= 0.0 ? 0.25 : effective.lineWidth);
    p.setPen(QPen(QColor(230, 230, 230), penW, Qt::SolidLine, Qt::RoundCap));
    p.drawLine(2, 6, 26, 6);
    return QIcon(px);
}

QIcon MakeLineTypeIcon(const EntityStyle& effective)
{
    QPixmap px(28, 12);
    px.fill(Qt::transparent);
    QPainter p(&px);
    p.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(QColor(230, 230, 230), 1.4, Qt::SolidLine, Qt::FlatCap);
    switch (effective.linePattern) {
    case LinePattern::Dashed:
        pen.setStyle(Qt::DashLine);
        break;
    case LinePattern::Center:
        pen.setStyle(Qt::DashDotLine);
        break;
    case LinePattern::Hidden:
        pen.setStyle(Qt::DotLine);
        break;
    case LinePattern::Continuous:
    default:
        pen.setStyle(Qt::SolidLine);
        break;
    }
    p.setPen(pen);
    p.drawLine(2, 6, 26, 6);
    return QIcon(px);
}

} // namespace

PropertiesRibbonWidget::PropertiesRibbonWidget(
    CadDocument& document,
    CommandManager& commandManager,
    QWidget* parent)
    : QWidget(parent)
    , document_(document)
    , commandManager_(commandManager)
{
    setObjectName(QStringLiteral("PropertiesRibbonWidget"));
    setStyleSheet(QStringLiteral("background: transparent;"));

    auto* root = new QHBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(2);

    auto* matchButton = new MatchPropertiesButton(this);
    connect(matchButton, &QToolButton::clicked, this, [this]() { StartMatchProperties(); });
    root->addWidget(matchButton);

    auto* right = new QWidget(this);
    auto* rightLayout = new QVBoxLayout(right);
    rightLayout->setContentsMargins(0, kContentVMargin, 0, kContentVMargin);
    rightLayout->setSpacing(kContentVSpacing);

    const auto makeRow = [&](RowGlyphButton::Kind kind, QToolButton*& outButton, const QString& tip) {
        auto* row = new QWidget(right);
        row->setFixedHeight(kRowHeight);
        auto* layout = new QHBoxLayout(row);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(3);

        auto* glyph = new RowGlyphButton(kind, row);
        glyph->setToolTip(tip);
        outButton = new QToolButton(row);
        outButton->setToolTip(tip);
        outButton->setFixedSize(kDropdownWidth, kRowHeight);
        outButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        outButton->setIconSize(QSize(14, 12));
        outButton->setStyleSheet(QStringLiteral(
            "QToolButton {"
            "  color: #f0f0f0; font-size: 11px; text-align: left;"
            "  background-color: #1a1a1e; border: 1px solid #5e5e64; border-radius: 2px;"
            "  padding-left: 4px; padding-right: 12px;"
            "}"
            "QToolButton:hover { background-color: #2a2a2e; }"
            "QToolButton::menu-indicator { image: none; width: 0px; }"));

        layout->addWidget(glyph);
        layout->addWidget(outButton);
        rightLayout->addWidget(row);
    };

    makeRow(RowGlyphButton::Kind::ColorWheel, colorButton_, tr("颜色：设置当前/选中对象颜色"));
    makeRow(RowGlyphButton::Kind::LineWeight, lineWeightButton_, tr("线宽：设置当前/选中对象线宽"));
    makeRow(RowGlyphButton::Kind::LineType, lineTypeButton_, tr("线型：设置当前/选中对象线型"));

    connect(colorButton_, &QToolButton::clicked, this, [this]() { ShowColorMenu(); });
    connect(lineWeightButton_, &QToolButton::clicked, this, [this]() { ShowLineWeightMenu(); });
    connect(lineTypeButton_, &QToolButton::clicked, this, [this]() { ShowLineTypeMenu(); });

    root->addWidget(right);

    setFixedHeight(kRibbonHeight);
    setMinimumWidth(kMatchButtonWidth + kIconColWidth + kDropdownWidth + 10);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

    document_.AddObserver(this);
    RefreshDisplay();
}

PropertiesRibbonWidget::~PropertiesRibbonWidget()
{
    document_.RemoveObserver(this);
}

QSize PropertiesRibbonWidget::sizeHint() const
{
    return QSize(kMatchButtonWidth + kIconColWidth + kDropdownWidth + 10, kRibbonHeight);
}

QSize PropertiesRibbonWidget::minimumSizeHint() const
{
    return sizeHint();
}

void PropertiesRibbonWidget::SetStatusCallback(std::function<void(const QString&)> callback)
{
    statusCallback_ = std::move(callback);
}

void PropertiesRibbonWidget::SetOpenPropertiesPaletteCallback(std::function<void()> callback)
{
    openPropertiesPaletteCallback_ = std::move(callback);
}

void PropertiesRibbonWidget::OpenPropertiesPalette()
{
    if (openPropertiesPaletteCallback_) {
        openPropertiesPaletteCallback_();
    }
}

void PropertiesRibbonWidget::CancelMatchProperties()
{
    matchPhase_ = MatchPhase::Idle;
    matchSourceId_.reset();
}

void PropertiesRibbonWidget::NotifyStatus(const QString& text) const
{
    if (statusCallback_) {
        statusCallback_(text);
    }
}

void PropertiesRibbonWidget::OnEntityAdded(const Entity& entity)
{
    Q_UNUSED(entity);
    RefreshDisplay();
}

void PropertiesRibbonWidget::OnEntityRemoved(EntityId id)
{
    if (matchSourceId_ && *matchSourceId_ == id) {
        CancelMatchProperties();
    }
    RefreshDisplay();
}

void PropertiesRibbonWidget::OnEntitiesRemoved(const std::vector<EntityId>& ids)
{
    Q_UNUSED(ids);
    RefreshDisplay();
}

void PropertiesRibbonWidget::OnEntitiesChanged(const std::vector<EntityId>& ids)
{
    Q_UNUSED(ids);
    RefreshDisplay();
}

void PropertiesRibbonWidget::OnSelectionChanged()
{
    if (matchPhase_ == MatchPhase::WaitingSource) {
        const std::vector<EntityId> selected = document_.SelectedEntityIds();
        if (!selected.empty()) {
            matchSourceId_ = selected.front();
            matchPhase_ = MatchPhase::WaitingTargets;
            NotifyStatus(tr("特性匹配：已选择源对象，请选择目标对象（可多选）；再次点击「特性匹配」取消"));
        }
    } else if (matchPhase_ == MatchPhase::WaitingTargets && matchSourceId_) {
        const std::vector<EntityId> selected = document_.SelectedEntityIds();
        std::vector<EntityId> targets;
        for (EntityId id : selected) {
            if (id != *matchSourceId_) {
                targets.push_back(id);
            }
        }
        if (!targets.empty()) {
            commandManager_.MatchEntityProperties(*matchSourceId_, targets);
            NotifyStatus(tr("特性匹配：已应用到 %1 个对象，可继续选择目标").arg(targets.size()));
        }
    }
    RefreshDisplay();
}

void PropertiesRibbonWidget::OnDocumentReset()
{
    CancelMatchProperties();
    RefreshDisplay();
}

EntityStyle PropertiesRibbonWidget::DisplayStyle() const
{
    const std::vector<EntityId> selected = document_.SelectedEntityIds();
    if (selected.size() == 1) {
        if (const Entity* entity = document_.FindEntity(selected.front())) {
            return entity->Style();
        }
    }
    return document_.CurrentEntityStyle();
}

std::string PropertiesRibbonWidget::DisplayLayerName() const
{
    const std::vector<EntityId> selected = document_.SelectedEntityIds();
    if (selected.size() == 1) {
        if (const Entity* entity = document_.FindEntity(selected.front())) {
            return entity->LayerName();
        }
    }
    return document_.CurrentLayerName();
}

void PropertiesRibbonWidget::RefreshDisplay()
{
    const EntityStyle stored = DisplayStyle();
    const std::string layerName = DisplayLayerName();
    const EntityStyle effective = ResolveDisplayStyle(stored, document_, layerName);
    const bool fromSelection = !document_.SelectedEntityIds().empty();

    colorButton_->setIcon(MakeColorIcon(effective));
    colorButton_->setText(ColorLabel(stored));
    colorButton_->setIconSize(QSize(12, 12));

    lineWeightButton_->setIcon(MakeLineWeightIcon(effective));
    if (fromSelection && stored.lineWeightSource == PropertySource::ByLayer) {
        // 选中对象且属性随层：显示该图层的实际线宽值
        if (const Layer* layer = document_.FindLayer(layerName)) {
            lineWeightButton_->setText(LineWeightDialogLabel(layer->LineWeightMm()));
        } else {
            lineWeightButton_->setText(LineWeightDialogLabel(effective.lineWidth));
        }
    } else {
        lineWeightButton_->setText(LineWeightLabel(stored));
    }
    lineWeightButton_->setIconSize(QSize(28, 12));

    lineTypeButton_->setIcon(MakeLineTypeIcon(effective));
    if (fromSelection && stored.lineTypeSource == PropertySource::ByLayer) {
        // 选中对象且属性随层：显示该图层的实际线型名
        const QString ltName = !effective.lineTypeName.empty()
            ? QString::fromUtf8(effective.lineTypeName.c_str())
            : QString::fromUtf8(LineTypeNameFromPattern(effective.linePattern).c_str());
        lineTypeButton_->setText(ltName);
    } else {
        lineTypeButton_->setText(LineTypeLabel(stored));
    }
    lineTypeButton_->setIconSize(QSize(28, 12));
}

void PropertiesRibbonWidget::ApplyStyleMutation(const std::function<void(EntityStyle&)>& mutator)
{
    const std::vector<EntityId> selected = document_.EditableEntityIds(document_.SelectedEntityIds());
    if (selected.empty()) {
        EntityStyle style = document_.CurrentEntityStyle();
        mutator(style);
        document_.SetCurrentEntityStyle(style);
        NotifyStatus(tr("已设置当前绘图默认属性"));
        RefreshDisplay();
        return;
    }

    commandManager_.MutateEntityStyles(selected, mutator);
    NotifyStatus(tr("已修改选中对象属性"));
    RefreshDisplay();
}

void PropertiesRibbonWidget::StartMatchProperties()
{
    if (matchPhase_ != MatchPhase::Idle) {
        CancelMatchProperties();
        NotifyStatus(tr("已取消特性匹配"));
        return;
    }

    const std::vector<EntityId> selected = document_.SelectedEntityIds();
    if (selected.size() >= 2) {
        commandManager_.MatchEntityProperties(selected.front(), selected);
        NotifyStatus(tr("特性匹配：已将首个对象属性应用到其余选中对象"));
        return;
    }

    matchPhase_ = MatchPhase::WaitingSource;
    matchSourceId_.reset();
    if (selected.size() == 1) {
        matchSourceId_ = selected.front();
        matchPhase_ = MatchPhase::WaitingTargets;
        NotifyStatus(tr("特性匹配：已选择源对象，请选择目标对象"));
    } else {
        NotifyStatus(tr("特性匹配：请选择源对象"));
    }
}

void PropertiesRibbonWidget::ShowColorMenu()
{
    QMenu menu(this);
    menu.setStyleSheet(QStringLiteral(
        "QMenu { background-color: #2d2d30; color: #f3f3f3; border: 1px solid #555555; }"
        "QMenu::item:selected { background-color: #094771; }"));

    auto* byLayer = menu.addAction(QStringLiteral("ByLayer"));
    auto* byBlock = menu.addAction(QStringLiteral("ByBlock"));
    menu.addSeparator();

    const int standardColors[] = {1, 2, 3, 4, 5, 6, 7};
    for (int index : standardColors) {
        const std::uint32_t rgb = AcadColorIndexToRgb(index);
        QPixmap px(12, 12);
        px.fill(QColor(
            static_cast<int>((rgb >> 16) & 0xFF),
            static_cast<int>((rgb >> 8) & 0xFF),
            static_cast<int>(rgb & 0xFF)));
        auto* action = menu.addAction(QIcon(px), AcadColorLabel(index, rgb));
        action->setData(index);
    }
    menu.addSeparator();
    auto* more = menu.addAction(tr("选择颜色..."));

    QAction* chosen = menu.exec(colorButton_->mapToGlobal(QPoint(0, colorButton_->height())));
    if (!chosen) {
        return;
    }
    if (chosen == byLayer) {
        ApplyStyleMutation([](EntityStyle& style) {
            style.colorSource = PropertySource::ByLayer;
        });
        return;
    }
    if (chosen == byBlock) {
        ApplyStyleMutation([](EntityStyle& style) {
            style.colorSource = PropertySource::ByBlock;
            style.red = 255;
            style.green = 255;
            style.blue = 255;
        });
        return;
    }
    if (chosen == more) {
        const EntityStyle current = DisplayStyle();
        const std::uint32_t initial =
            (static_cast<std::uint32_t>(current.red) << 16)
            | (static_cast<std::uint32_t>(current.green) << 8)
            | static_cast<std::uint32_t>(current.blue);
        LayerColorPickerDialog dialog(initial, this);
        if (dialog.exec() != QDialog::Accepted) {
            return;
        }
        const std::uint32_t rgb = dialog.selectedRgb();
        ApplyStyleMutation([rgb](EntityStyle& style) {
            style.colorSource = PropertySource::Override;
            style.red = static_cast<std::uint8_t>((rgb >> 16) & 0xFF);
            style.green = static_cast<std::uint8_t>((rgb >> 8) & 0xFF);
            style.blue = static_cast<std::uint8_t>(rgb & 0xFF);
        });
        return;
    }

    const int index = chosen->data().toInt();
    const std::uint32_t rgb = AcadColorIndexToRgb(index);
    ApplyStyleMutation([rgb](EntityStyle& style) {
        style.colorSource = PropertySource::Override;
        style.red = static_cast<std::uint8_t>((rgb >> 16) & 0xFF);
        style.green = static_cast<std::uint8_t>((rgb >> 8) & 0xFF);
        style.blue = static_cast<std::uint8_t>(rgb & 0xFF);
    });
}

void PropertiesRibbonWidget::ShowLineWeightMenu()
{
    QMenu menu(this);
    menu.setStyleSheet(QStringLiteral(
        "QMenu { background-color: #2d2d30; color: #f3f3f3; border: 1px solid #555555; padding: 2px; }"
        "QMenu::item { padding: 4px 20px 4px 8px; }"
        "QMenu::item:selected { background-color: #094771; }"
        "QMenu::separator { height: 1px; background: #555555; margin: 4px 6px; }"));
    menu.setMinimumWidth(220);

    auto makeWeightIcon = [](double mm) {
        EntityStyle preview;
        preview.lineWeightSource = PropertySource::Override;
        preview.lineWidth = IsDefaultLineWeight(mm) ? 0.25 : mm;
        return MakeLineWeightIcon(preview);
    };

    auto* byLayer = menu.addAction(makeWeightIcon(0.25), QStringLiteral("ByLayer"));
    auto* byBlock = menu.addAction(makeWeightIcon(0.25), QStringLiteral("ByBlock"));
    menu.addSeparator();

    for (const double mm : StandardLineWeightsMm()) {
        if (IsDefaultLineWeight(mm)) {
            continue; // 下拉列表只显示具体毫米值，默认仍可通过「线宽设置」选
        }
        auto* action = menu.addAction(makeWeightIcon(mm), LineWeightMenuLabel(mm));
        action->setData(mm);
    }
    menu.addSeparator();
    auto* more = menu.addAction(tr("线宽设置..."));

    QAction* chosen = menu.exec(lineWeightButton_->mapToGlobal(QPoint(0, lineWeightButton_->height())));
    if (!chosen) {
        return;
    }
    if (chosen == byLayer) {
        ApplyStyleMutation([](EntityStyle& style) {
            style.lineWeightSource = PropertySource::ByLayer;
        });
        return;
    }
    if (chosen == byBlock) {
        ApplyStyleMutation([](EntityStyle& style) {
            style.lineWeightSource = PropertySource::ByBlock;
            style.lineWidth = 0.0;
        });
        return;
    }
    if (chosen == more) {
        const EntityStyle current = DisplayStyle();
        double initial = 0.25;
        if (current.lineWeightSource == PropertySource::Override) {
            initial = current.lineWidth;
        } else if (const Layer* layer = document_.FindLayer(DisplayLayerName())) {
            initial = layer->LineWeightMm();
        }
        LayerLineWeightPickerDialog dialog(initial, this);
        if (dialog.exec() != QDialog::Accepted) {
            return;
        }
        const double mm = dialog.selectedLineWeightMm();
        ApplyStyleMutation([mm](EntityStyle& style) {
            style.lineWeightSource = PropertySource::Override;
            style.lineWidth = IsDefaultLineWeight(mm) ? 0.0 : mm;
        });
        return;
    }

    const double mm = chosen->data().toDouble();
    ApplyStyleMutation([mm](EntityStyle& style) {
        style.lineWeightSource = PropertySource::Override;
        style.lineWidth = mm;
    });
}

void PropertiesRibbonWidget::ShowLineTypeMenu()
{
    QMenu menu(this);
    menu.setStyleSheet(QStringLiteral(
        "QMenu { background-color: #2d2d30; color: #f3f3f3; border: 1px solid #555555; padding: 2px; }"
        "QMenu::item { padding: 4px 24px 4px 8px; }"
        "QMenu::item:selected { background-color: #094771; }"
        "QMenu::separator { height: 1px; background: #555555; margin: 4px 6px; }"));
    menu.setMinimumWidth(180);

    auto makeTypeIcon = [](const std::string& name) {
        EntityStyle preview;
        preview.lineTypeSource = PropertySource::Override;
        preview.lineTypeName = name;
        preview.linePattern = LinePatternFromLineTypeName(name);
        return MakeLineTypeIcon(preview);
    };

    auto* byLayer = menu.addAction(makeTypeIcon("Continuous"), QStringLiteral("ByLayer"));
    auto* byBlock = menu.addAction(makeTypeIcon("Continuous"), QStringLiteral("ByBlock"));
    menu.addSeparator();

    for (const LineTypeDefinition& lineType : document_.LineTypes()) {
        const QString name = QString::fromUtf8(lineType.name.c_str());
        auto* action = menu.addAction(makeTypeIcon(lineType.name), name);
        action->setData(name);
    }
    menu.addSeparator();
    auto* more = menu.addAction(tr("其他..."));

    QAction* chosen = menu.exec(lineTypeButton_->mapToGlobal(QPoint(0, lineTypeButton_->height())));
    if (!chosen) {
        return;
    }
    if (chosen == byLayer) {
        ApplyStyleMutation([](EntityStyle& style) {
            style.lineTypeSource = PropertySource::ByLayer;
        });
        return;
    }
    if (chosen == byBlock) {
        ApplyStyleMutation([](EntityStyle& style) {
            style.lineTypeSource = PropertySource::ByBlock;
            style.lineTypeName = "Continuous";
            style.linePattern = LinePattern::Continuous;
        });
        return;
    }
    if (chosen == more) {
        LineTypeManagerDialog dialog(document_, this);
        if (dialog.exec() != QDialog::Accepted) {
            return;
        }
        const std::string name = dialog.selectedCurrentLineType();
        if (name == "ByLayer") {
            ApplyStyleMutation([](EntityStyle& style) {
                style.lineTypeSource = PropertySource::ByLayer;
            });
        } else if (name == "ByBlock") {
            ApplyStyleMutation([](EntityStyle& style) {
                style.lineTypeSource = PropertySource::ByBlock;
                style.lineTypeName = "Continuous";
                style.linePattern = LinePattern::Continuous;
            });
        } else {
            ApplyStyleMutation([name](EntityStyle& style) {
                style.lineTypeSource = PropertySource::Override;
                style.lineTypeName = name;
                style.linePattern = LinePatternFromLineTypeName(name);
            });
        }
        return;
    }

    const std::string name = chosen->data().toString().toStdString();
    ApplyStyleMutation([name](EntityStyle& style) {
        style.lineTypeSource = PropertySource::Override;
        style.lineTypeName = name;
        style.linePattern = LinePatternFromLineTypeName(name);
    });
}

} // namespace cad
