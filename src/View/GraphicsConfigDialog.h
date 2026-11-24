#pragma once

#include "Base/GraphicsConfig.h"
#include <QDialog>

class QCheckBox;
class QComboBox;
class QLabel;

namespace cad {

class GraphicsConfigDialog : public QDialog {
public:
    explicit GraphicsConfigDialog(const GraphicsConfig& config, QWidget* parent = nullptr);

    GraphicsConfig Config() const;

private:
    void SyncDependentControls();

    QCheckBox* hardwareAccelCheck_ = nullptr;
    QComboBox* msaaCombo_ = nullptr;
    QCheckBox* lineSmoothingCheck_ = nullptr;
    QCheckBox* highQualityGeomCheck_ = nullptr;
    QLabel* statusLabel_ = nullptr;
};

} // namespace cad
