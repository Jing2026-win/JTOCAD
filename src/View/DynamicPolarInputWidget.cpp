#include "View/DynamicPolarInputWidget.h"

#include <algorithm>
#include <cmath>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QResizeEvent>
#include <QSignalBlocker>
#include <QTimer>

namespace cad {

namespace {

constexpr int kPolarDecimals = 4;

QString FormatLength(double value)
{
    return QString::number(value, 'f', kPolarDecimals);
}

QString FormatAngle(double degrees, bool polarPrefix)
{
    // 保留符号：CSA/SCA 负角=顺时针。不要取 abs（否则 -90 显示成 90° 并在提交时丢方向）。
    // 也不要把负角折成 360-α（-30 不得变成 330°）。
    if (!std::isfinite(degrees)) {
        degrees = 0.0;
    }
    degrees = std::fmod(degrees, 360.0);
    if (std::abs(degrees) < 1.0e-9) {
        degrees = 0.0;
    }
    const QString body = QString::number(degrees, 'f', 0) + QStringLiteral("°");
    return polarPrefix ? (QStringLiteral("< ") + body) : body;
}

void StripAngleDecorations(QString& text)
{
    text = text.trimmed();
    if (text.startsWith(QLatin1Char('<'))) {
        text = text.mid(1).trimmed();
    }
    if (text.endsWith(QChar(0x00B0)) || text.endsWith(QLatin1Char('d')) || text.endsWith(QLatin1Char('D'))) {
        text.chop(1);
        text = text.trimmed();
    }
}

const char* kFieldActiveStyle =
    "QLineEdit {"
    "  background-color: #ffffff;"
    "  color: #000000;"
    "  border: 1px solid #3399ff;"
    "  padding: 0px 2px;"
    "  font-size: 11px;"
    "  selection-background-color: #3399ff;"
    "  selection-color: #ffffff;"
    "}";

const char* kFieldInactiveStyle =
    "QLineEdit {"
    "  background-color: #7a7a7a;"
    "  color: #1e1e1e;"
    "  border: 1px solid #5a5a5a;"
    "  padding: 0px 2px;"
    "  font-size: 11px;"
    "}";

const char* kHintStyle =
    "QLabel {"
    "  background-color: #1a1a1a;"
    "  color: #ffffff;"
    "  border: 1px solid #000000;"
    "  padding: 2px 6px;"
    "  font-size: 11px;"
    "}";

void ApplyFieldStyle(QLineEdit* edit, bool active)
{
    edit->setFrame(false);
    edit->setAttribute(Qt::WA_StyledBackground, true);
    edit->setAutoFillBackground(true);
    edit->setStyleSheet(active ? kFieldActiveStyle : kFieldInactiveStyle);
}

} // namespace

DynamicPolarInputWidget::DynamicPolarInputWidget(QWidget* parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_StyledBackground, true);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
    setAutoFillBackground(false);

    lengthEdit_ = new QLineEdit(this);
    angleEdit_ = new QLineEdit(this);
    tabHint_ = new QLabel(tr("Tab 键"), this);
    tabHint_->setStyleSheet(kHintStyle);
    tabHint_->setAlignment(Qt::AlignCenter);

    // AutoCAD dyninput length box ≈ 69×24px; angle is a compact square.
    lengthEdit_->setFixedSize(70, 24);
    angleEdit_->setFixedSize(40, 24);
    tabHint_->adjustSize();
    tabHint_->setFixedHeight(24);
    lengthEdit_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    angleEdit_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    // Children receive mouse; overlay itself stays click-through.
    lengthEdit_->setAttribute(Qt::WA_TransparentForMouseEvents, false);
    angleEdit_->setAttribute(Qt::WA_TransparentForMouseEvents, false);
    tabHint_->setAttribute(Qt::WA_TransparentForMouseEvents, true);

    lengthEdit_->installEventFilter(this);
    angleEdit_->installEventFilter(this);

