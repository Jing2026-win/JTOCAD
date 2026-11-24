#pragma once

#include "DataModel/CadDocument.h"
#include <QDialog>

class QFormLayout;
class QLabel;

namespace cad {

// Lightweight AutoCAD-style Properties palette (Ctrl+1 / PR).
class PropertiesPaletteDialog : public QDialog {
public:
    explicit PropertiesPaletteDialog(CadDocument& document, QWidget* parent = nullptr);

private:
    void Refresh();

    CadDocument& document_;
    QLabel* countLabel_ = nullptr;
    QLabel* layerLabel_ = nullptr;
    QLabel* colorLabel_ = nullptr;
    QLabel* lineTypeLabel_ = nullptr;
    QLabel* lineWeightLabel_ = nullptr;
};

} // namespace cad
