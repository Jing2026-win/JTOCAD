#include "View/BlockInsertDialog.h"

#include "DataModel/CadDocument.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QMessageBox>
#include <QVBoxLayout>

namespace cad {

BlockInsertDialog::BlockInsertDialog(CadDocument& document, QWidget* parent)
    : QDialog(parent)
    , document_(document)
{
    setWindowTitle(tr("插入块"));
    setModal(true);
    resize(360, 220);

    auto* root = new QVBoxLayout(this);
    auto* form = new QFormLayout();

    blockCombo_ = new QComboBox(this);
    for (const BlockDefinition& block : document_.BlockDefinitions()) {
        blockCombo_->addItem(QString::fromStdString(block.name));
    }
    form->addRow(tr("名称"), blockCombo_);

    scaleSpin_ = new QDoubleSpinBox(this);
    scaleSpin_->setRange(0.001, 1.0e6);
    scaleSpin_->setDecimals(4);
    scaleSpin_->setValue(1.0);
    form->addRow(tr("缩放比例"), scaleSpin_);

    rotationSpin_ = new QDoubleSpinBox(this);
    rotationSpin_->setRange(-3600.0, 3600.0);
    rotationSpin_->setDecimals(2);
    rotationSpin_->setSuffix(QStringLiteral(" °"));
    form->addRow(tr("旋转角度"), rotationSpin_);

    explodeCheck_ = new QCheckBox(tr("插入时分解"), this);
    form->addRow(QString(), explodeCheck_);

    root->addLayout(form);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    root->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
        if (blockCombo_->currentText().trimmed().isEmpty()) {
            QMessageBox::warning(this, tr("插入块"), tr("当前图纸没有可用块定义。"));
            return;
        }
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

BlockInsertDialogResult BlockInsertDialog::Result() const
{
    BlockInsertDialogResult result;
    result.blockName = blockCombo_->currentText().trimmed().toStdString();
    result.scale = scaleSpin_->value();
    result.rotationDegrees = rotationSpin_->value();
    result.explodeOnInsert = explodeCheck_->isChecked();

    if (const BlockDefinition* def = document_.FindBlockDefinition(result.blockName)) {
        for (const BlockAttributeDef& attrDef : def->attributeDefs) {
            result.attributes.push_back({attrDef.tag, attrDef.defaultValue});
        }
    }
    return result;
}

void BlockInsertDialog::SelectBlock(const QString& name)
{
    const int index = blockCombo_->findText(name);
    if (index >= 0) {
        blockCombo_->setCurrentIndex(index);
    }
}

void BlockInsertDialog::OnBlockChanged(const QString&)
{
}

} // namespace cad
