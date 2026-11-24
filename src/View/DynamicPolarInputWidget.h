#pragma once

#include <QWidget>
#include <functional>

class QLabel;
class QLineEdit;
class QKeyEvent;

namespace cad {

// AutoCAD-style dynamic input (DYNINPUT) for LINE:
// length dimension near mid-segment, angle + Tab near the cursor.
class DynamicPolarInputWidget : public QWidget {
public:
    explicit DynamicPolarInputWidget(QWidget* parent = nullptr);

    void SetLengthAngle(double length, double angleDegrees);
    double Length() const;
    double AngleDegrees() const;

    // When false, only the angle field is shown (e.g. arc end-direction pick).
    void SetLengthVisible(bool visible);
    bool IsLengthVisible() const { return lengthVisible_; }
    // When false, only the length field is shown (e.g. arc chord-length pick).
    void SetAngleVisible(bool visible);
    bool IsAngleVisible() const { return angleVisible_; }

    // Place length below mid-segment (screen-down), angle+Tab at cursor.
    void MoveToLinePositions(const QPoint& lengthViewPos, const QPoint& angleViewPos);
    // AutoCAD SPLINE: length + angle in one row after the prompt HUD.
    void MoveToHorizontalRow(const QPoint& topLeft);
    void SetPolarAnglePrefix(bool enabled);
    void SetTabHintVisible(bool visible);
    void FocusLength();
    void FocusAngle();
    void ToggleFocus();
    void ResetLocks();
    void BeginTyping(const QString& firstChar);
    bool IsLengthLocked() const { return lengthVisible_ && lengthLocked_; }
    bool IsAngleLocked() const { return angleVisible_ && angleLocked_; }
    bool IsLengthActive() const { return lengthVisible_ && lengthActive_; }
    void SetFieldsClickable(bool clickable);

    std::function<void()> onValueChanged;
    std::function<void()> onConfirmed;
    std::function<void()> onCancelRequested;
    // Return true to consume a key that would otherwise go into the length/angle field.
    std::function<bool(QKeyEvent*)> onInterceptKey;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    QLineEdit* ActiveEdit() const;
    void RefreshActiveStyle();
    void SelectActiveText();
    bool ParseNumber(const QString& text, double& value) const;
    void SyncOverlaySize();

    QLineEdit* lengthEdit_ = nullptr;
    QLineEdit* angleEdit_ = nullptr;
    QLabel* tabHint_ = nullptr;
    bool lengthVisible_ = true;
    bool angleVisible_ = true;
    bool lengthActive_ = true;
    bool lengthLocked_ = false;
    bool angleLocked_ = false;
    bool polarAnglePrefix_ = false;
    bool tabHintEnabled_ = true;
};

} // namespace cad
