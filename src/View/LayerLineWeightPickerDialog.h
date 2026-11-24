#pragma once

#include <QDialog>

class QLabel;
class QListWidget;

namespace cad {

class LayerLineWeightPickerDialog : public QDialog {
public:
    explicit LayerLineWeightPickerDialog(double initialLineWeightMm, QWidget* parent = nullptr);

    double selectedLineWeightMm() const { return selectedLineWeightMm_; }

private:
    void Populate();
    void AcceptSelection();
    void UpdateNewLabel();

    QListWidget* list_ = nullptr;
    QLabel* oldLabel_ = nullptr;
    QLabel* newLabel_ = nullptr;
    double initialLineWeightMm_ = 0.0;
    double selectedLineWeightMm_ = 0.0;
};

} // namespace cad
