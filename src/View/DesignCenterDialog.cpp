#include "View/DesignCenterDialog.h"

#include "Command/CommandManager.h"
#include "DataModel/BlockReferenceEntity.h"
#include "DataModel/Entity.h"
#include "IO/DxfService.h"
#include "View/CadView.h"
#include "View/BlockInsertDialog.h"

#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

namespace cad {

DesignCenterDialog::DesignCenterDialog(
    CadDocument& document,
    CommandManager& commandManager,
    CadView* view,
    QWidget* parent)
    : QDialog(parent)
    , document_(document)
    , commandManager_(commandManager)
    , view_(view)
{
    setWindowTitle(QStringLiteral("设计中心 (ADC)"));
    setModal(false);
    resize(420, 520);
    setAttribute(Qt::WA_DeleteOnClose);
    setStyleSheet(QStringLiteral(
        "QDialog { background-color: #2d2d30; color: #f0f0f0; }"
        "QLabel, QListWidget, QLineEdit { color: #f0f0f0; background-color: #252526; }"
        "QListWidget::item:selected { background-color: #094771; }"
        "QPushButton { background-color: #3e3e42; color: #f0f0f0; border: 1px solid #555; padding: 5px 10px; }"
        "QPushButton:hover { background-color: #094771; }"));

    auto* root = new QVBoxLayout(this);
    auto* pathRow = new QHBoxLayout();
    pathEdit_ = new QLineEdit(this);
    pathEdit_->setReadOnly(true);
    pathEdit_->setPlaceholderText(QStringLiteral("选择含块定义的 DXF 文件…"));
    auto* browseButton = new QPushButton(QStringLiteral("浏览…"), this);
    pathRow->addWidget(pathEdit_, 1);
    pathRow->addWidget(browseButton);
    root->addLayout(pathRow);

    blockList_ = new QListWidget(this);
    root->addWidget(blockList_, 1);

    auto* buttonRow = new QHBoxLayout();
    auto* insertButton = new QPushButton(QStringLiteral("插入"), this);
    auto* redefineButton = new QPushButton(QStringLiteral("仅重定义"), this);
    auto* insertRedefineButton = new QPushButton(QStringLiteral("插入并重定义"), this);
    buttonRow->addWidget(insertButton);
    buttonRow->addWidget(redefineButton);
    buttonRow->addWidget(insertRedefineButton);
    root->addLayout(buttonRow);

    statusLabel_ = new QLabel(QStringLiteral("Ctrl+2 设计中心：从外部图纸拖入/重定义块。"), this);
    root->addWidget(statusLabel_);

    connect(browseButton, &QPushButton::clicked, this, [this]() { BrowseFile(); });
    connect(insertButton, &QPushButton::clicked, this, [this]() { InsertSelected(); });
    connect(redefineButton, &QPushButton::clicked, this, [this]() { RedefineSelected(); });
    connect(insertRedefineButton, &QPushButton::clicked, this, [this]() {
        RedefineSelected();
        InsertSelected();
    });
    connect(blockList_, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem*) {
        OnItemActivated();
    });
}

void DesignCenterDialog::BrowseFile()
{
    const QString fileName = QFileDialog::getOpenFileName(
        this,
        QStringLiteral("打开图纸（设计中心）"),
        QString(),
        QStringLiteral("DXF 文件 (*.dxf)"));
    if (fileName.isEmpty()) {
        return;
    }
    sourceDocument_.Clear();
    if (!DxfService::ImportFile(fileName.toStdString(), sourceDocument_)) {
        QMessageBox::warning(this, QStringLiteral("设计中心"), QStringLiteral("无法读取该 DXF。"));
        return;
    }
    sourcePath_ = fileName;
    pathEdit_->setText(fileName);
    ReloadBlocks();
}

void DesignCenterDialog::ReloadBlocks()
{
    blockList_->clear();
    for (const BlockDefinition& def : sourceDocument_.BlockDefinitions()) {
        auto* item = new QListWidgetItem(QString::fromStdString(def.name), blockList_);
        item->setData(Qt::UserRole, QString::fromStdString(def.name));
        item->setToolTip(
            QStringLiteral("实体数: %1  基点: (%2, %3)")
                .arg(def.entities.size())
                .arg(def.baseX, 0, 'f', 3)
                .arg(def.baseY, 0, 'f', 3));
    }
    statusLabel_->setText(
        QStringLiteral("共 %1 个块定义。双击或点插入。").arg(blockList_->count()));
}

void DesignCenterDialog::RedefineSelected()
{
    const auto* item = blockList_->currentItem();
    if (!item) {
        QMessageBox::information(this, QStringLiteral("设计中心"), QStringLiteral("请先选择一个块。"));
        return;
    }
    const std::string name = item->data(Qt::UserRole).toString().toStdString();
    const BlockDefinition* src = sourceDocument_.FindBlockDefinition(name);
    if (!src) {
        return;
    }

    BlockDefinition copy = *src;
    // Deep-clone entities so host owns independent copies.
    copy.entities.clear();
    for (const std::shared_ptr<Entity>& e : src->entities) {
        if (!e) {
            continue;
        }
        std::unique_ptr<Entity> clone = e->Clone();
        if (!clone) {
            continue;
        }
        copy.entities.push_back(std::shared_ptr<Entity>(std::move(clone)));
    }
    // Legacy WBLOCK files stored geometry only in ENTITIES with an empty BLOCK shell.
    if (copy.entities.empty()) {
        for (const auto& e : sourceDocument_.Entities()) {
            if (!e || e->Type() == EntityType::BlockReference) {
                continue;
            }
            std::unique_ptr<Entity> clone = e->Clone();
            if (!clone) {
                continue;
            }
            clone->MoveBy(-copy.baseX, -copy.baseY);
            copy.entities.push_back(std::shared_ptr<Entity>(std::move(clone)));
        }
    }
    document_.AddBlockDefinition(std::move(copy));
    document_.RefreshBlockReferenceCaches(name);
    statusLabel_->setText(QStringLiteral("已重定义块「%1」。").arg(QString::fromStdString(name)));
}

void DesignCenterDialog::InsertSelected()
{
    const auto* item = blockList_->currentItem();
    if (!item) {
        QMessageBox::information(this, QStringLiteral("设计中心"), QStringLiteral("请先选择一个块。"));
        return;
    }
    const std::string name = item->data(Qt::UserRole).toString().toStdString();
    // Always pull definition from the source DXF so we don't insert a stale
    // same-named block that already exists in the host drawing.
    RedefineSelected();
    if (!document_.HasBlockDefinition(name)) {
        QMessageBox::warning(this, QStringLiteral("设计中心"), QStringLiteral("块定义无效（可能源文件未包含块内几何）。"));
        return;
    }

    BlockInsertDialog dialog(document_, this);
    dialog.SelectBlock(QString::fromStdString(name));
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    const BlockInsertDialogResult result = dialog.Result();
    if (!view_) {
        return;
    }
    CadView::PendingBlockInsert pending;
    pending.blockName = result.blockName;
    pending.scale = result.scale;
    pending.rotationRadians = result.rotationDegrees * 3.14159265358979323846 / 180.0;
    pending.explodeOnInsert = result.explodeOnInsert;
    pending.attributes = result.attributes;
    view_->BeginInsertBlock(std::move(pending));
    statusLabel_->setText(QStringLiteral("请在图纸中指定插入点。"));
    raise();
}

void DesignCenterDialog::OnItemActivated()
{
    InsertSelected();
}

} // namespace cad
