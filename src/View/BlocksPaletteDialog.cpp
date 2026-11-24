#include "View/BlocksPaletteDialog.h"

#include "Command/CommandManager.h"
#include "DataModel/BlockReferenceEntity.h"
#include "DataModel/Entity.h"
#include "IO/DxfService.h"
#include "View/BlockInsertDialog.h"
#include "View/CadView.h"

#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QTabWidget>
#include <QVBoxLayout>

namespace cad {

BlocksPaletteDialog::BlocksPaletteDialog(
    CadDocument& document,
    CommandManager& commandManager,
    CadView* view,
    QWidget* parent)
    : QDialog(parent)
    , document_(document)
    , commandManager_(commandManager)
    , view_(view)
{
    setWindowTitle(QStringLiteral("块选项板 (BLOCKSPALETTE)"));
    setModal(false);
    resize(460, 560);
    setAttribute(Qt::WA_DeleteOnClose);
    setStyleSheet(QStringLiteral(
        "QDialog { background-color: #2d2d30; color: #f0f0f0; }"
        "QLabel, QListWidget, QLineEdit, QTabWidget, QTabBar::tab {"
        "  color: #f0f0f0; background-color: #252526;"
        "}"
        "QTabBar::tab:selected { background-color: #3e3e42; }"
        "QListWidget::item:selected { background-color: #094771; }"
        "QPushButton { background-color: #3e3e42; color: #f0f0f0; border: 1px solid #555; padding: 5px 10px; }"
        "QPushButton:hover { background-color: #094771; }"));

    auto* root = new QVBoxLayout(this);
    auto* tabs = new QTabWidget(this);

    // Current drawing tab
    auto* currentPage = new QWidget(tabs);
    auto* currentLayout = new QVBoxLayout(currentPage);
    currentList_ = new QListWidget(currentPage);
    currentLayout->addWidget(currentList_, 1);
    auto* currentButtons = new QHBoxLayout();
    auto* insertButton = new QPushButton(QStringLiteral("插入"), currentPage);
    auto* createButton = new QPushButton(QStringLiteral("新建块"), currentPage);
    auto* redefineButton = new QPushButton(QStringLiteral("重定义"), currentPage);
    auto* wblockButton = new QPushButton(QStringLiteral("写块"), currentPage);
    currentButtons->addWidget(insertButton);
    currentButtons->addWidget(createButton);
    currentButtons->addWidget(redefineButton);
    currentButtons->addWidget(wblockButton);
    currentLayout->addLayout(currentButtons);
    tabs->addTab(currentPage, QStringLiteral("当前图形"));

    // Library tab
    auto* libraryPage = new QWidget(tabs);
    auto* libraryLayout = new QVBoxLayout(libraryPage);
    auto* pathRow = new QHBoxLayout();
    libraryPathEdit_ = new QLineEdit(libraryPage);
    libraryPathEdit_->setReadOnly(true);
    libraryPathEdit_->setPlaceholderText(QStringLiteral("选择图块库文件夹…"));
    auto* browseLib = new QPushButton(QStringLiteral("浏览…"), libraryPage);
    pathRow->addWidget(libraryPathEdit_, 1);
    pathRow->addWidget(browseLib);
    libraryLayout->addLayout(pathRow);
    libraryList_ = new QListWidget(libraryPage);
    libraryLayout->addWidget(libraryList_, 1);
    auto* importButton = new QPushButton(QStringLiteral("导入到当前图"), libraryPage);
    libraryLayout->addWidget(importButton);
    tabs->addTab(libraryPage, QStringLiteral("库"));

    root->addWidget(tabs, 1);
    statusLabel_ = new QLabel(QStringLiteral("Ctrl+3 块选项板：当前图形 / 本地库。"), this);
    root->addWidget(statusLabel_);

    connect(insertButton, &QPushButton::clicked, this, [this]() { InsertCurrent(); });
    connect(createButton, &QPushButton::clicked, this, [this]() {
        if (createBlockCallback_) {
            createBlockCallback_();
            Refresh();
        }
    });
    connect(redefineButton, &QPushButton::clicked, this, [this]() { RedefineFromSelection(); });
    connect(wblockButton, &QPushButton::clicked, this, [this]() {
        if (wblockCallback_) {
            wblockCallback_();
        }
    });
    connect(browseLib, &QPushButton::clicked, this, [this]() { BrowseLibraryFolder(); });
    connect(importButton, &QPushButton::clicked, this, [this]() {
        const auto* item = libraryList_->currentItem();
        if (!item) {
            return;
        }
        const QString path = item->data(Qt::UserRole).toString();
        CadDocument temp;
        if (!DxfService::ImportFile(path.toStdString(), temp)) {
            QMessageBox::warning(this, QStringLiteral("块选项板"), QStringLiteral("导入失败。"));
            return;
        }
        int count = 0;
        for (const BlockDefinition& def : temp.BlockDefinitions()) {
            BlockDefinition copy = def;
            copy.entities.clear();
            for (const auto& e : def.entities) {
                if (!e) {
                    continue;
                }
                auto clone = e->Clone();
                if (clone) {
                    copy.entities.push_back(std::shared_ptr<Entity>(std::move(clone)));
                }
            }
            // Legacy WBLOCK: geometry only in model space.
            if (copy.entities.empty()) {
                for (const auto& e : temp.Entities()) {
                    if (!e || e->Type() == EntityType::BlockReference) {
                        continue;
                    }
                    auto clone = e->Clone();
                    if (!clone) {
                        continue;
                    }
                    clone->MoveBy(-copy.baseX, -copy.baseY);
                    copy.entities.push_back(std::shared_ptr<Entity>(std::move(clone)));
                }
            }
            document_.AddBlockDefinition(std::move(copy));
            document_.RefreshBlockReferenceCaches(def.name);
            ++count;
        }
        Refresh();
        statusLabel_->setText(QStringLiteral("已导入 %1 个块定义。").arg(count));
    });
    connect(currentList_, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem*) {
        InsertCurrent();
    });

