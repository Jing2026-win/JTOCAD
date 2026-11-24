#include "Ui/BlockRibbonWidget.h"

#include <QAction>
#include <QHBoxLayout>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QSizePolicy>
#include <QToolButton>
#include <QVBoxLayout>

namespace cad {

namespace {

constexpr int kRibbonHeight = 66;
constexpr int kLargeBtnWidth = 52;
constexpr int kSmallBtnWidth = 72;
constexpr int kSmallBtnHeight = 20;

QString ToolButtonStyle()
{
    return QStringLiteral(
        "QToolButton { border: none; padding: 0px; color: #ececec; }"
        "QToolButton:hover { background-color: #3e3e42; border-radius: 2px; }"
        "QToolButton:pressed { background-color: #094771; }"
        "QToolButton::menu-indicator { image: none; width: 0px; }");
}

class LargeBlockButton : public QToolButton {
public:
    enum class Kind { Insert, Detect };

    LargeBlockButton(Kind kind, const QString& label, QWidget* parent = nullptr)
        : QToolButton(parent)
        , kind_(kind)
        , label_(label)
    {
        setAutoRaise(true);
        setFixedSize(kLargeBtnWidth, kRibbonHeight);
        setStyleSheet(ToolButtonStyle());
        setPopupMode(MenuButtonPopup);
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

        if (kind_ == Kind::Insert) {
            painter.setPen(QPen(QColor(90, 170, 230), 1.3));
            painter.setBrush(QColor(50, 110, 170));
            painter.drawRoundedRect(QRectF(10, 8, 28, 22), 2, 2);
            painter.setPen(QPen(QColor(230, 230, 230), 1.6));
            painter.drawLine(24, 14, 24, 24);
            painter.drawLine(19, 19, 29, 19);
            // dropdown chevron
            painter.setPen(QPen(QColor(200, 200, 200), 1.2));
            painter.drawLine(20, 34, 26, 34);
            painter.drawLine(22, 36, 24, 38);
            painter.drawLine(24, 38, 26, 36);
        } else {
            painter.setPen(QPen(QColor(120, 200, 140), 1.3));
            painter.setBrush(QColor(40, 100, 60));
            painter.drawEllipse(QRectF(12, 8, 24, 24));
            painter.setPen(QPen(QColor(230, 230, 230), 1.5));
            painter.drawLine(20, 20, 26, 26);
            painter.drawEllipse(QRectF(17, 15, 8, 8));
        }

        painter.setPen(QColor(236, 236, 236));
        QFont font = painter.font();
        font.setPointSize(9);
        painter.setFont(font);
        painter.drawText(QRect(0, 42, width(), 20), Qt::AlignHCenter | Qt::AlignTop, label_);
    }

private:
    Kind kind_;
    QString label_;
};

class SmallBlockButton : public QToolButton {
public:
    enum class Kind { Create, Edit, Attr };