    connect(lengthEdit_, &QLineEdit::textEdited, this, [this]() {
        lengthLocked_ = true;
        lengthActive_ = true;
        RefreshActiveStyle();
        if (onValueChanged) {
            onValueChanged();
        }
    });
    connect(angleEdit_, &QLineEdit::textEdited, this, [this]() {
        angleLocked_ = true;
        lengthActive_ = false;
        RefreshActiveStyle();
        if (onValueChanged) {
            onValueChanged();
        }
    });
    connect(angleEdit_, &QLineEdit::editingFinished, this, [this]() {
        double value = 0.0;
        QString text = angleEdit_->text();
        StripAngleDecorations(text);
        if (!ParseNumber(text, value)) {
            return;
        }
        // 规范化显示，保留负号（勿用 abs）。
        const QSignalBlocker block(angleEdit_);
        angleEdit_->setText(FormatAngle(value, polarAnglePrefix_));
    });

    RefreshActiveStyle();
    SyncOverlaySize();
    hide();
}

void DynamicPolarInputWidget::SetLengthAngle(double length, double angleDegrees)
{
    const QSignalBlocker blockL(lengthEdit_);
    const QSignalBlocker blockA(angleEdit_);
    if (!lengthLocked_) {
        lengthEdit_->setText(FormatLength(std::max(0.0, length)));
    }
    if (!angleLocked_) {
        angleEdit_->setText(FormatAngle(angleDegrees, polarAnglePrefix_));
    }
    RefreshActiveStyle();
    // AutoCAD: the active unlocked field stays fully selected while the
    // rubber-band updates, so the next keystroke replaces the value.
    if (lengthVisible_ && lengthActive_ && !lengthLocked_) {
        if (QWidget::focusWidget() != lengthEdit_) {
            lengthEdit_->setFocus(Qt::OtherFocusReason);
        }
        lengthEdit_->selectAll();
    } else if (angleVisible_ && !lengthActive_ && !angleLocked_) {
        if (QWidget::focusWidget() != angleEdit_) {
            angleEdit_->setFocus(Qt::OtherFocusReason);
        }
        angleEdit_->selectAll();
    }
}

double DynamicPolarInputWidget::Length() const
{
    double value = 0.0;
    ParseNumber(lengthEdit_->text(), value);
    if (!std::isfinite(value)) {
        return 0.0;
    }
    // SCL/CSL：保留符号（负弦长=优弧）；距离用途在调用处取 abs。
    return value;
}

double DynamicPolarInputWidget::AngleDegrees() const
{
    double value = 0.0;
    QString text = angleEdit_->text();
    StripAngleDecorations(text);
    ParseNumber(text, value);
    // AutoCAD CSA/SCA：保留符号（负角=顺时针）；其他调用处自行取 abs。
    // 仅把幅值规范到 (-360, 360)。
    if (!std::isfinite(value)) {
        return 0.0;
    }
    value = std::fmod(value, 360.0);
    return value;
}

void DynamicPolarInputWidget::SetLengthVisible(bool visible)
{
    if (lengthVisible_ == visible) {
        return;
    }
    lengthVisible_ = visible;
    lengthEdit_->setVisible(visible);
    tabHint_->setVisible(tabHintEnabled_ && visible && angleVisible_);
    if (!visible) {
        lengthLocked_ = false;
        lengthActive_ = false;
        RefreshActiveStyle();
        if (isVisible() && angleVisible_) {
            FocusAngle();
        }
    } else if (!lengthActive_ && !angleLocked_) {
        lengthActive_ = true;
        RefreshActiveStyle();
    }
}

void DynamicPolarInputWidget::SetAngleVisible(bool visible)
{
    if (angleVisible_ == visible) {
        return;
    }
    angleVisible_ = visible;
    angleEdit_->setVisible(visible);
    tabHint_->setVisible(tabHintEnabled_ && visible && lengthVisible_);
    if (!visible) {
        angleLocked_ = false;
        lengthActive_ = true;
        RefreshActiveStyle();
        if (isVisible() && lengthVisible_) {
            FocusLength();
        }
    } else if (lengthActive_ && !lengthLocked_) {
        RefreshActiveStyle();
    }
}

