#pragma once

#include <QDialog>
#include <cstdint>

class QLabel;
class QSpinBox;

namespace cad {

class LayerColorPickerDialog : public QDialog {
public:
    explicit LayerColorPickerDialog(std::uint32_t initialRgb, QWidget* parent = nullptr);

    std::uint32_t selectedRgb() const { return selectedRgb_; }
    int selectedColorIndex() const { return selectedIndex_; }

private:
    void SetSelection(int index, std::uint32_t rgb);
    void UpdatePreview();
    void PickTrueColor();

    QLabel* previewLabel_ = nullptr;
    QSpinBox* indexSpin_ = nullptr;
    std::uint32_t selectedRgb_ = 0xFFFFFF;
    int selectedIndex_ = 7;
};

} // namespace cad
