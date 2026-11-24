#pragma once

#include "DataModel/CadDocument.h"
#include <QDialog>
#include <string>

class QListWidget;
class QPushButton;

namespace cad {

class LayerLineTypePickerDialog : public QDialog {
public:
    explicit LayerLineTypePickerDialog(
        CadDocument& document,
        const std::string& initialLineType,
        QWidget* parent = nullptr);

    std::string selectedLineType() const { return selectedLineType_; }

private:
    void Populate();
    void AcceptSelection();
    void LoadLineTypes();

    CadDocument& document_;
    QListWidget* list_ = nullptr;
    QPushButton* loadButton_ = nullptr;
    std::string selectedLineType_;
};

} // namespace cad