void DynamicPolarInputWidget::MoveToLinePositions(const QPoint& lengthViewPos, const QPoint& angleViewPos)
{
    SyncOverlaySize();

    auto clampPos = [this](QWidget* child, int x, int y) {
        if (QWidget* p = parentWidget()) {
            x = std::clamp(x, 0, std::max(0, p->width() - child->width()));
            y = std::clamp(y, 0, std::max(0, p->height() - child->height()));
        } else {
            x = std::max(0, x);
            y = std::max(0, y);
        }
        child->move(x, y);
    };

    if (lengthVisible_) {
        clampPos(lengthEdit_,
            lengthViewPos.x() - lengthEdit_->width() / 2,
            lengthViewPos.y() - lengthEdit_->height() / 2);
        lengthEdit_->raise();
    }

    if (angleVisible_) {
        clampPos(angleEdit_,
            angleViewPos.x() - angleEdit_->width() / 2,
            angleViewPos.y() - angleEdit_->height() / 2);
        angleEdit_->raise();
    }

    if (tabHintEnabled_ && lengthVisible_ && angleVisible_) {
        tabHint_->adjustSize();
        tabHint_->setFixedHeight(angleEdit_->height());
        int tabX = angleEdit_->x() + angleEdit_->width() + 3;
        int tabY = angleEdit_->y();
        if (QWidget* p = parentWidget()) {
            tabX = std::clamp(tabX, 0, std::max(0, p->width() - tabHint_->width()));
            tabY = std::clamp(tabY, 0, std::max(0, p->height() - tabHint_->height()));
        }
        tabHint_->move(tabX, tabY);
        tabHint_->show();
        tabHint_->raise();
    } else {
        tabHint_->hide();
    }
}

void DynamicPolarInputWidget::MoveToHorizontalRow(const QPoint& topLeft)
{
    SyncOverlaySize();

    auto place = [this](QWidget* child, int& x, int y) {
        if (QWidget* p = parentWidget()) {
            x = std::clamp(x, 0, std::max(0, p->width() - child->width()));
            y = std::clamp(y, 0, std::max(0, p->height() - child->height()));
        } else {
            x = std::max(0, x);
            y = std::max(0, y);
        }
        child->move(x, y);
        child->raise();
        x = child->x() + child->width() + 3;
    };

    int x = topLeft.x();
    const int y = topLeft.y();
    if (lengthVisible_) {
        place(lengthEdit_, x, y);
    }
    if (angleVisible_) {
        place(angleEdit_, x, y);
    }
    tabHint_->hide();
}

void DynamicPolarInputWidget::SetPolarAnglePrefix(bool enabled)
{
    if (polarAnglePrefix_ == enabled) {
        return;
    }
    polarAnglePrefix_ = enabled;
    angleEdit_->setFixedSize(enabled ? 56 : 40, 24);
    if (!angleLocked_) {
        const QSignalBlocker block(angleEdit_);
        double value = 0.0;
        QString text = angleEdit_->text();
        StripAngleDecorations(text);
        if (ParseNumber(text, value)) {
            angleEdit_->setText(FormatAngle(value, polarAnglePrefix_));
        }
    }
}

void DynamicPolarInputWidget::SetTabHintVisible(bool visible)
{
    tabHintEnabled_ = visible;
    tabHint_->setVisible(visible && lengthVisible_ && angleVisible_);
}

void DynamicPolarInputWidget::FocusLength()
{
    if (!lengthVisible_) {
        if (angleVisible_) {
            FocusAngle();
        }
        return;
    }
    lengthActive_ = true;
    RefreshActiveStyle();
    lengthEdit_->setFocus(Qt::OtherFocusReason);
    SelectActiveText();
}

void DynamicPolarInputWidget::FocusAngle()
{
    if (!angleVisible_) {
        if (lengthVisible_) {
            FocusLength();
        }
        return;
    }
    lengthActive_ = false;
    RefreshActiveStyle();
    angleEdit_->setFocus(Qt::OtherFocusReason);
    SelectActiveText();
}

void DynamicPolarInputWidget::ToggleFocus()
{
    if (!lengthVisible_ || !angleVisible_) {
        return;
    }
    // AutoCAD: leaving a field locks its current value.
    if (lengthActive_) {
        lengthLocked_ = true;
        FocusAngle();
    } else {
        angleLocked_ = true;
        FocusLength();
    }
}

void DynamicPolarInputWidget::ResetLocks()
{
    lengthLocked_ = false;
    angleLocked_ = false;
    lengthActive_ = lengthVisible_;
    RefreshActiveStyle();
}

