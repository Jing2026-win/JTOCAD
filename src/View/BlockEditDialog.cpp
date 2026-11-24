#include "View/BlockEditDialog.h"

#include "DataModel/CadDocument.h"
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

namespace cad {

BlockEditDialog::BlockEditDialog(CadDocument& document, const std::string& blockName, QWidget* parent)
    : QDialog(parent)
    , document_(document)
    , originalName_(blockName)
{
    setWindowTitle(tr("块编辑器 - %1").arg(QString::fromStdString(blockName)));
    setModal(true);
    resize(400, 300);

    const BlockDefinition* def = document_.FindBlockDefinition(blockName);

    auto* root = new QVBoxLayout(this);
    auto* form = new QFormLayout();

    nameEdit_ = new QLineEdit(this);
    nameEdit_->setText(QString::fromStdString(blockName));
    form->addRow(tr("名称"), nameEdit_);

    baseXSpin_ = new QDoubleSpinBox(this);
    baseXSpin_->setRange(-1.0e9, 1.0e9);
    baseXSpin_->setDecimals(4);
    baseXSpin_->setValue(def ? def->baseX : 0.0);
    form->addRow(tr("基点 X"), baseXSpin_);

    baseYSpin_ = new QDoubleSpinBox(this);
    baseYSpin_->setRange(-1.0e9, 1.0e9);
    baseYSpin_->setDecimals(4);
    baseYSpin_->setValue(def ? def->baseY : 0.0);
    form->addRow(tr("基点 Y"), baseYSpin_);

    allowExplodeCheck_ = new QCheckBox(tr("允许分解"), this);
    allowExplodeCheck_->setChecked(def ? def->allowExplode : true);
    form->addRow(QString(), allowExplodeCheck_);

    uniformScaleCheck_ = new QCheckBox(tr("按统一比例缩放"), this);
    uniformScaleCheck_->setChecked(def ? def->uniformScale : true);
    form->addRow(QString(), uniformScaleCheck_);

    const int entityCount = def ? static_cast<int>(def->entities.size()) : 0;
    entityCountLabel_ = new QLabel(tr("块内图元数量：%1").arg(entityCount), this);
    form->addRow(QString(), entityCountLabel_);

    root->addLayout(form);

    auto* redefineButton = new QPushButton(tr("用当前选择重定义块"), this);
    connect(redefineButton, &QPushButton::clicked, this, [this]() {
        redefineRequested_ = true;
        accept();
    });
    root->addWidget(redefineButton);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    root->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
        if (nameEdit_->text().trimmed().isEmpty()) {
            QMessageBox::warning(this, tr("块编辑"), tr("块名称不能为空。"));
            return;
        }
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

BlockEditDialogResult BlockEditDialog::Result() const
{
    BlockEditDialogResult result;
    result.newName = nameEdit_->text().trimmed().toStdString();
    result.baseX = baseXSpin_->value();
    result.baseY = baseYSpin_->value();
    result.allowExplode = allowExplodeCheck_->isChecked();
    result.uniformScale = uniformScaleCheck_->isChecked();
    result.redefineFromSelection = redefineRequested_;
    return result;
}

} // namespace cad
