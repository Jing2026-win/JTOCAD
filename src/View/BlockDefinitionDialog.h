#pragma once

#include "DataModel/BlockDefinition.h"
#include <QDialog>
#include <string>
#include <vector>

class QCheckBox;
class QDoubleSpinBox;
class QLineEdit;
class QListWidget;
class QRadioButton;

namespace cad {

struct BlockDefinitionDialogResult {
    std::string name;
    double baseX = 0.0;
    double baseY = 0.0;
    BlockCreateMode mode = BlockCreateMode::Convert;
    bool allowExplode = true;
    bool uniformScale = true;
    bool annotative = false;
    std::vector<BlockAttributeDef> attributeDefs;
};

class BlockDefinitionDialog : public QDialog {
public:
    explicit BlockDefinitionDialog(QWidget* parent = nullptr);

    void SetSuggestedName(const QString& name);
    void SetBasePoint(double x, double y);
    BlockDefinitionDialogResult Result() const;

private:
    void OnAddAttribute();
    void OnRemoveAttribute();

    QLineEdit* nameEdit_ = nullptr;
    QDoubleSpinBox* baseXSpin_ = nullptr;
    QDoubleSpinBox* baseYSpin_ = nullptr;
    QRadioButton* keepRadio_ = nullptr;
    QRadioButton* convertRadio_ = nullptr;
    QRadioButton* deleteRadio_ = nullptr;
    QCheckBox* allowExplodeCheck_ = nullptr;
    QCheckBox* uniformScaleCheck_ = nullptr;
    QCheckBox* annotativeCheck_ = nullptr;
    QListWidget* attributeList_ = nullptr;
    QLineEdit* attrTagEdit_ = nullptr;
    QLineEdit* attrPromptEdit_ = nullptr;
    QLineEdit* attrDefaultEdit_ = nullptr;
};

} // namespace cad