    SmallBlockButton(Kind kind, const QString& label, QWidget* parent = nullptr)
        : QToolButton(parent)
        , kind_(kind)
        , label_(label)
    {
        setAutoRaise(true);
        setFixedSize(kSmallBtnWidth, kSmallBtnHeight);
        setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        setStyleSheet(ToolButtonStyle());
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

        const QRectF icon(4, 3, 14, 14);
        if (kind_ == Kind::Create) {
            painter.setPen(QPen(QColor(100, 180, 240), 1.2));
            painter.setBrush(QColor(45, 90, 140));
            painter.drawRect(icon);
            painter.setPen(QPen(QColor(230, 230, 230), 1.4));
            painter.drawLine(icon.center().x(), icon.top() + 3, icon.center().x(), icon.bottom() - 3);
            painter.drawLine(icon.left() + 3, icon.center().y(), icon.right() - 3, icon.center().y());
        } else if (kind_ == Kind::Edit) {
            painter.setPen(QPen(QColor(220, 180, 100), 1.2));
            painter.setBrush(QColor(140, 110, 50));
            QPainterPath pencil;
            pencil.moveTo(6, 14);
            pencil.lineTo(8, 12);
            pencil.lineTo(14, 6);
            pencil.lineTo(16, 8);
            pencil.lineTo(10, 14);
            pencil.closeSubpath();
            painter.drawPath(pencil);
        } else {
            painter.setPen(QPen(QColor(180, 180, 220), 1.2));
            painter.setBrush(QColor(70, 70, 120));
            painter.drawRoundedRect(icon, 2, 2);
            painter.setPen(QPen(QColor(230, 230, 230), 1.0));
            painter.drawLine(7, 7, 15, 7);
            painter.drawLine(7, 10, 13, 10);
            painter.drawLine(7, 13, 14, 13);
        }

        painter.setPen(QColor(236, 236, 236));
        QFont font = painter.font();
        font.setPointSize(8);
        painter.setFont(font);
        painter.drawText(QRect(22, 0, width() - 24, height()), Qt::AlignVCenter | Qt::AlignLeft, label_);
    }

private:
    Kind kind_;
    QString label_;
};

} // namespace

BlockRibbonWidget::BlockRibbonWidget(QWidget* parent)
    : QWidget(parent)
{
    setFixedHeight(kRibbonHeight);
    setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    setStyleSheet(QStringLiteral("QWidget { background: transparent; color: #ececec; }"));

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(2, 0, 2, 0);
    layout->setSpacing(4);

    insertButton_ = new LargeBlockButton(LargeBlockButton::Kind::Insert, QStringLiteral("插入"), this);
    insertButton_->setToolTip(QStringLiteral("插入块 (I)"));
    detectButton_ = new LargeBlockButton(LargeBlockButton::Kind::Detect, QStringLiteral("检测"), this);
    detectButton_->setToolTip(QStringLiteral("检测重复图形并建议创建块"));
    detectButton_->setPopupMode(QToolButton::DelayedPopup);

    auto* stack = new QVBoxLayout();
    stack->setContentsMargins(0, 2, 0, 2);
    stack->setSpacing(2);
    createButton_ = new SmallBlockButton(SmallBlockButton::Kind::Create, QStringLiteral("创建"), this);
    createButton_->setToolTip(QStringLiteral("创建块 (B)"));
    editButton_ = new SmallBlockButton(SmallBlockButton::Kind::Edit, QStringLiteral("编辑"), this);
    editButton_->setToolTip(QStringLiteral("编辑块定义"));
    attrButton_ = new SmallBlockButton(SmallBlockButton::Kind::Attr, QStringLiteral("编辑属性"), this);
    attrButton_->setToolTip(QStringLiteral("编辑块属性 (ATTEDIT)"));
    attrButton_->setPopupMode(QToolButton::MenuButtonPopup);
    stack->addWidget(createButton_);
    stack->addWidget(editButton_);
    stack->addWidget(attrButton_);

    layout->addWidget(insertButton_);
    layout->addWidget(detectButton_);
    layout->addLayout(stack);

    auto* insertMenu = new QMenu(this);
    auto* insertAction = insertMenu->addAction(QStringLiteral("插入块 (I)"));
    auto* wblockAction = insertMenu->addAction(QStringLiteral("写块 WBLOCK (W)"));
    insertMenu->addSeparator();
    auto* adcAction = insertMenu->addAction(QStringLiteral("设计中心 ADC (Ctrl+2)"));
    auto* paletteAction = insertMenu->addAction(QStringLiteral("块选项板 (Ctrl+3)"));
    insertButton_->setMenu(insertMenu);

    auto* editMenu = new QMenu(this);
    auto* beditAction = editMenu->addAction(QStringLiteral("块编辑器 BEDIT"));
    auto* refEditAction = editMenu->addAction(QStringLiteral("在位编辑 REFEDIT"));
    editMenu->addSeparator();
    auto* demoDynAction = editMenu->addAction(QStringLiteral("一键演示动态块（测试用）"));
    editButton_->setPopupMode(QToolButton::MenuButtonPopup);
    editButton_->setMenu(editMenu);

    auto* attrMenu = new QMenu(this);
    auto* editAttrAction = attrMenu->addAction(QStringLiteral("编辑属性"));
    auto* explodeAction = attrMenu->addAction(QStringLiteral("分解选中块"));
    auto* purgeAction = attrMenu->addAction(QStringLiteral("清理未用块"));
    attrButton_->setMenu(attrMenu);

    connect(insertButton_, &QToolButton::clicked, this, [this]() {
        if (insertCallback_) {
            insertCallback_();
        }
    });
    connect(insertAction, &QAction::triggered, this, [this]() {
        if (insertCallback_) {
            insertCallback_();
        }
    });
    connect(wblockAction, &QAction::triggered, this, [this]() {
        if (wblockCallback_) {
            wblockCallback_();
        }
    });
    connect(adcAction, &QAction::triggered, this, [this]() {
        if (designCenterCallback_) {
            designCenterCallback_();
        }
    });
    connect(paletteAction, &QAction::triggered, this, [this]() {
        if (blocksPaletteCallback_) {
            blocksPaletteCallback_();
        }
    });
    connect(detectButton_, &QToolButton::clicked, this, [this]() {
        if (detectCallback_) {
            detectCallback_();
        }
    });
    connect(createButton_, &QToolButton::clicked, this, [this]() {
        if (createCallback_) {
            createCallback_();
        }
    });
    connect(editButton_, &QToolButton::clicked, this, [this]() {
        if (editCallback_) {
            editCallback_();
        }
    });
    connect(beditAction, &QAction::triggered, this, [this]() {
        if (editCallback_) {
            editCallback_();
        }
    });
    connect(refEditAction, &QAction::triggered, this, [this]() {
        if (refEditCallback_) {
            refEditCallback_();
        }
    });
    connect(demoDynAction, &QAction::triggered, this, [this]() {
        if (demoDynamicBlockCallback_) {
            demoDynamicBlockCallback_();
        }
    });
    connect(attrButton_, &QToolButton::clicked, this, [this]() {
        if (editAttributesCallback_) {
            editAttributesCallback_();
        }
    });
    connect(editAttrAction, &QAction::triggered, this, [this]() {
        if (editAttributesCallback_) {
            editAttributesCallback_();
        }
    });
    connect(explodeAction, &QAction::triggered, this, [this]() {
        if (explodeCallback_) {
            explodeCallback_();
        }
    });
    connect(purgeAction, &QAction::triggered, this, [this]() {
        if (purgeCallback_) {
            purgeCallback_();
        }
    });
}

void BlockRibbonWidget::SetInsertCallback(std::function<void()> callback) { insertCallback_ = std::move(callback); }
void BlockRibbonWidget::SetDetectCallback(std::function<void()> callback) { detectCallback_ = std::move(callback); }
void BlockRibbonWidget::SetCreateCallback(std::function<void()> callback) { createCallback_ = std::move(callback); }
void BlockRibbonWidget::SetEditCallback(std::function<void()> callback) { editCallback_ = std::move(callback); }
void BlockRibbonWidget::SetEditAttributesCallback(std::function<void()> callback) { editAttributesCallback_ = std::move(callback); }
void BlockRibbonWidget::SetExplodeCallback(std::function<void()> callback) { explodeCallback_ = std::move(callback); }
void BlockRibbonWidget::SetPurgeCallback(std::function<void()> callback) { purgeCallback_ = std::move(callback); }
void BlockRibbonWidget::SetWblockCallback(std::function<void()> callback) { wblockCallback_ = std::move(callback); }
void BlockRibbonWidget::SetDemoDynamicBlockCallback(std::function<void()> callback) { demoDynamicBlockCallback_ = std::move(callback); }
void BlockRibbonWidget::SetRefEditCallback(std::function<void()> callback) { refEditCallback_ = std::move(callback); }
void BlockRibbonWidget::SetDesignCenterCallback(std::function<void()> callback) { designCenterCallback_ = std::move(callback); }
void BlockRibbonWidget::SetBlocksPaletteCallback(std::function<void()> callback) { blocksPaletteCallback_ = std::move(callback); }

QSize BlockRibbonWidget::sizeHint() const
{
    return QSize(kLargeBtnWidth * 2 + kSmallBtnWidth + 16, kRibbonHeight);
}

QSize BlockRibbonWidget::minimumSizeHint() const
{
    return sizeHint();
}

} // namespace cad
