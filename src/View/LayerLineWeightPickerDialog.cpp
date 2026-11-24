#include "View/LayerLineWeightPickerDialog.h"

#include "View/LineWeightUtil.h"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QStyledItemDelegate>
#include <QVBoxLayout>

namespace cad {

namespace {

class LineWeightPreviewDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        QStyleOptionViewItem opt = option;
        initStyleOption(&opt, index);

        painter->save();
        const bool selected = opt.state & QStyle::State_Selected;
        painter->fillRect(opt.rect, selected ? QColor(9, 71, 113) : QColor(37, 37, 38));

        const double mm = index.data(Qt::UserRole).toDouble();
        const QString label = LineWeightMenuLabel(mm);
        const QColor textColor(243, 243, 243);

        const int textWidth = 100;
        const QRect textRect(opt.rect.right() - textWidth - 8, opt.rect.top(), textWidth, opt.rect.height());
        const QRect lineRect(opt.rect.left() + 12, opt.rect.top(), textRect.left() - opt.rect.left() - 20, opt.rect.height());

        QPen pen(textColor, LineWeightPreviewPenWidth(mm));
        pen.setCapStyle(Qt::FlatCap);
        painter->setPen(pen);
        const int y = lineRect.center().y();
        painter->drawLine(lineRect.left(), y, lineRect.right(), y);

        painter->setPen(textColor);
        painter->drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, label);
        painter->restore();
    }

    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        Q_UNUSED(index);
        QSize size = QStyledItemDelegate::sizeHint(option, index);
        size.setHeight(std::max(size.height(), 28));
        return size;
    }
};

const char* kDarkDialogStyle = R"(
QDialog { background-color: #2d2d30; color: #f3f3f3; }
QLabel { color: #f3f3f3; background: transparent; }
QListWidget {
  background-color: #252526;
  color: #f3f3f3;
  border: 1px solid #555555;
  outline: none;
}
QListWidget::item { color: #f3f3f3; }
QListWidget::item:selected { background-color: #094771; color: #ffffff; }
QPushButton {
  background-color: #3e3e42;
  color: #f3f3f3;
  border: 1px solid #555555;
  padding: 4px 14px;
  min-height: 22px;
}
QPushButton:hover { background-color: #094771; }
QDialogButtonBox QPushButton { min-width: 72px; }
)";

} // namespace

LayerLineWeightPickerDialog::LayerLineWeightPickerDialog(
    const double initialLineWeightMm,
    QWidget* parent)
    : QDialog(parent)
    , initialLineWeightMm_(initialLineWeightMm)
    , selectedLineWeightMm_(initialLineWeightMm)
{
    setWindowTitle(QStringLiteral("线宽"));
    setModal(true);
    resize(360, 420);
    setStyleSheet(QString::fromUtf8(kDarkDialogStyle));

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel(QStringLiteral("线宽:"), this));

    list_ = new QListWidget(this);
    list_->setItemDelegate(new LineWeightPreviewDelegate(list_));
    layout->addWidget(list_, 1);

    oldLabel_ = new QLabel(
        QStringLiteral("旧的: %1").arg(LineWeightMenuLabel(initialLineWeightMm_)),
        this);
    newLabel_ = new QLabel(
        QStringLiteral("新的: %1").arg(LineWeightMenuLabel(initialLineWeightMm_)),
        this);
    layout->addWidget(oldLabel_);
    layout->addWidget(newLabel_);

    auto* buttonRow = new QHBoxLayout();
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    auto* helpButton = new QPushButton(QStringLiteral("帮助(&H)"), this);
    connect(helpButton, &QPushButton::clicked, this, [this]() {
        QMessageBox::information(
            this,
            QStringLiteral("线宽"),
            QStringLiteral("选择图层在打印和线宽显示打开时使用的线宽。\n"
                           "“默认”表示使用图形默认线宽。"));
    });
    connect(buttons, &QDialogButtonBox::accepted, this, [this]() { AcceptSelection(); });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    buttonRow->addWidget(helpButton);
    buttonRow->addStretch(1);
    buttonRow->addWidget(buttons);
    layout->addLayout(buttonRow);

    connect(list_, &QListWidget::currentItemChanged, this, [this]() { UpdateNewLabel(); });
    connect(list_, &QListWidget::itemDoubleClicked, this, [this]() { AcceptSelection(); });

    Populate();
}

void LayerLineWeightPickerDialog::Populate()
{
    list_->clear();
    int currentRow = 0;
    for (const double mm : StandardLineWeightsMm()) {
        auto* item = new QListWidgetItem(LineWeightMenuLabel(mm));
        item->setData(Qt::UserRole, mm);
        list_->addItem(item);
        if (LineWeightNearlyEqual(mm, initialLineWeightMm_)) {
            currentRow = list_->count() - 1;
        }
    }
    if (list_->count() > 0) {
        list_->setCurrentRow(currentRow);
    }
    UpdateNewLabel();
}

void LayerLineWeightPickerDialog::UpdateNewLabel()
{
    if (QListWidgetItem* item = list_->currentItem()) {
        const double mm = item->data(Qt::UserRole).toDouble();
        newLabel_->setText(QStringLiteral("新的: %1").arg(LineWeightMenuLabel(mm)));
    }
}

void LayerLineWeightPickerDialog::AcceptSelection()
{
    if (QListWidgetItem* item = list_->currentItem()) {
        selectedLineWeightMm_ = item->data(Qt::UserRole).toDouble();
        accept();
    }
}

} // namespace cad
