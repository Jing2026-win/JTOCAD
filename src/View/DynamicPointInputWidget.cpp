#include "View/DynamicPointInputWidget.h"

#include <algorithm>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QTimer>

namespace cad {

namespace {

constexpr int kCoordDecimals = 4;

QString FormatCoord(double value)
{
    return QString::number(value, 'f', kCoordDecimals);
}

const char* kLabelStyle =
    "QLabel {"
    "  background-color: #a8a8a8;"
    "  color: #000000;"
    "  border: 1px solid #000000;"
    "  padding: 2px 6px;"
    "  font-size: 11px;"
    "}";

const char* kFieldActiveStyle =
    "QLineEdit {"
    "  background-color: #ffffff;"
    "  color: #000000;"
    "  border: 1px solid #3399ff;"
    "  padding: 2px 4px;"
    "  font-size: 11px;"
    "  selection-background-color: #3399ff;"
    "  selection-color: #ffffff;"
    "}";

const char* kFieldInactiveStyle =
    "QLineEdit {"
    "  background-color: #a8a8a8;"
    "  color: #1e1e1e;"
    "  border: 1px solid #5a5a5a;"
    "  padding: 2px 4px;"
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

DynamicPointInputWidget::DynamicPointInputWidget(QWidget* parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_StyledBackground, true);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setAutoFillBackground(false);

    label_ = new QLabel(tr("指定起点:"), this);
    label_->setStyleSheet(kLabelStyle);
    label_->setAlignment(Qt::AlignCenter);

    xEdit_ = new QLineEdit(this);
    yEdit_ = new QLineEdit(this);
    xEdit_->setFixedWidth(88);
    yEdit_->setFixedWidth(88);
    xEdit_->setFixedHeight(24);
    yEdit_->setFixedHeight(24);
    label_->setFixedHeight(24);
    xEdit_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    yEdit_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);
    layout->addWidget(label_);
    layout->addWidget(xEdit_);
    layout->addWidget(yEdit_);

    xEdit_->installEventFilter(this);
    yEdit_->installEventFilter(this);

    connect(xEdit_, &QLineEdit::textEdited, this, [this]() {
        coordinateInputLocked_ = true;
        SetActiveAxis(true);
        if (onValueChanged) {
            onValueChanged();
        }
    });
    connect(yEdit_, &QLineEdit::textEdited, this, [this]() {
        coordinateInputLocked_ = true;
        SetActiveAxis(false);
        if (onValueChanged) {
            onValueChanged();
        }
    });
    connect(xEdit_, &QLineEdit::editingFinished, this, [this]() {
        if (onValueChanged) {
            onValueChanged();
        }
    });
    connect(yEdit_, &QLineEdit::editingFinished, this, [this]() {
        if (onValueChanged) {
            onValueChanged();
        }
    });

    SetActiveAxis(true);
}

void DynamicPointInputWidget::SetPrompt(const QString& prompt)
{
    QString text = prompt.trimmed();
    if (text.isEmpty()) {
        text = tr("指定点:");
    }
    if (!text.endsWith(QLatin1Char(':')) && !text.endsWith(QStringLiteral("或"))) {
        text += QLatin1Char(':');
    }
    label_->setText(text);
}

QLineEdit* DynamicPointInputWidget::ActiveLineEdit() const
{
    return activeX_ ? xEdit_ : yEdit_;
}

QLineEdit* DynamicPointInputWidget::InactiveLineEdit() const
{
    return activeX_ ? yEdit_ : xEdit_;
}

void DynamicPointInputWidget::SelectActiveText()
{
    if (coordinateInputLocked_) {
        return;
    }

    QLineEdit* active = ActiveLineEdit();
    QLineEdit* inactive = InactiveLineEdit();
    inactive->deselect();
    inactive->setCursorPosition(inactive->text().size());

    if (QWidget::focusWidget() != active) {
        active->setFocus(Qt::OtherFocusReason);
    }
    active->selectAll();
}

void DynamicPointInputWidget::SetModelPoint(const GeoPoint& point)
{
    const QSignalBlocker blockX(xEdit_);
    const QSignalBlocker blockY(yEdit_);
    xEdit_->setText(FormatCoord(point.X()));
    yEdit_->setText(FormatCoord(point.Y()));
    RefreshActiveStyle();
    SelectActiveText();
}

GeoPoint DynamicPointInputWidget::ModelPoint() const
{
    double x = 0.0;
    double y = 0.0;
    ParseCoordinate(xEdit_->text(), x);
    ParseCoordinate(yEdit_->text(), y);
    return GeoPoint(x, y);
}

