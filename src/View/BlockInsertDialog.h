#pragma once

#include "DataModel/BlockDefinition.h"
#include <QDialog>
#include <string>
#include <vector>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;

namespace cad {

class CadDocument;

struct BlockInsertDialogResult {
    std::string blockName;
    double scale = 1.0;
    double rotationDegrees = 0.0;
    bool explodeOnInsert = false;
    std::vector<BlockAttributeValue> attributes;
};

class BlockInsertDialog : public QDialog {
public:
    BlockInsertDialog(CadDocument& document, QWidget* parent = nullptr);

    void SelectBlock(const QString& name);

    BlockInsertDialogResult Result() const;

private:
    void OnBlockChanged(const QString& name);

    CadDocument& document_;
    QComboBox* blockCombo_ = nullptr;
    QDoubleSpinBox* scaleSpin_ = nullptr;
    QDoubleSpinBox* rotationSpin_ = nullptr;
    QCheckBox* explodeCheck_ = nullptr;
};

} // namespace cad
