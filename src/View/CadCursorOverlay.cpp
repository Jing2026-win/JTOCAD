#include "View/CadCursorOverlay.h"

#include <QPainter>
#include <QtMath>

namespace cad {

CadCursorOverlay::CadCursorOverlay(QWidget* parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_NoSystemBackground);
    setAutoFillBackground(false);
}

void CadCursorOverlay::SetCursorPosition(const QPoint& viewportPos)
{
    cursorPos_ = viewportPos;
    // Always repaint: callers (e.g. wheel zoom) may pass the same pixel
    // position after the viewport beneath us has been redrawn, and we must
    // redraw the crosshair on top or it disappears (system cursor is blank).
    update();
}

void CadCursorOverlay::SetDisplay(CadCursorDisplay display)
{
    const bool wasVisible = IsVisibleDisplay(display_);
    const bool nowVisible = IsVisibleDisplay(display);
    if (display_ == display && wasVisible == nowVisible) {
        return;
    }
    display_ = display;
    setVisible(nowVisible);
    if (wasVisible != nowVisible) {
        if (QWidget* parent = parentWidget()) {
            parent->update();
        }
    }
    update();
}

void CadCursorOverlay::SetSettings(const CursorSettings& settings)
{
    settings_ = settings;
    update();
}

bool CadCursorOverlay::IsVisibleDisplay(CadCursorDisplay display) const
{
    return display != CadCursorDisplay::Hidden && display != CadCursorDisplay::SystemOnly;
}

int CadCursorOverlay::CrosshairHalfLength() const
{
    if (settings_.crosshairSizePercent >= 100) {
        return qMax(width(), height());
    }
    const int reference = qMin(width(), height());
    return qMax(1, reference * settings_.crosshairSizePercent / 200);
}

void CadCursorOverlay::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, false);

    // widget 设了 WA_NoSystemBackground + WA_TranslucentBackground + 不自动填充，
    // Qt 不会在重绘前清除旧像素，全屏十字线移动后会留下残影（L 形/横线）。
    // 这里显式用 CompositionMode_Clear 把整个 widget 擦成完全透明，再画新内容。
    painter.setCompositionMode(QPainter::CompositionMode_Clear);
    painter.fillRect(rect(), Qt::transparent);
    painter.setCompositionMode(QPainter::CompositionMode_SourceOver);

    if (!IsVisibleDisplay(display_)) {
        return;
    }

    const QPoint center = cursorPos_;
    const QColor color = settings_.crosshairColor;

    const bool showCrosshair = display_ == CadCursorDisplay::CrosshairOnly
        || display_ == CadCursorDisplay::CrosshairWithPickbox
        || display_ == CadCursorDisplay::CrosshairWithAperture;
    const bool showPickbox = display_ == CadCursorDisplay::CrosshairWithPickbox
        || display_ == CadCursorDisplay::PickboxOnly;
    const bool showAperture = display_ == CadCursorDisplay::CrosshairWithAperture
        && settings_.showAutoSnapAperture;

    if (showCrosshair && settings_.cursorType == 0) {
        QPen crossPen(color, 1.0);
        painter.setPen(crossPen);
        if (settings_.crosshairSizePercent >= 100) {
            painter.drawLine(center.x(), 0, center.x(), height());
            painter.drawLine(0, center.y(), width(), center.y());
        } else {
            const int halfLen = CrosshairHalfLength();
            painter.drawLine(center.x() - halfLen, center.y(), center.x() + halfLen, center.y());
            painter.drawLine(center.x(), center.y() - halfLen, center.x(), center.y() + halfLen);
        }
    }

    if (showPickbox) {
        const int size = qMax(2, settings_.pickboxPixels);
        const int half = size / 2;
        QPen pickPen(color, 1.0);
        painter.setPen(pickPen);
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(center.x() - half, center.y() - half, size, size);
    }

    if (showAperture) {
        const int size = qMax(4, settings_.aperturePixels);
        const int half = size / 2;
        QPen aperturePen(color, 1.0, Qt::DashLine);
        painter.setPen(aperturePen);
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(center.x() - half, center.y() - half, size, size);
    }
}

} // namespace cad