void DynamicPointInputWidget::MoveToViewPosition(const QPoint& viewPos)
{
    constexpr int kOffsetX = 18;
    constexpr int kOffsetY = 18;
    int x = viewPos.x() + kOffsetX;
    int y = viewPos.y() + kOffsetY;
    if (QWidget* p = parentWidget()) {
        x = std::clamp(x, 0, std::max(0, p->width() - width()));
        y = std::clamp(y, 0, std::max(0, p->height() - height()));
    }
    move(x, y);
}

void DynamicPointInputWidget::MoveToAbsolute(const QPoint& topLeft)
{
    adjustSize();
    int x = topLeft.x();
    int y = topLeft.y();
    if (QWidget* p = parentWidget()) {
        x = std::clamp(x, 0, std::max(0, p->width() - width()));
        y = std::clamp(y, 0, std::max(0, p->height() - height()));
    }
    move(x, y);
}

void DynamicPointInputWidget::SetPromptVisible(bool visible)
{
    if (label_->isVisible() == visible) {
        return;
    }
    label_->setVisible(visible);
    adjustSize();
}

void DynamicPointInputWidget::SetFieldsClickable(bool clickable)
{
    if (xEdit_) {
        xEdit_->setAttribute(Qt::WA_TransparentForMouseEvents, !clickable);
        xEdit_->setReadOnly(!clickable);
    }
    if (yEdit_) {
        yEdit_->setAttribute(Qt::WA_TransparentForMouseEvents, !clickable);
        yEdit_->setReadOnly(!clickable);
    }
}

void DynamicPointInputWidget::FocusX()
{
    SetActiveAxis(true);
    xEdit_->setFocus(Qt::OtherFocusReason);
    SelectActiveText();
}

void DynamicPointInputWidget::FocusY()
{
    SetActiveAxis(false);
    yEdit_->setFocus(Qt::OtherFocusReason);
    SelectActiveText();
}

void DynamicPointInputWidget::ToggleFocus()
{
    if (activeX_) {
        FocusY();
    } else {
        FocusX();
    }
}

void DynamicPointInputWidget::BeginTyping(const QString& firstChar)
{
    coordinateInputLocked_ = true;
    activeX_ = true;
    RefreshActiveStyle();
    xEdit_->setFocus(Qt::OtherFocusReason);
    xEdit_->setText(firstChar);
    xEdit_->setCursorPosition(firstChar.size());
    if (onValueChanged) {
        onValueChanged();
    }
}

bool DynamicPointInputWidget::IsCoordinateInputLocked() const
{
    return coordinateInputLocked_;
}

void DynamicPointInputWidget::ResetCoordinateInputLock()
{
    coordinateInputLocked_ = false;
}

void DynamicPointInputWidget::SetActiveAxis(bool xAxis)
{
    activeX_ = xAxis;
    RefreshActiveStyle();
    SelectActiveText();
}

void DynamicPointInputWidget::RefreshActiveStyle()
{
    ApplyFieldStyle(xEdit_, activeX_);
    ApplyFieldStyle(yEdit_, !activeX_);
}

bool DynamicPointInputWidget::ParseCoordinate(const QString& text, double& value) const
{
    bool ok = false;
    const double parsed = text.trimmed().toDouble(&ok);
    if (!ok) {
        return false;
    }
    value = parsed;
    return true;
}

bool DynamicPointInputWidget::eventFilter(QObject* watched, QEvent* event)
{
    const bool onX = watched == xEdit_;
    const bool onY = watched == yEdit_;
    if (onX || onY) {
        if (event->type() == QEvent::FocusIn) {
            SetActiveAxis(onX);
            return false;
        }
        if (event->type() == QEvent::MouseButtonPress && !coordinateInputLocked_) {
            QTimer::singleShot(0, this, [this]() { SelectActiveText(); });
            return false;
        }
    }

    if (event->type() != QEvent::KeyPress) {
        return QWidget::eventFilter(watched, event);
    }

    auto* keyEvent = static_cast<QKeyEvent*>(event);
    if (!onX && !onY) {
        return QWidget::eventFilter(watched, event);
    }

    if (keyEvent->key() == Qt::Key_Tab || keyEvent->key() == Qt::Key_Backtab) {
        const bool shiftTab = keyEvent->key() == Qt::Key_Backtab
            || (keyEvent->modifiers() & Qt::ShiftModifier);
        if (shiftTab) {
            if (onY) {
                FocusX();
            }
        } else if (onX) {
            FocusY();
        } else if (onY) {
            if (onConfirmed) {
                onConfirmed();
            }
        }
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
