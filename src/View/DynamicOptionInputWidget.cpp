#include "View/DynamicOptionInputWidget.h"

#include <algorithm>
#include <QCursor>
#include <QHBoxLayout>
#include <QEnterEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QToolButton>
#include <QVBoxLayout>

namespace cad {

namespace {

const char* kPromptBarStyle =
    "QFrame {"
    "  background-color: #a8a8a8;"
    "  color: #000000;"
    "  border: 1px solid #000000;"
    "}";

const char* kPromptLabelStyle =
    "QLabel {"
    "  background-color: transparent;"
    "  color: #000000;"
    "  border: none;"
    "  padding: 0px 4px 0px 6px;"
    "  font-size: 11px;"
    "}";

const char* kArrowStyle =
    "QToolButton {"
    "  background-color: #9a9a9a;"
    "  color: #000000;"
    "  border: 1px solid #5a5a5a;"
    "  padding: 0px;"
    "  font-size: 9px;"
    "}"
    "QToolButton:hover {"
    "  background-color: #b8b8b8;"
    "}"
    "QToolButton:pressed {"
    "  background-color: #7a7a7a;"
    "}";

const char* kFieldStyle =
    "QLineEdit {"
    "  background-color: #ffffff;"
    "  color: #000000;"
    "  border: 1px solid #3399ff;"
    "  padding: 0px 4px;"
    "  font-size: 11px;"
    "  selection-background-color: #3399ff;"
    "  selection-color: #ffffff;"
    "}";

} // namespace

DynamicOptionInputWidget::DynamicOptionInputWidget(QWidget* parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_StyledBackground, true);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setAutoFillBackground(false);

    promptBar_ = new QFrame(this);
    promptBar_->setStyleSheet(kPromptBarStyle);
    promptBar_->setFixedHeight(24);

    promptLabel_ = new QLabel(promptBar_);
    promptLabel_->setStyleSheet(kPromptLabelStyle);
    promptLabel_->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);

    arrowButton_ = new QToolButton(promptBar_);
    arrowButton_->setStyleSheet(kArrowStyle);
    arrowButton_->setText(QStringLiteral("▼"));
    arrowButton_->setFixedSize(16, 16);
    arrowButton_->setAutoRaise(true);
    arrowButton_->setFocusPolicy(Qt::NoFocus);
    arrowButton_->setToolTip(QStringLiteral("显示选项 (↓)"));

    auto* barLayout = new QHBoxLayout(promptBar_);
    barLayout->setContentsMargins(0, 2, 3, 2);
    barLayout->setSpacing(2);
    barLayout->addWidget(promptLabel_);
    barLayout->addWidget(arrowButton_);

    edit_ = new QLineEdit(this);
    edit_->setStyleSheet(kFieldStyle);
    edit_->setFrame(false);
    edit_->setAttribute(Qt::WA_StyledBackground, true);
    edit_->setFixedHeight(24);
    edit_->setMinimumWidth(40);
    edit_->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    edit_->installEventFilter(this);
    edit_->hide();

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(3);
    layout->addWidget(promptBar_);
    layout->addWidget(edit_);

    connect(arrowButton_, &QToolButton::clicked, this, [this]() {
        ShowOptionMenu();
    });
    connect(edit_, &QLineEdit::textEdited, this, [this]() {
        SyncInputWidth();
    });

    hide();
}

void DynamicOptionInputWidget::SetPrompt(const QString& prompt)
{
    if (cycleList_) {
        ClearCycleList();
    }
    promptLabel_->setTextFormat(Qt::PlainText);
    promptLabel_->setWordWrap(false);
    promptLabel_->setText(prompt);
    promptBar_->setFixedHeight(24);
    promptLabel_->adjustSize();
    adjustSize();
}

void DynamicOptionInputWidget::SetCycleList(
    const QString& title,
    const QString& hint,
    const QVector<QString>& items,
    int currentIndex)
{
    cycleList_ = true;
    promptLabel_->setTextFormat(Qt::RichText);
    promptLabel_->setWordWrap(true);
    QString html;
    html += QStringLiteral("<div>%1</div>").arg(title.toHtmlEscaped());
    html += QStringLiteral("<div>%1</div>").arg(hint.toHtmlEscaped());
    for (int i = 0; i < items.size(); ++i) {
        const QString text = items[i].toHtmlEscaped();
        if (i == currentIndex) {
            html += QStringLiteral(
                "<div style=\"background-color:#3399ff;color:#ffffff;\">&nbsp;%1&nbsp;</div>")
                .arg(text);
        } else {
            html += QStringLiteral("<div>&nbsp;%1&nbsp;</div>").arg(text);
        }
    }
    promptLabel_->setText(html);
    const int lineCount = 2 + items.size();
    promptBar_->setFixedHeight(std::max(24, 6 + lineCount * 16));
    promptLabel_->adjustSize();
    adjustSize();
}

void DynamicOptionInputWidget::ClearCycleList()
{
    if (!cycleList_) {
        return;
    }
    cycleList_ = false;
    promptLabel_->setTextFormat(Qt::PlainText);
    promptLabel_->setWordWrap(false);
    promptBar_->setFixedHeight(24);
}