void DynamicPolarInputWidget::SetFieldsClickable(bool clickable)
{
    if (lengthEdit_) {
        lengthEdit_->setAttribute(Qt::WA_TransparentForMouseEvents, !clickable);
        lengthEdit_->setReadOnly(!clickable);
    }
    if (angleEdit_) {
        angleEdit_->setAttribute(Qt::WA_TransparentForMouseEvents, !clickable);
        angleEdit_->setReadOnly(!clickable);
    }
}

void DynamicPolarInputWidget::BeginTyping(const QString& firstChar)
{
    if (!lengthVisible_ && angleVisible_) {
        lengthActive_ = false;
        angleLocked_ = true;
        RefreshActiveStyle();
        angleEdit_->setFocus(Qt::OtherFocusReason);
        angleEdit_->setText(firstChar);
        angleEdit_->setCursorPosition(firstChar.size());
        if (onValueChanged) {
            onValueChanged();
        }
        return;
    }
    lengthActive_ = true;
    lengthLocked_ = true;
    RefreshActiveStyle();
    lengthEdit_->setFocus(Qt::OtherFocusReason);
    lengthEdit_->setText(firstChar);
    lengthEdit_->setCursorPosition(firstChar.size());
    if (onValueChanged) {
        onValueChanged();
    }
}

QLineEdit* DynamicPolarInputWidget::ActiveEdit() const
{
    if (lengthVisible_ && lengthActive_) {
        return lengthEdit_;
    }
    if (angleVisible_) {
        return angleEdit_;
    }
    return lengthEdit_;
}

void DynamicPolarInputWidget::RefreshActiveStyle()
{
    if (lengthVisible_) {
        ApplyFieldStyle(lengthEdit_, lengthActive_);
    }
    if (angleVisible_) {
        ApplyFieldStyle(angleEdit_, !lengthVisible_ || !lengthActive_);
    }
}

void DynamicPolarInputWidget::SelectActiveText()
{
    QLineEdit* active = ActiveEdit();
    if (lengthVisible_ && angleVisible_) {
        QLineEdit* inactive = lengthActive_ ? angleEdit_ : lengthEdit_;
        inactive->deselect();
    }
    if (QWidget::focusWidget() != active) {
        active->setFocus(Qt::OtherFocusReason);
    }
    active->selectAll();
}

bool DynamicPolarInputWidget::ParseNumber(const QString& text, double& value) const
{
    bool ok = false;
    const double parsed = text.trimmed().toDouble(&ok);
    if (!ok) {
        return false;
    }
    value = parsed;
    return true;
}

void DynamicPolarInputWidget::SyncOverlaySize()
{
    if (QWidget* p = parentWidget()) {
        if (size() != p->size()) {
            setGeometry(0, 0, p->width(), p->height());
        }
    }
}

void DynamicPolarInputWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
}

bool DynamicPolarInputWidget::eventFilter(QObject* watched, QEvent* event)
{
    const bool onLength = watched == lengthEdit_;
    const bool onAngle = watched == angleEdit_;
    if (!onLength && !onAngle) {
        return QWidget::eventFilter(watched, event);
    }

    if (event->type() == QEvent::FocusIn) {
        lengthActive_ = onLength;
        RefreshActiveStyle();
        QTimer::singleShot(0, this, [this]() { SelectActiveText(); });
        return false;
    }
    if (event->type() == QEvent::MouseButtonPress) {
        lengthActive_ = onLength;
        RefreshActiveStyle();
        QTimer::singleShot(0, this, [this]() { SelectActiveText(); });
        return false;
    }
    if (event->type() != QEvent::KeyPress) {
        return QWidget::eventFilter(watched, event);
    }

    auto* keyEvent = static_cast<QKeyEvent*>(event);
    if (onInterceptKey && onInterceptKey(keyEvent)) {
        return true;
    }
    if (keyEvent->key() == Qt::Key_Tab || keyEvent->key() == Qt::Key_Backtab) {
        ToggleFocus();
        return true;
    }
    if (keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter) {
        if (onConfirmed) {
            onConfirmed();
        }
        return true;
    }
    if (keyEvent->key() == Qt::Key_Escape) {
        if (onCancelRequested) {
            onCancelRequested();
        }
        return true;
    }
    return QWidget::eventFilter(watched, event);
}

} // namespace cad
