#include "View/LayerLineTypePickerDialog.h"

#include "IO/LineTypeLibrary.h"
#include "View/LayerLineTypeLoadDialog.h"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPainter>
#include <QPushButton>
#include <QStyledItemDelegate>
#include <QVBoxLayout>
#include <cstring>
#include <algorithm>

namespace cad {

namespace {

class LineTypePreviewDelegate : public QStyledItemDelegate {
public:
    explicit LineTypePreviewDelegate(const CadDocument& document, QObject* parent = nullptr)
        : QStyledItemDelegate(parent)
        , document_(document)
    {
    }

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        QStyleOptionViewItem opt = option;
        initStyleOption(&opt, index);

        painter->save();
        const bool selected = opt.state & QStyle::State_Selected;
        painter->fillRect(opt.rect, selected ? QColor(9, 71, 113) : QColor(37, 37, 38));

        const QString name = index.data(Qt::UserRole).toString();
        const QString description = index.data(Qt::UserRole + 1).toString();
        const QColor textColor(243, 243, 243);

        LineTypeDefinition definition;
        definition.name = name.toStdString();
        if (const LineTypeDefinition* found = LineTypeLibrary::FindDefinition(document_, definition.name)) {
            definition = *found;
        } else {
            const QByteArray patternBytes = index.data(Qt::UserRole + 2).toByteArray();
            if (!patternBytes.isEmpty()) {
                const int count = patternBytes.size() / static_cast<int>(sizeof(double));
                definition.pattern.resize(static_cast<std::size_t>(count));
                std::memcpy(definition.pattern.data(), patternBytes.constData(), patternBytes.size());
            }
        }

        const QRect nameRect(opt.rect.left() + 10, opt.rect.top(), opt.rect.width() / 3, opt.rect.height());
        painter->setPen(textColor);
        painter->drawText(nameRect, Qt::AlignLeft | Qt::AlignVCenter, name);

        QPen pen(textColor, 1.2);
        LineTypeLibrary::ApplyPatternToPen(pen, definition, 1.0);
        painter->setPen(pen);
        const QRect lineRect = opt.rect.adjusted(opt.rect.width() / 3 + 8, 8, -12, -8);
        const int y = lineRect.center().y();
        painter->drawLine(lineRect.left(), y, lineRect.right(), y);
        painter->restore();

        Q_UNUSED(description);
    }

    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        QSize size = QStyledItemDelegate::sizeHint(option, index);
        size.setHeight(std::max(size.height(), 34));
        return size;
    }

private:
    const CadDocument& document_;
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

LayerLineTypePickerDialog::LayerLineTypePickerDialog(
    CadDocument& document,
    const std::string& initialLineType,
    QWidget* parent)
    : QDialog(parent)
    , document_(document)
    , selectedLineType_(initialLineType.empty() ? "Continuous" : initialLineType)
{
    setWindowTitle(QStringLiteral("选择线型"));
    setModal(true);
    resize(460, 380);
    setStyleSheet(QString::fromUtf8(kDarkDialogStyle));

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel(QStringLiteral("可用线型"), this));

    list_ = new QListWidget(this);
    list_->setItemDelegate(new LineTypePreviewDelegate(document_, list_));
    layout->addWidget(list_, 1);

    auto* buttonRow = new QHBoxLayout();
    loadButton_ = new QPushButton(QStringLiteral("加载(&L)..."), this);
    connect(loadButton_, &QPushButton::clicked, this, [this]() { LoadLineTypes(); });
    buttonRow->addWidget(loadButton_);
    buttonRow->addStretch(1);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, [this]() { AcceptSelection(); });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    buttonRow->addWidget(buttons);
    layout->addLayout(buttonRow);

    connect(list_, &QListWidget::itemDoubleClicked, this, [this]() { AcceptSelection(); });

    Populate();
}

void LayerLineTypePickerDialog::Populate()
{
    list_->clear();
    int currentRow = 0;
    for (const LineTypeDefinition& lineType : document_.LineTypes()) {
        const QString name = QString::fromUtf8(lineType.name.c_str());
        const QString description = QString::fromUtf8(lineType.description.c_str());
        auto* item = new QListWidgetItem(name);
        item->setData(Qt::UserRole, name);
        item->setData(Qt::UserRole + 1, description);
        QByteArray patternBytes;
        patternBytes.resize(static_cast<int>(lineType.pattern.size() * sizeof(double)));
        if (!lineType.pattern.empty()) {
            std::memcpy(patternBytes.data(), lineType.pattern.data(), patternBytes.size());
        }
        item->setData(Qt::UserRole + 2, patternBytes);
        list_->addItem(item);
        if (lineType.name == selectedLineType_) {
            currentRow = list_->count() - 1;
        }
    }
    if (list_->count() > 0) {
        list_->setCurrentRow(currentRow);
    }
}

void LayerLineTypePickerDialog::LoadLineTypes()
{
    LayerLineTypeLoadDialog dialog(document_, this);
    if (dialog.exec() != QDialog::Accepted || dialog.loadedCount() <= 0) {
        return;
    }
    Populate();
}

void LayerLineTypePickerDialog::AcceptSelection()
{
    if (QListWidgetItem* item = list_->currentItem()) {
        selectedLineType_ = item->data(Qt::UserRole).toString().toStdString();
        accept();
    }
}

} // namespace cad