void DynamicOptionInputWidget::SetOptions(const QVector<DynCommandOption>& options)
{
    options_ = options;
    if (arrowButton_) {
        arrowButton_->setVisible(!options.isEmpty());
    }
    if (promptBar_) {
        promptBar_->adjustSize();
    }
    adjustSize();
}

void DynamicOptionInputWidget::MoveToViewPosition(const QPoint& viewPos)
{
    if (menuOpen_ || keywordEditing_ || ContainsGlobalPos(QCursor::pos())) {
        return;
    }
    adjustSize();
    int x = viewPos.x() + 18;
    int y = viewPos.y() + 28;
    if (QWidget* p = parentWidget()) {
        x = std::clamp(x, 0, std::max(0, p->width() - width()));
        y = std::clamp(y, 0, std::max(0, p->height() - height()));
    }
    move(x, y);
}

void DynamicOptionInputWidget::BeginTyping(const QString& firstChar)
{
    show();
    raise();
    SetKeywordEditVisible(true);
    edit_->setFocus(Qt::OtherFocusReason);
    if (edit_->hasSelectedText() || edit_->text().isEmpty()) {
        edit_->setText(firstChar);
    } else {
        edit_->setText(edit_->text() + firstChar);
    }
    edit_->setCursorPosition(edit_->text().size());
    SyncInputWidth();
}

void DynamicOptionInputWidget::BeginValueEdit(const QString& current)
{
    show();
    raise();
    SetKeywordEditVisible(true);
    const QSignalBlocker block(edit_);
    edit_->setText(current);
    edit_->selectAll();
    edit_->setFocus(Qt::OtherFocusReason);
    SyncInputWidth();
}

void DynamicOptionInputWidget::FocusKeywordEdit()
{
    if (!keywordEditing_ || !edit_ || !edit_->isVisible()) {
        return;
    }
    edit_->setFocus(Qt::OtherFocusReason);
    edit_->setCursorPosition(edit_->text().size());
}

void DynamicOptionInputWidget::ClearInput()
{
    const QSignalBlocker block(edit_);
    edit_->clear();
    SyncInputWidth();
}

void DynamicOptionInputWidget::CancelKeywordEdit()
{
    if (!keywordEditing_ && !edit_->isVisible()) {
        return;
    }
    HideKeywordEdit();
    if (onKeywordEditCancelled) {
        onKeywordEditCancelled();
    }
}

void DynamicOptionInputWidget::HideKeywordEdit()
{
    SetKeywordEditVisible(false);
    if (cycleList_ && !isVisible()) {
        ClearCycleList();
    }
}

void DynamicOptionInputWidget::SetKeywordEditVisible(bool visible)
{
    keywordEditing_ = visible;
    if (!visible) {
        ClearInput();
    }
    edit_->setVisible(visible);
    adjustSize();
}

bool DynamicOptionInputWidget::HasInput() const
{
    return !edit_->text().trimmed().isEmpty();
}

QString DynamicOptionInputWidget::InputText() const
{
    return edit_->text().trimmed();
}

void DynamicOptionInputWidget::ShowOptionMenu()
{
    if (options_.isEmpty()) {
        return;
    }

    QMenu menu(this);
    menu.setStyleSheet(
        QStringLiteral(
            "QMenu {"
            "  background-color: #f0f0f0;"
            "  color: #000000;"
            "  border: 1px solid #000000;"
            "  font-size: 11px;"
            "}"
            "QMenu::item {"
            "  padding: 3px 18px;"
            "}"
            "QMenu::item:selected {"
            "  background-color: #3399ff;"
            "  color: #ffffff;"
            "}"
            "QMenu::item:disabled {"
            "  color: #888888;"
            "}"));

    for (const DynCommandOption& option : options_) {
        QAction* action = menu.addAction(option.label);
        action->setEnabled(option.enabled);
        action->setData(option.keyword);
    }

    menuOpen_ = true;
    const QPoint globalPos = arrowButton_->mapToGlobal(QPoint(0, arrowButton_->height()));
    QAction* chosen = menu.exec(globalPos);
    menuOpen_ = false;
    if (chosen && chosen->isEnabled()) {
        ApplyKeyword(chosen->data().toString());
    }
}

void DynamicOptionInputWidget::ConfirmInput()
{
    const QString typed = InputText();
    if (typed.isEmpty()) {
        if (options_.isEmpty()) {
            ApplyKeyword(QString());
            return;
        }
        CancelKeywordEdit();
        return;
    }
    const QString keyword = MatchKeyword(typed);
    if (keyword.isEmpty()) {
        if (onInvalidKeyword) {
            onInvalidKeyword(QStringLiteral("无效的选项关键字"));
        }
        edit_->selectAll();
        return;
    }
    ApplyKeyword(keyword);
}

