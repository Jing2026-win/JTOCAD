#pragma once

#include "DataModel/CadDocument.h"
#include <QDialog>
#include <QShowEvent>

class QLineEdit;
class QTableWidget;

namespace cad {

class LayerManagerDialog : public QDialog {
public:
    explicit LayerManagerDialog(CadDocument& document, QWidget* parent = nullptr);

protected:
    void showEvent(QShowEvent* event) override;

private:
    void Populate();
    void ApplyTable(bool notifyDocument = true);
    void AddLayer();
    void DeleteSelectedLayer();
    void SetSelectedCurrent();
    void MoveSelectedEntitiesToLayer();
    void IsolateSelectedLayer();
    void RestoreIsolation();
    void MergeSelectedLayer();
    void SaveState();
    void RestoreState();
    void OnCellClicked(int row, int column);
    void EditColorAt(int row);
    void EditLineTypeAt(int row);
    void EditLineWeightAt(int row);
    void UpdateColorItem(int row, std::uint32_t rgb);
    void UpdateLineTypeItem(int row, const QString& lineTypeName);
    void UpdateLineWeightItem(int row, double lineWeightMm);

    int CurrentRow() const;
    std::string LayerNameAt(int row) const;

    CadDocument& document_;
    QLineEdit* filterEdit_ = nullptr;
    QTableWidget* table_ = nullptr;
};

} // namespace cad
