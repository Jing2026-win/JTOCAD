#pragma once

#include "DataModel/BlockDefinition.h"
#include <QDialog>
#include <vector>

class QFormLayout;
class QLineEdit;

namespace cad {

class BlockReferenceEntity;

class BlockAttributeEditDialog : public QDialog {
public:
    BlockAttributeEditDialog(BlockReferenceEntity& reference, const BlockDefinition* definition, QWidget* parent = nullptr);

    std::vector<BlockAttributeValue> Result() const;

private:
    BlockReferenceEntity& reference_;
    const BlockDefinition* definition_ = nullptr;
    QFormLayout* form_ = nullptr;
    std::vector<QLineEdit*> valueEdits_;
    std::vector<std::string> tags_;
};

} // namespace cad