    Refresh();
}

void BlocksPaletteDialog::SetCreateBlockCallback(std::function<void()> callback)
{
    createBlockCallback_ = std::move(callback);
}

void BlocksPaletteDialog::SetWblockCallback(std::function<void()> callback)
{
    wblockCallback_ = std::move(callback);
}

std::string BlocksPaletteDialog::SelectedBlockName() const
{
    const auto* item = currentList_ ? currentList_->currentItem() : nullptr;
    if (!item) {
        return {};
    }
    return item->data(Qt::UserRole).toString().toStdString();
}

void BlocksPaletteDialog::Refresh()
{
    currentList_->clear();
    for (const BlockDefinition& def : document_.BlockDefinitions()) {
        auto* item = new QListWidgetItem(
            QStringLiteral("%1  (%2)")
                .arg(QString::fromStdString(def.name))
                .arg(def.entities.size()),
            currentList_);
        item->setData(Qt::UserRole, QString::fromStdString(def.name));
    }
    statusLabel_->setText(
        QStringLiteral("当前图形块定义：%1").arg(currentList_->count()));
}

void BlocksPaletteDialog::InsertCurrent()
{
    const auto* item = currentList_->currentItem();
    if (!item) {
        QMessageBox::information(this, QStringLiteral("块选项板"), QStringLiteral("请选择一个块。"));
        return;
    }
    const QString name = item->data(Qt::UserRole).toString();
    BlockInsertDialog dialog(document_, this);
    dialog.SelectBlock(name);
    if (dialog.exec() != QDialog::Accepted || !view_) {
        return;
    }
    const BlockInsertDialogResult result = dialog.Result();
    CadView::PendingBlockInsert pending;
    pending.blockName = result.blockName;
    pending.scale = result.scale;
    pending.rotationRadians = result.rotationDegrees * 3.14159265358979323846 / 180.0;
    pending.explodeOnInsert = result.explodeOnInsert;
    pending.attributes = result.attributes;
    view_->BeginInsertBlock(std::move(pending));
    statusLabel_->setText(QStringLiteral("请在图纸中指定插入点。"));
}

void BlocksPaletteDialog::RedefineFromSelection()
{
    const auto* item = currentList_->currentItem();
    if (!item) {
        QMessageBox::information(this, QStringLiteral("块选项板"), QStringLiteral("请选择要重定义的块。"));
        return;
    }
    const std::string name = item->data(Qt::UserRole).toString().toStdString();
    std::vector<EntityId> sources = document_.SelectedEntityIds();
    if (sources.empty()) {
        QMessageBox::information(
            this,
            QStringLiteral("块选项板"),
            QStringLiteral("请先选择要写入块定义的图元（可同时选中原块参照和新画的线）。"));
        return;
    }

    bool includesSameBlockRef = false;
    for (EntityId id : sources) {
        const Entity* entity = document_.FindEntity(id);
        if (entity && entity->Type() == EntityType::BlockReference
            && static_cast<const BlockReferenceEntity&>(*entity).BlockName() == name) {
            includesSameBlockRef = true;
            break;
        }
    }
    if (!includesSameBlockRef) {
        const auto answer = QMessageBox::question(
            this,
            QStringLiteral("重定义块"),
            QStringLiteral(
                "选择集中未包含块「%1」的参照。\n"
                "重定义将用当前选择完全替换原块定义（与 AutoCAD 的 BLOCK 重定义相同）。\n"
                "若要在原块上增加图形，请同时选中原块参照和新图元，或使用块编辑器/在位编辑。\n\n"
                "是否继续？")
                .arg(QString::fromStdString(name)));
        if (answer != QMessageBox::Yes) {
            return;
        }
    }

    const BlockDefinition* def = document_.FindBlockDefinition(name);
    const double bx = def ? def->baseX : 0.0;
    const double by = def ? def->baseY : 0.0;
    commandManager_.CreateBlock(name, bx, by, sources, BlockCreateMode::Keep);
    Refresh();
    statusLabel_->setText(QStringLiteral("已重定义「%1」。").arg(QString::fromStdString(name)));
}

void BlocksPaletteDialog::BrowseLibraryFolder()
{
    const QString folder = QFileDialog::getExistingDirectory(this, QStringLiteral("选择图块库文件夹"));
    if (folder.isEmpty()) {
        return;
    }
    LoadLibraryFolder(folder);
}

void BlocksPaletteDialog::LoadLibraryFolder(const QString& folder)
{
    libraryFolder_ = folder;
    libraryPathEdit_->setText(folder);
    libraryList_->clear();
    QDir dir(folder);
    const QStringList files = dir.entryList(QStringList() << QStringLiteral("*.dxf"), QDir::Files, QDir::Name);
    for (const QString& file : files) {
        auto* item = new QListWidgetItem(file, libraryList_);
        item->setData(Qt::UserRole, dir.absoluteFilePath(file));
    }
    statusLabel_->setText(QStringLiteral("库中 DXF：%1").arg(libraryList_->count()));
}

} // namespace cad
