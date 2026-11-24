#pragma once

#include "DataModel/CadDocument.h"
#include <QDialog>
#include <string>
#include <vector>

class QLineEdit;
class QListWidget;

namespace cad {

class LayerLineTypeLoadDialog : public QDialog {
public:
    explicit LayerLineTypeLoadDialog(CadDocument& document, QWidget* parent = nullptr);

    int loadedCount() const { return loadedCount_; }

private:
    void BrowseFile();
    void ReloadFromFile();
    void LoadSelected();

    CadDocument& document_;
    QLineEdit* fileEdit_ = nullptr;
    QListWidget* list_ = nullptr;
    int loadedCount_ = 0;
};

} // namespace cad
