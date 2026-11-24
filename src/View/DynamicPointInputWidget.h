#pragma once

#include "Geometry/GeoPoint.h"
#include <QWidget>
#include <functional>

class QLabel;
class QLineEdit;

namespace cad {

// AutoCAD-style dynamic input for absolute X/Y (first point / insert point).
class DynamicPointInputWidget : public QWidget {
public:
    explicit DynamicPointInputWidget(QWidget* parent = nullptr);

    void SetPrompt(const QString& prompt);
    void SetModelPoint(const GeoPoint& point);
    GeoPoint ModelPoint() const;

    void MoveToViewPosition(const QPoint& viewPos);
    void MoveToAbsolute(const QPoint& topLeft);
    void SetPromptVisible(bool visible);
    void SetFieldsClickable(bool clickable);
    void SetActiveAxis(bool xAxis);
    void FocusX();
    void FocusY();
    void ToggleFocus();
    void BeginTyping(const QString& firstChar);
    void ResetCoordinateInputLock();
    bool IsCoordinateInputLocked() const;

    std::function<void()> onValueChanged;
    std::function<void()> onConfirmed;
    std::function<void()> onCancelRequested;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    QLineEdit* ActiveLineEdit() const;
    QLineEdit* InactiveLineEdit() const;
    void RefreshActiveStyle();
    void SelectActiveText();
    bool ParseCoordinate(const QString& text, double& value) const;

    QLabel* label_ = nullptr;
    QLineEdit* xEdit_ = nullptr;
    QLineEdit* yEdit_ = nullptr;
    bool activeX_ = true;
    bool coordinateInputLocked_ = false;
};

} // namespace cad
