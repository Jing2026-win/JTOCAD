#pragma once

#include "DataModel/CadDocument.h"
#include <QDialog>
#include <functional>

class QListWidget;
class QLabel;
class QLineEdit;

namespace cad {

class CommandManager;
class CadView;

// AutoCAD Blocks Palette (BLOCKSPALETTE / Ctrl+3).
class BlocksPaletteDialog : public QDialog {
public:
    BlocksPaletteDialog(
        CadDocument& document,
        CommandManager& commandManager,
        CadView* view,
        QWidget* parent = nullptr);

    void Refresh();
    void SetCreateBlockCallback(std::function<void()> callback);
    void SetWblockCallback(std::function<void()> callback);
    std::string SelectedBlockName() const;

private:
    void InsertCurrent();
    void RedefineFromSelection();
    void BrowseLibraryFolder();
    void LoadLibraryFolder(const QString& folder);

    CadDocument& document_;
    CommandManager& commandManager_;
    CadView* view_ = nullptr;
    QListWidget* currentList_ = nullptr;
    QListWidget* libraryList_ = nullptr;
    QLineEdit* libraryPathEdit_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    std::function<void()> createBlockCallback_;
    std::function<void()> wblockCallback_;
    QString libraryFolder_;
};

} // namespace cad
