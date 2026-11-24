#include "View/GraphicsConfigDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QVBoxLayout>

namespace cad {

GraphicsConfigDialog::GraphicsConfigDialog(const GraphicsConfig& config, QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("图形性能 (GRAPHICSCONFIG)"));
    setModal(true);
    resize(520, 380);

    auto* root = new QVBoxLayout(this);

    auto* accelGroup = new QGroupBox(tr("硬件加速"), this);
    auto* accelForm = new QFormLayout(accelGroup);

    hardwareAccelCheck_ = new QCheckBox(tr("启用硬件加速（OpenGL + GPU 光栅化）"), accelGroup);
    hardwareAccelCheck_->setChecked(config.hardwareAcceleration);
    accelForm->addRow(QString(), hardwareAccelCheck_);

    msaaCombo_ = new QComboBox(accelGroup);
    msaaCombo_->addItem(tr("关闭 (0x)"), 0);
    msaaCombo_->addItem(tr("2x MSAA"), 2);
    msaaCombo_->addItem(tr("4x MSAA"), 4);
    msaaCombo_->addItem(tr("8x MSAA（推荐）"), 8);
    const int msaa = GraphicsConfig::ClampMsaaSamples(config.msaaSamples);
    for (int i = 0; i < msaaCombo_->count(); ++i) {
        if (msaaCombo_->itemData(i).toInt() == msaa) {
            msaaCombo_->setCurrentIndex(i);
            break;
        }
    }
    accelForm->addRow(tr("多重采样抗锯齿 (MSAA)"), msaaCombo_);

    root->addWidget(accelGroup);

    auto* qualityGroup = new QGroupBox(tr("显示质量"), this);
    auto* qualityForm = new QFormLayout(qualityGroup);

    lineSmoothingCheck_ = new QCheckBox(tr("平滑线显示 (LINESMOOTHING)"), qualityGroup);
    lineSmoothingCheck_->setChecked(config.lineSmoothing);
    qualityForm->addRow(QString(), lineSmoothingCheck_);

    highQualityGeomCheck_ = new QCheckBox(tr("高质量几何 (HQGEOM)"), qualityGroup);
    highQualityGeomCheck_->setChecked(config.highQualityGeometry);
    highQualityGeomCheck_->setToolTip(tr(
        "软件渲染：屏幕圆/圆弧使用数学真曲线。\n"
        "硬件加速（OpenGL）：使用随缩放自适应的高密度矢量拟合（避免 OpenGL 放大后圆弧变折线）。\n"
        "不影响几何精度与打印。"));
    qualityForm->addRow(QString(), highQualityGeomCheck_);

    root->addWidget(qualityGroup);

    statusLabel_ = new QLabel(this);
    statusLabel_->setWordWrap(true);
    statusLabel_->setStyleSheet(QStringLiteral("color: #A8B0B8;"));
    root->addWidget(statusLabel_);

    auto* tip = new QLabel(
        tr("说明：硬件加速将视口渲染移交 GPU（OpenGL），并启用 MSAA 对斜线/曲线边缘做多重采样平滑。\n"
           "若出现闪烁或驱动异常，可关闭硬件加速回退到 CPU 软件光栅化。"),
        this);
    tip->setWordWrap(true);
    root->addWidget(tip);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(buttons);

    connect(hardwareAccelCheck_, &QCheckBox::toggled, this, [this](bool) { SyncDependentControls(); });
    SyncDependentControls();
}

void GraphicsConfigDialog::SyncDependentControls()
{
    const bool accel = hardwareAccelCheck_->isChecked();
    msaaCombo_->setEnabled(accel);
    if (accel) {
        statusLabel_->setText(tr("状态：硬件加速开启 — GPU 参与线条光栅化与抗锯齿。"));
    } else {
        statusLabel_->setText(tr("状态：硬件加速关闭 — 使用 CPU 软件绘制（VIEWRES 控制曲线细分）。"));
    }
}

GraphicsConfig GraphicsConfigDialog::Config() const
{
    GraphicsConfig config;
    config.hardwareAcceleration = hardwareAccelCheck_->isChecked();
    config.msaaSamples = msaaCombo_->currentData().toInt();
    config.lineSmoothing = lineSmoothingCheck_->isChecked();
    config.highQualityGeometry = highQualityGeomCheck_->isChecked();
    config.Normalize();
    return config;
}

} // namespace cad
