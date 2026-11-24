#pragma once

#include "DataModel/CadDocument.h"
#include <QDialog>
#include <vector>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;
class QWidget;

namespace cad {

class LineTypeManagerDialog : public QDialog {
public:
    explicit LineTypeManagerDialog(CadDocument& document, QWidget* parent = nullptr);

    // Name chosen as "current" when OK (ByLayer / ByBlock / Continuous / ...)
    std::string selectedCurrentLineType() const { return acceptedCurrentName_; }

private:
    enum class FilterMode {
        All = 0,
        Used = 1,
        Unused = 2
    };

    struct Snapshot {
        std::vector<LineTypeDefinition> lineTypes;
        EntityStyle currentStyle;
        double globalScale = 1.0;
        double currentScale = 1.0;
        bool usePaperSpace = true;
        std::vector<std::pair<std::string, std::string>> layerLineTypes;
        std::vector<std::pair<EntityId, EntityStyle>> entityStyles;
    };

    void CaptureSnapshot();
    void RestoreSnapshot();
    void PopulateTable();
    void UpdateCurrentLabel();
    void UpdateDetailsFromSelection();
    void ApplyDetailsToSelection();
    void OnFilterChanged();
    void OnSelectionChanged();
    void OnLoad();
    void OnDelete();
    void OnSetCurrent();
    void ToggleDetails();
    void OnAccept();
    void OnRenameCell(int row, int column);

    bool IsSpecialName(const std::string& name) const;
    bool PassesFilter(const std::string& name) const;
    std::string CurrentLineTypeDisplayName() const;
    void SetDocumentCurrentLineType(const std::string& name);

    CadDocument& document_;
    Snapshot snapshot_;
    std::string workingCurrentName_;
    std::string acceptedCurrentName_;

    QComboBox* filterCombo_ = nullptr;
    QCheckBox* invertFilterCheck_ = nullptr;
    QLabel* currentLabel_ = nullptr;
    QTableWidget* table_ = nullptr;
    QWidget* detailsPanel_ = nullptr;
    QPushButton* deleteButton_ = nullptr;
    QPushButton* detailsToggleButton_ = nullptr;
    QLineEdit* nameEdit_ = nullptr;
    QLineEdit* descriptionEdit_ = nullptr;
    QCheckBox* paperSpaceCheck_ = nullptr;
    QDoubleSpinBox* globalScaleSpin_ = nullptr;
    QDoubleSpinBox* objectScaleSpin_ = nullptr;
    QComboBox* isoPenCombo_ = nullptr;
    bool updatingUi_ = false;
};

} // namespace cad
