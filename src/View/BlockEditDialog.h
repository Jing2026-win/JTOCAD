#pragma once

#include "DataModel/BlockDefinition.h"
#include <QDialog>
#include <string>

class QCheckBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;

namespace cad {

class CadDocument;

struct BlockEditDialogResult {
    std::string newName;
    double baseX = 0.0;
    double baseY = 0.0;
    bool allowExplode = true;
    bool uniformScale = true;
    bool redefineFromSelection = false;
};

class BlockEditDialog : public QDialog {
public:
    BlockEditDialog(CadDocument& document, const std::string& blockName, QWidget* parent = nullptr);

    BlockEditDialogResult Result() const;
    bool WantsRedefineFromSelection() const { return redefineRequested_; }

private:
    CadDocument& document_;
    std::string originalName_;
    bool redefineRequested_ = false;

    QLineEdit* nameEdit_ = nullptr;
    QDoubleSpinBox* baseXSpin_ = nullptr;
    QDoubleSpinBox* baseYSpin_ = nullptr;
    QCheckBox* allowExplodeCheck_ = nullptr;
    QCheckBox* uniformScaleCheck_ = nullptr;
    QLabel* entityCountLabel_ = nullptr;
};

} // namespace cad
