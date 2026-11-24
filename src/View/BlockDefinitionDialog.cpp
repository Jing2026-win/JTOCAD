#include "View/BlockDefinitionDialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QVBoxLayout>

namespace cad {

BlockDefinitionDialog::BlockDefinitionDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("块定义"));
    setModal(true);
    resize(460, 520);

    auto* root = new QVBoxLayout(this);

    auto* form = new QFormLayout();
    nameEdit_ = new QLineEdit(this);
    nameEdit_->setPlaceholderText(tr("块名称"));
    form->addRow(tr("名称"), nameEdit_);

    baseXSpin_ = new QDoubleSpinBox(this);
    baseXSpin_->setRange(-1.0e9, 1.0e9);
    baseXSpin_->setDecimals(4);
    form->addRow(tr("基点 X"), baseXSpin_);

    baseYSpin_ = new QDoubleSpinBox(this);
    baseYSpin_->setRange(-1.0e9, 1.0e9);
    baseYSpin_->setDecimals(4);
    form->addRow(tr("基点 Y"), baseYSpin_);
    root->addLayout(form);

    auto* modeGroup = new QGroupBox(tr("对象"), this);
    auto* modeLayout = new QVBoxLayout(modeGroup);
    keepRadio_ = new QRadioButton(tr("保留"), modeGroup);
    convertRadio_ = new QRadioButton(tr("转换为块"), modeGroup);
    deleteRadio_ = new QRadioButton(tr("删除"), modeGroup);
    convertRadio_->setChecked(true);
    modeLayout->addWidget(keepRadio_);
    modeLayout->addWidget(convertRadio_);
    modeLayout->addWidget(deleteRadio_);
    root->addWidget(modeGroup);

    auto* behaviorGroup = new QGroupBox(tr("行为"), this);
    auto* behaviorLayout = new QVBoxLayout(behaviorGroup);
    allowExplodeCheck_ = new QCheckBox(tr("允许分解"), behaviorGroup);
    allowExplodeCheck_->setChecked(true);
    uniformScaleCheck_ = new QCheckBox(tr("按统一比例缩放"), behaviorGroup);
    uniformScaleCheck_->setChecked(true);
    annotativeCheck_ = new QCheckBox(tr("注释性"), behaviorGroup);
    behaviorLayout->addWidget(allowExplodeCheck_);
    behaviorLayout->addWidget(uniformScaleCheck_);
    behaviorLayout->addWidget(annotativeCheck_);
    root->addWidget(behaviorGroup);

    auto* attrGroup = new QGroupBox(tr("属性定义（可选）"), this);
    auto* attrLayout = new QVBoxLayout(attrGroup);
    attributeList_ = new QListWidget(attrGroup);
    attrLayout->addWidget(attributeList_);

    auto* attrForm = new QFormLayout();
    attrTagEdit_ = new QLineEdit(attrGroup);
    attrPromptEdit_ = new QLineEdit(attrGroup);
    attrDefaultEdit_ = new QLineEdit(attrGroup);
    attrForm->addRow(tr("标记"), attrTagEdit_);
    attrForm->addRow(tr("提示"), attrPromptEdit_);
    attrForm->addRow(tr("默认"), attrDefaultEdit_);
    attrLayout->addLayout(attrForm);

    auto* attrButtons = new QHBoxLayout();
    auto* addAttr = new QPushButton(tr("添加属性"), attrGroup);
    auto* removeAttr = new QPushButton(tr("删除属性"), attrGroup);
    attrButtons->addWidget(addAttr);
    attrButtons->addWidget(removeAttr);
    attrLayout->addLayout(attrButtons);
    root->addWidget(attrGroup);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    root->addWidget(buttons);

    connect(addAttr, &QPushButton::clicked, this, &BlockDefinitionDialog::OnAddAttribute);
    connect(removeAttr, &QPushButton::clicked, this, &BlockDefinitionDialog::OnRemoveAttribute);
    connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
        if (nameEdit_->text().trimmed().isEmpty()) {
            QMessageBox::warning(this, tr("块定义"), tr("请输入块名称。"));
            return;
        }
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

void BlockDefinitionDialog::SetSuggestedName(const QString& name)
{
    nameEdit_->setText(name);
}

void BlockDefinitionDialog::SetBasePoint(double x, double y)
{
    baseXSpin_->setValue(x);
    baseYSpin_->setValue(y);
}

BlockDefinitionDialogResult BlockDefinitionDialog::Result() const
{
    BlockDefinitionDialogResult result;
    result.name = nameEdit_->text().trimmed().toStdString();
    result.baseX = baseXSpin_->value();
    result.baseY = baseYSpin_->value();
    if (keepRadio_->isChecked()) {
        result.mode = BlockCreateMode::Keep;
    } else if (deleteRadio_->isChecked()) {
        result.mode = BlockCreateMode::Delete;
    } else {
        result.mode = BlockCreateMode::Convert;
    }
    result.allowExplode = allowExplodeCheck_->isChecked();
    result.uniformScale = uniformScaleCheck_->isChecked();
    result.annotative = annotativeCheck_->isChecked();

    for (int i = 0; i < attributeList_->count(); ++i) {
        const QStringList parts = attributeList_->item(i)->text().split(QLatin1Char('|'));
        if (parts.size() < 3) {
            continue;
        }
        BlockAttributeDef def;
        def.tag = parts[0].trimmed().toStdString();
        def.prompt = parts[1].trimmed().toStdString();
        def.defaultValue = parts[2].trimmed().toStdString();
        if (!def.tag.empty()) {
            result.attributeDefs.push_back(std::move(def));
        }
    }
    return result;
}

void BlockDefinitionDialog::OnAddAttribute()
{
    const QString tag = attrTagEdit_->text().trimmed();
    if (tag.isEmpty()) {
        QMessageBox::information(this, tr("属性"), tr("请输入属性标记。"));
        return;
    }
    const QString prompt = attrPromptEdit_->text().trimmed();
    const QString defVal = attrDefaultEdit_->text().trimmed();
    attributeList_->addItem(QStringLiteral("%1 | %2 | %3").arg(tag, prompt, defVal));
    attrTagEdit_->clear();
    attrPromptEdit_->clear();
    attrDefaultEdit_->clear();
}

void BlockDefinitionDialog::OnRemoveAttribute()
{
    qDeleteAll(attributeList_->selectedItems());
}

} // namespace cad
