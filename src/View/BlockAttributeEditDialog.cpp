#include "View/BlockAttributeEditDialog.h"

#include "DataModel/BlockReferenceEntity.h"
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QVBoxLayout>

namespace cad {

BlockAttributeEditDialog::BlockAttributeEditDialog(
    BlockReferenceEntity& reference,
    const BlockDefinition* definition,
    QWidget* parent)
    : QDialog(parent)
    , reference_(reference)
    , definition_(definition)
{
    setWindowTitle(tr("编辑属性 - %1").arg(QString::fromStdString(reference.BlockName())));
    setModal(true);
    resize(380, 280);

    auto* root = new QVBoxLayout(this);
    form_ = new QFormLayout();

    if (definition_ && !definition_->attributeDefs.empty()) {
        for (const BlockAttributeDef& attrDef : definition_->attributeDefs) {
            if (attrDef.constant) {
                continue;
            }
            auto* edit = new QLineEdit(this);
            std::string value = attrDef.defaultValue;
            for (const BlockAttributeValue& av : reference_.AttributeValues()) {
                if (av.tag == attrDef.tag) {
                    value = av.value;
                    break;
                }
            }
            edit->setText(QString::fromStdString(value));
            const QString label = attrDef.prompt.empty()
                ? QString::fromStdString(attrDef.tag)
                : QString::fromStdString(attrDef.prompt);
            form_->addRow(label, edit);
            valueEdits_.push_back(edit);
            tags_.push_back(attrDef.tag);
        }
    } else {
        for (const BlockAttributeValue& av : reference_.AttributeValues()) {
            auto* edit = new QLineEdit(this);
            edit->setText(QString::fromStdString(av.value));
            form_->addRow(QString::fromStdString(av.tag), edit);
            valueEdits_.push_back(edit);
            tags_.push_back(av.tag);
        }
    }

    if (valueEdits_.empty()) {
        root->addWidget(new QLabel(tr("该块没有可编辑属性。"), this));
    } else {
        root->addLayout(form_);
    }

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    root->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

std::vector<BlockAttributeValue> BlockAttributeEditDialog::Result() const
{
    std::vector<BlockAttributeValue> values;
    values.reserve(tags_.size());
    for (std::size_t i = 0; i < tags_.size(); ++i) {
        values.push_back({tags_[i], valueEdits_[i]->text().toStdString()});
    }
    return values;
}

} // namespace cad
