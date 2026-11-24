#include "View/LayerColorPickerDialog.h"

#include "View/AcadColorPalette.h"

#include <QColorDialog>
#include <QDialogButtonBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QVBoxLayout>

namespace cad {

namespace {

QPushButton* MakeColorButton(const int index, const std::uint32_t rgb, QWidget* parent)
{
    const QColor color(
        static_cast<int>((rgb >> 16) & 0xFF),
        static_cast<int>((rgb >> 8) & 0xFF),
        static_cast<int>(rgb & 0xFF));
    auto* button = new QPushButton(parent);
    button->setFixedSize(24, 24);
    button->setToolTip(QStringLiteral("颜色 %1").arg(index));
    button->setProperty("aciIndex", index);
    button->setStyleSheet(QStringLiteral(
        "QPushButton { background-color: %1; border: 1px solid #666; }"
        "QPushButton:hover { border: 2px solid #00A0FF; }")
        .arg(color.name(QColor::HexRgb)));
    return button;
}

} // namespace

LayerColorPickerDialog::LayerColorPickerDialog(const std::uint32_t initialRgb, QWidget* parent)
    : QDialog(parent)
    , selectedRgb_(initialRgb & 0xFFFFFFU)
    , selectedIndex_(RgbToAcadColorIndex(selectedRgb_))
{
    setWindowTitle(QStringLiteral("选择颜色"));
    setModal(true);
    resize(560, 520);

    auto* rootLayout = new QVBoxLayout(this);

    auto* previewLayout = new QHBoxLayout();
    previewLabel_ = new QLabel(this);
    previewLabel_->setFixedSize(72, 36);
    previewLabel_->setFrameShape(QFrame::Box);
    indexSpin_ = new QSpinBox(this);
    indexSpin_->setRange(1, 255);
    indexSpin_->setValue(selectedIndex_);
    previewLayout->addWidget(new QLabel(QStringLiteral("当前颜色"), this));
    previewLayout->addWidget(previewLabel_);
    previewLayout->addWidget(new QLabel(QStringLiteral("索引颜色"), this));
    previewLayout->addWidget(indexSpin_);
    previewLayout->addStretch();
    rootLayout->addLayout(previewLayout);

    auto* standardGroup = new QGroupBox(QStringLiteral("标准颜色"), this);
    auto* standardLayout = new QHBoxLayout(standardGroup);
    for (int index = 1; index <= 9; ++index) {
        auto* button = MakeColorButton(index, AcadColorIndexToRgb(index), this);
        standardLayout->addWidget(button);
        connect(button, &QPushButton::clicked, this, [this, index]() {
            SetSelection(index, AcadColorIndexToRgb(index));
        });
    }
    standardLayout->addStretch();
    rootLayout->addWidget(standardGroup);

    auto* paletteGroup = new QGroupBox(QStringLiteral("索引颜色 (10-249)"), this);
    auto* paletteOuter = new QVBoxLayout(paletteGroup);
    auto* scroll = new QScrollArea(paletteGroup);
    scroll->setWidgetResizable(true);
    auto* paletteWidget = new QWidget(scroll);
    auto* paletteLayout = new QGridLayout(paletteWidget);
    paletteLayout->setSpacing(2);
    int gridRow = 0;
    for (int index = 10; index <= 249; ++index) {
        auto* button = MakeColorButton(index, AcadColorIndexToRgb(index), paletteWidget);
        paletteLayout->addWidget(button, gridRow, (index - 10) % 10);
        connect(button, &QPushButton::clicked, this, [this, index]() {
            SetSelection(index, AcadColorIndexToRgb(index));
        });
        if ((index - 9) % 10 == 0) {
            ++gridRow;
        }
    }
    scroll->setWidget(paletteWidget);
    paletteOuter->addWidget(scroll);
    rootLayout->addWidget(paletteGroup, 1);

    auto* grayGroup = new QGroupBox(QStringLiteral("灰度 (250-255)"), this);
    auto* grayLayout = new QHBoxLayout(grayGroup);
    for (int index = 250; index <= 255; ++index) {
        auto* button = MakeColorButton(index, AcadColorIndexToRgb(index), this);
        grayLayout->addWidget(button);
        connect(button, &QPushButton::clicked, this, [this, index]() {
            SetSelection(index, AcadColorIndexToRgb(index));
        });
    }
    grayLayout->addStretch();
    rootLayout->addWidget(grayGroup);

    auto* trueColorButton = new QPushButton(QStringLiteral("真彩色..."), this);
    connect(trueColorButton, &QPushButton::clicked, this, &LayerColorPickerDialog::PickTrueColor);
    rootLayout->addWidget(trueColorButton);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    rootLayout->addWidget(buttons);

    connect(indexSpin_, qOverload<int>(&QSpinBox::valueChanged), this, [this](int index) {
        SetSelection(index, AcadColorIndexToRgb(index));
    });

    UpdatePreview();
}

void LayerColorPickerDialog::SetSelection(const int index, const std::uint32_t rgb)
{
    selectedIndex_ = index;
    selectedRgb_ = rgb & 0xFFFFFFU;
    if (indexSpin_ && indexSpin_->value() != index) {
        indexSpin_->blockSignals(true);
        indexSpin_->setValue(index);
        indexSpin_->blockSignals(false);
    }
    UpdatePreview();
}

void LayerColorPickerDialog::UpdatePreview()
{
    const QColor color(
        static_cast<int>((selectedRgb_ >> 16) & 0xFF),
        static_cast<int>((selectedRgb_ >> 8) & 0xFF),
        static_cast<int>(selectedRgb_ & 0xFF));
    previewLabel_->setStyleSheet(QStringLiteral("background-color: %1;").arg(color.name(QColor::HexRgb)));
    previewLabel_->setText(AcadColorLabel(selectedIndex_, selectedRgb_));
    previewLabel_->setAlignment(Qt::AlignCenter);
}

void LayerColorPickerDialog::PickTrueColor()
{
    const QColor initial(
        static_cast<int>((selectedRgb_ >> 16) & 0xFF),
        static_cast<int>((selectedRgb_ >> 8) & 0xFF),
        static_cast<int>(selectedRgb_ & 0xFF));
    const QColor picked = QColorDialog::getColor(initial, this, QStringLiteral("真彩色"));
    if (!picked.isValid()) {
        return;
    }
    const std::uint32_t rgb = (static_cast<std::uint32_t>(picked.red()) << 16)
        | (static_cast<std::uint32_t>(picked.green()) << 8)
        | static_cast<std::uint32_t>(picked.blue());
    SetSelection(RgbToAcadColorIndex(rgb), rgb);
}

} // namespace cad
