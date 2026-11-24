#pragma once

#include "DataModel/CadDocument.h"
#include <QDialog>
#include <string>
#include <vector>

class QListWidget;
class QLineEdit;
class QLabel;

namespace cad {

class CommandManager;
class CadView;

// AutoCAD Design Center (ADC / Ctrl+2): browse DXF/DWG-like files and insert/redefine blocks.
class DesignCenterDialog : public QDialog {
public:
    DesignCenterDialog(
        CadDocument& document,
        CommandManager& commandManager,
        CadView* view,
        QWidget* parent = nullptr);

private:
    void BrowseFile();
    void ReloadBlocks();
    void InsertSelected();
    void RedefineSelected();
    void OnItemActivated();

    CadDocument& document_;
    CommandManager& commandManager_;
    CadView* view_ = nullptr;
    CadDocument sourceDocument_;
    QString sourcePath_;
    QLineEdit* pathEdit_ = nullptr;
    QListWidget* blockList_ = nullptr;
    QLabel* statusLabel_ = nullptr;
};

} // namespace cad
