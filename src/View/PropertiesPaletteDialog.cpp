#include "View/PropertiesPaletteDialog.h"

#include "View/LineWeightUtil.h"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace cad {
namespace {

QString SourceText(PropertySource source, const QString& overrideText)
{
    if (source == PropertySource::ByLayer) {
        return QStringLiteral("ByLayer");
    }
    if (source == PropertySource::ByBlock) {
        return QStringLiteral("ByBlock");
    }
    return overrideText;
}

} // namespace

PropertiesPaletteDialog::PropertiesPaletteDialog(CadDocument& document, QWidget* parent)
    : QDialog(parent)
    , document_(document)
{
    setWindowTitle(tr("特性"));
    setMinimumWidth(320);
    setStyleSheet(QStringLiteral(
        "QDialog { background-color: #2d2d30; color: #f3f3f3; }"
        "QLabel { color: #f3f3f3; }"
        "QPushButton { background-color: #3e3e42; color: #f3f3f3; border: 1px solid #555555; padding: 4px 10px; }"
        "QPushButton:hover { background-color: #094771; }"));

    auto* root = new QVBoxLayout(this);
    countLabel_ = new QLabel(this);
    root->addWidget(countLabel_);

    auto* form = new QFormLayout();
    layerLabel_ = new QLabel(this);
    colorLabel_ = new QLabel(this);
    lineTypeLabel_ = new QLabel(this);
    lineWeightLabel_ = new QLabel(this);
    form->addRow(tr("图层"), layerLabel_);
    form->addRow(tr("颜色"), colorLabel_);
    form->addRow(tr("线型"), lineTypeLabel_);
    form->addRow(tr("线宽"), lineWeightLabel_);
    root->addLayout(form);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    buttons->button(QDialogButtonBox::Close)->setText(tr("关闭"));
    root->addWidget(buttons);

    Refresh();
}

void PropertiesPaletteDialog::Refresh()
{
    const std::vector<EntityId> selected = document_.SelectedEntityIds();
    if (selected.empty()) {
        countLabel_->setText(tr("未选中对象 — 显示当前绘图默认属性"));
        const EntityStyle& style = document_.CurrentEntityStyle();
        layerLabel_->setText(QString::fromUtf8(document_.CurrentLayerName().c_str()));
        colorLabel_->setText(SourceText(style.colorSource, QStringLiteral("自定义")));
        lineTypeLabel_->setText(SourceText(
            style.lineTypeSource,
            QString::fromUtf8(style.lineTypeName.c_str())));
        lineWeightLabel_->setText(SourceText(
            style.lineWeightSource,
            LineWeightDialogLabel(style.lineWidth)));
        return;
    }

    countLabel_->setText(tr("已选中 %1 个对象").arg(selected.size()));
    if (const Entity* entity = document_.FindEntity(selected.front())) {
        const EntityStyle& style = entity->Style();
        layerLabel_->setText(QString::fromUtf8(entity->LayerName().c_str()));
        colorLabel_->setText(SourceText(style.colorSource, QStringLiteral("自定义")));
        lineTypeLabel_->setText(SourceText(
            style.lineTypeSource,
            QString::fromUtf8(style.lineTypeName.empty()
                ? LineTypeNameFromPattern(style.linePattern).c_str()
                : style.lineTypeName.c_str())));
        lineWeightLabel_->setText(SourceText(
            style.lineWeightSource,
            LineWeightDialogLabel(style.lineWidth)));
    }
}

} // namespace cad