QString DynamicOptionInputWidget::MatchKeyword(const QString& typed) const
{
    const QString trimmed = typed.trimmed();
    if (trimmed.isEmpty()) {
        return {};
    }
    const QString upper = trimmed.toUpper();

    QString exact;
    QVector<QString> prefixHits;
    for (const DynCommandOption& option : options_) {
        if (!option.enabled) {
            continue;
        }
        if (option.keyword.compare(trimmed, Qt::CaseInsensitive) == 0
            || option.label == trimmed) {
            exact = option.keyword;
            break;
        }
        if (option.keyword.startsWith(upper, Qt::CaseInsensitive)
            || option.label.startsWith(trimmed)) {
            prefixHits.push_back(option.keyword);
        }
    }
    if (!exact.isEmpty()) {
        return exact;
    }
    if (prefixHits.size() == 1) {
        return prefixHits.front();
    }
    bool ok = false;
    trimmed.toDouble(&ok);
    if (ok) {
        return trimmed;
    }
    return {};
}

void DynamicOptionInputWidget::ApplyKeyword(const QString& keyword)
{
    SetKeywordEditVisible(false);
    if (onOptionChosen) {
        onOptionChosen(keyword);
    }
}

bool DynamicOptionInputWidget::ContainsGlobalPos(const QPoint& globalPos) const
{
    if (!isVisible()) {
        return false;
    }
    return rect().adjusted(-2, -2, 2, 2).contains(mapFromGlobal(globalPos));
}

void DynamicOptionInputWidget::SyncInputWidth()
{
    const int textWidth = edit_->fontMetrics().horizontalAdvance(edit_->text()) + 16;
    edit_->setFixedWidth(std::clamp(textWidth, 56, 140));
    adjustSize();
}

bool DynamicOptionInputWidget::eventFilter(QObject* watched, QEvent* event)
{
    if (watched != edit_ || event->type() != QEvent::KeyPress) {
        return QWidget::eventFilter(watched, event);
    }

    auto* keyEvent = static_cast<QKeyEvent*>(event);
    if (keyEvent->key() == Qt::Key_Down || keyEvent->key() == Qt::Key_Up) {
        ShowOptionMenu();
        return true;
    }
    if (keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter) {
        ConfirmInput();
        return true;
    }
    if (keyEvent->key() == Qt::Key_Space && !HasInput()) {
        CancelKeywordEdit();
        return true;
    }
    if (keyEvent->key() == Qt::Key_Escape) {
        if (onEscapeRequested) {
            SetKeywordEditVisible(false);
            onEscapeRequested();
            return true;
        }
        CancelKeywordEdit();
        return true;
    }
    return QWidget::eventFilter(watched, event);
}

GripHoverMenuWidget::GripHoverMenuWidget(QWidget* parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_StyledBackground, true);
    setFocusPolicy(Qt::NoFocus);
    setStyleSheet(QStringLiteral(
        "QWidget { background-color: #ffffff; color: #000000; border: 1px solid #8a8a8a; }"
        "QToolButton {"
        "  background-color: #ffffff;"
        "  color: #000000;"
        "  border: none;"
        "  padding: 3px 14px 3px 8px;"
        "  font-size: 12px;"
        "  text-align: left;"
        "}"
        "QToolButton:hover {"
        "  background-color: #3399ff;"
        "  color: #ffffff;"
        "}"
        "QToolButton:disabled { color: #888888; }"));
    layout_ = new QVBoxLayout(this);
    layout_->setContentsMargins(1, 2, 1, 2);
    layout_->setSpacing(0);
    hide();
}

void GripHoverMenuWidget::SetItems(const QVector<DynCommandOption>& items)
{
    while (QLayoutItem* child = layout_->takeAt(0)) {
        if (QWidget* widget = child->widget()) {
            delete widget;
        }
        delete child;
    }
    for (const DynCommandOption& item : items) {
        auto* button = new QToolButton(this);
        button->setText(item.label);
        button->setEnabled(item.enabled);
        button->setToolButtonStyle(Qt::ToolButtonTextOnly);
        button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        button->setFocusPolicy(Qt::NoFocus);
        button->setCursor(Qt::ArrowCursor);
        const QString keyword = item.keyword;
        connect(button, &QToolButton::clicked, this, [this, keyword]() {
            if (onOptionChosen) {
                onOptionChosen(keyword);
            }
        });
        layout_->addWidget(button);
    }
    adjustSize();
}

void GripHoverMenuWidget::MoveNearCursor(const QPoint& viewPos)
{
    adjustSize();
    int x = viewPos.x() + 16;
    int y = viewPos.y() + 18;
    if (QWidget* p = parentWidget()) {
        x = std::clamp(x, 0, std::max(0, p->width() - width()));
        y = std::clamp(y, 0, std::max(0, p->height() - height()));
    }
    move(x, y);
}

bool GripHoverMenuWidget::ContainsGlobalPos(const QPoint& globalPos) const
{
    return rect().adjusted(-4, -4, 4, 4).contains(mapFromGlobal(globalPos));
}

void GripHoverMenuWidget::enterEvent(QEnterEvent* event)
{
    QWidget::enterEvent(event);
    if (onEntered) {
        onEntered();
    }
}

void GripHoverMenuWidget::leaveEvent(QEvent* event)
{
    QWidget::leaveEvent(event);
    if (onLeft) {
        onLeft();
    }
}

} // namespace cad
