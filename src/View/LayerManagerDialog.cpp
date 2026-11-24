#include "View/LayerManagerDialog.h"

#include "View/AcadColorPalette.h"
#include "View/LayerColorPickerDialog.h"
#include "View/LayerLineTypePickerDialog.h"
#include "View/LayerLineWeightPickerDialog.h"
#include "View/LineWeightUtil.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QShowEvent>
#include <QStyledItemDelegate>
#include <QStringList>
#include <QTableWidget>
#include <QVBoxLayout>
#include <algorithm>
#include <iomanip>
#include <sstream>

namespace cad {

namespace {

enum Column {
    CurrentColumn,
    NameColumn,
    OnColumn,
    FrozenColumn,
    LockedColumn,
    PlotColumn,
    ColorColumn,
    LineTypeColumn,
    LineWeightColumn,
    TransparencyColumn,
    DescriptionColumn,
    ColumnCount
};

QColor ColorFromRgb(const std::uint32_t color)
{
    return QColor(
        static_cast<int>((color >> 16) & 0xFF),
        static_cast<int>((color >> 8) & 0xFF),
        static_cast<int>(color & 0xFF));
}

QTableWidgetItem* ColorItem(const std::uint32_t color)
{
    const int index = RgbToAcadColorIndex(color);
    auto* item = new QTableWidgetItem(AcadColorLabel(index, color));
    item->setData(Qt::UserRole, static_cast<qulonglong>(color & 0xFFFFFFU));
    item->setFlags((item->flags() | Qt::ItemIsEnabled) & ~Qt::ItemIsEditable);
    return item;
}

class LayerColorCellDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        QStyleOptionViewItem opt = option;
        initStyleOption(&opt, index);

        painter->save();
        if (opt.state & QStyle::State_Selected) {
            painter->fillRect(opt.rect, opt.palette.highlight());
        } else {
            painter->fillRect(opt.rect, opt.palette.base());
        }

        const std::uint32_t rgb = static_cast<std::uint32_t>(index.data(Qt::UserRole).toULongLong() & 0xFFFFFFU);
        const QColor swatchColor = ColorFromRgb(rgb);
        const QRect swatchRect(opt.rect.left() + 6, opt.rect.center().y() - 6, 12, 12);
        painter->fillRect(swatchRect, swatchColor);
        painter->setPen(QColor(100, 100, 100));
        painter->drawRect(swatchRect);

        const QColor textColor = (opt.state & QStyle::State_Selected)
            ? opt.palette.highlightedText().color()
            : opt.palette.text().color();
        painter->setPen(textColor);
        painter->drawText(
            opt.rect.adjusted(24, 0, -4, 0),
            Qt::AlignLeft | Qt::AlignVCenter,
            index.data(Qt::DisplayRole).toString());
        painter->restore();
    }
};

class LayerLineWeightCellDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        QStyleOptionViewItem opt = option;
        initStyleOption(&opt, index);

        painter->save();
        if (opt.state & QStyle::State_Selected) {
            painter->fillRect(opt.rect, opt.palette.highlight());
        } else {
            painter->fillRect(opt.rect, opt.palette.base());
        }

        const double mm = index.data(Qt::UserRole).toDouble();
        const QString label = LineWeightTableLabel(mm);
        const QColor textColor = (opt.state & QStyle::State_Selected)
            ? opt.palette.highlightedText().color()
            : opt.palette.text().color();

        const int textWidth = 44;
        const QRect textRect(opt.rect.right() - textWidth - 4, opt.rect.top(), textWidth, opt.rect.height());
        const QRect lineRect(opt.rect.left() + 6, opt.rect.top(), textRect.left() - opt.rect.left() - 10, opt.rect.height());

        QPen pen(textColor, LineWeightPreviewPenWidth(mm));
        pen.setCapStyle(Qt::FlatCap);
        painter->setPen(pen);
        const int y = lineRect.center().y();
        painter->drawLine(lineRect.left(), y, lineRect.right(), y);

        painter->setPen(textColor);
        painter->drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, label);
        painter->restore();
    }
};

QTableWidgetItem* LineTypeItem(const QString& lineTypeName)
{
    auto* item = new QTableWidgetItem(lineTypeName);
    item->setFlags((item->flags() | Qt::ItemIsEnabled) & ~Qt::ItemIsEditable);
    return item;
}

QTableWidgetItem* LineWeightItem(const double lineWeightMm)
{
    auto* item = new QTableWidgetItem(LineWeightTableLabel(lineWeightMm));
    item->setData(Qt::UserRole, lineWeightMm);
    item->setFlags((item->flags() | Qt::ItemIsEnabled) & ~Qt::ItemIsEditable);
    return item;
}

double LineWeightFromItem(const QTableWidgetItem* item, const double fallback)
{
    if (!item) {
        return fallback;
    }
    return item->data(Qt::UserRole).toDouble();
}

std::uint32_t ColorFromItem(const QTableWidgetItem* item, const std::uint32_t fallback)
{
    if (!item) {
        return fallback;
    }
    return static_cast<std::uint32_t>(item->data(Qt::UserRole).toULongLong() & 0xFFFFFFU);
}

QTableWidgetItem* TextItem(const QString& text, const bool editable = true)
{
    auto* item = new QTableWidgetItem(text);
    if (!editable) {
        item->setFlags(item->flags() & ~Qt::ItemIsEditable);
    }
    return item;
}

QTableWidgetItem* CheckItem(const bool checked)
{
    auto* item = new QTableWidgetItem();
    item->setFlags((item->flags() | Qt::ItemIsUserCheckable) & ~Qt::ItemIsEditable);
    item->setCheckState(checked ? Qt::Checked : Qt::Unchecked);
    return item;
}

} // namespace

LayerManagerDialog::LayerManagerDialog(CadDocument& document, QWidget* parent)
    : QDialog(parent)
    , document_(document)
{
    setWindowTitle(QStringLiteral("图层特性管理器"));
    setModal(false);
    setWindowFlags(windowFlags() | Qt::Window);
    resize(980, 560);

    auto* layout = new QVBoxLayout(this);
    auto* filterLayout = new QFormLayout();
    filterEdit_ = new QLineEdit(this);
    filterEdit_->setPlaceholderText(QStringLiteral("按名称/说明过滤图层"));
    filterLayout->addRow(QStringLiteral("过滤器"), filterEdit_);
    layout->addLayout(filterLayout);

    table_ = new QTableWidget(this);
    table_->setColumnCount(ColumnCount);
    table_->setHorizontalHeaderLabels({
        QStringLiteral("当前"),
        QStringLiteral("名称"),
        QStringLiteral("开"),
        QStringLiteral("冻结"),
        QStringLiteral("锁定"),
        QStringLiteral("打印"),
        QStringLiteral("颜色"),
        QStringLiteral("线型"),
        QStringLiteral("线宽"),
        QStringLiteral("透明度"),
        QStringLiteral("说明"),
    });

    QHeaderView* header = table_->horizontalHeader();
    header->setMinimumSectionSize(28);
    header->setSectionsClickable(true);
    header->setHighlightSections(true);
    for (int column = 0; column < DescriptionColumn; ++column) {
        header->setSectionResizeMode(column, QHeaderView::Interactive);
    }
    header->setSectionResizeMode(DescriptionColumn, QHeaderView::Stretch);
    header->resizeSection(CurrentColumn, 44);
    header->resizeSection(NameColumn, 120);
    header->resizeSection(OnColumn, 40);
    header->resizeSection(FrozenColumn, 48);
    header->resizeSection(LockedColumn, 48);
    header->resizeSection(PlotColumn, 48);
    header->resizeSection(ColorColumn, 88);
    header->resizeSection(LineTypeColumn, 110);
    header->resizeSection(LineWeightColumn, 96);
    header->resizeSection(TransparencyColumn, 64);

    table_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setItemDelegateForColumn(ColorColumn, new LayerColorCellDelegate(table_));
    table_->setItemDelegateForColumn(LineWeightColumn, new LayerLineWeightCellDelegate(table_));
    layout->addWidget(table_, 1);

    auto* actionLayout = new QHBoxLayout();
    const auto addButton = [&](const QString& text, auto slot) {
        auto* button = new QPushButton(text, this);
        actionLayout->addWidget(button);
        connect(button, &QPushButton::clicked, this, slot);
    };
    addButton(QStringLiteral("新建"), [this]() { AddLayer(); });
    addButton(QStringLiteral("删除/强删"), [this]() { DeleteSelectedLayer(); });
    addButton(QStringLiteral("置为当前"), [this]() { SetSelectedCurrent(); });
    addButton(QStringLiteral("所选对象到此层"), [this]() { MoveSelectedEntitiesToLayer(); });
    addButton(QStringLiteral("隔离"), [this]() { IsolateSelectedLayer(); });
    addButton(QStringLiteral("取消隔离"), [this]() { RestoreIsolation(); });
    addButton(QStringLiteral("合并到..."), [this]() { MergeSelectedLayer(); });
    addButton(QStringLiteral("保存状态"), [this]() { SaveState(); });
    addButton(QStringLiteral("恢复状态"), [this]() { RestoreState(); });
    actionLayout->addStretch();
    layout->addLayout(actionLayout);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Apply | QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons->button(QDialogButtonBox::Apply), &QPushButton::clicked, this, [this]() {
        ApplyTable();
        Populate();
    });
    connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
        ApplyTable();
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    connect(filterEdit_, &QLineEdit::textChanged, this, [this]() { Populate(); });
    connect(table_, &QTableWidget::cellClicked, this, &LayerManagerDialog::OnCellClicked);
    Populate();
}

void LayerManagerDialog::showEvent(QShowEvent* event)
{
    QDialog::showEvent(event);
    Populate();
}

void LayerManagerDialog::Populate()
{
    // 刷新前先把表格中未应用的修改写回文档，避免新建/筛选等操作冲掉颜色等修改。
    if (table_->rowCount() > 0) {
        ApplyTable(false);
    }

    const QString filter = filterEdit_ ? filterEdit_->text().trimmed() : QString();
    table_->setRowCount(0);
    for (const Layer& layer : document_.Layers()) {
        const QString name = QString::fromUtf8(layer.Name().c_str());
        const QString description = QString::fromUtf8(layer.Description().c_str());
        if (!filter.isEmpty()
            && !name.contains(filter, Qt::CaseInsensitive)
            && !description.contains(filter, Qt::CaseInsensitive)) {
            continue;
        }

        const int row = table_->rowCount();
        table_->insertRow(row);
        auto* currentItem = TextItem(layer.Name() == document_.CurrentLayerName() ? QStringLiteral("✓") : QString(), false);
        currentItem->setData(Qt::UserRole, name);
        table_->setItem(row, CurrentColumn, currentItem);
        table_->setItem(row, NameColumn, TextItem(name, !layer.IsSystemLayer()));
        table_->setItem(row, OnColumn, CheckItem(layer.IsOn()));
        table_->setItem(row, FrozenColumn, CheckItem(layer.IsFrozen()));
        table_->setItem(row, LockedColumn, CheckItem(layer.IsLocked()));
        table_->setItem(row, PlotColumn, CheckItem(layer.IsPlottable()));
        table_->setItem(row, ColorColumn, ColorItem(layer.Color()));
        table_->setItem(row, LineTypeColumn, LineTypeItem(QString::fromUtf8(layer.LineTypeName().c_str())));
        table_->setItem(row, LineWeightColumn, LineWeightItem(layer.LineWeightMm()));
        table_->setItem(row, TransparencyColumn, TextItem(QString::number(layer.Transparency())));
        table_->setItem(row, DescriptionColumn, TextItem(description));
    }
}

void LayerManagerDialog::ApplyTable(const bool notifyDocument)
{
    for (int row = 0; row < table_->rowCount(); ++row) {
        const std::string oldName = LayerNameAt(row);
        Layer* layer = document_.FindLayer(oldName);
        if (!layer) {
            continue;
        }

        const QString newNameText = table_->item(row, NameColumn)->text().trimmed();
        std::string layerName = oldName;
        if (!newNameText.isEmpty() && newNameText.toStdString() != oldName) {
            if (document_.RenameLayer(oldName, newNameText.toStdString())) {
                layerName = newNameText.toStdString();
                layer = document_.FindLayer(layerName);
            }
        }
        if (!layer) {
            continue;
        }

        layer->SetOn(table_->item(row, OnColumn)->checkState() == Qt::Checked);
        layer->SetFrozen(table_->item(row, FrozenColumn)->checkState() == Qt::Checked);
        layer->SetLocked(table_->item(row, LockedColumn)->checkState() == Qt::Checked);
        layer->SetPlottable(layer->Name() == "Defpoints" ? false : table_->item(row, PlotColumn)->checkState() == Qt::Checked);
        layer->SetColor(ColorFromItem(table_->item(row, ColorColumn), layer->Color()));
        layer->SetLineTypeName(table_->item(row, LineTypeColumn)->text().trimmed().toStdString());
        layer->SetLineWeightMm(LineWeightFromItem(table_->item(row, LineWeightColumn), layer->LineWeightMm()));
        layer->SetTransparency(table_->item(row, TransparencyColumn)->text().toInt());
        layer->SetDescription(table_->item(row, DescriptionColumn)->text().trimmed().toStdString());
    }
    if (notifyDocument) {
        document_.NotifyChanged();
    }
}

void LayerManagerDialog::AddLayer()
{
    bool ok = false;
    const QString name = QInputDialog::getText(
        this,
        QStringLiteral("新建图层"),
        QStringLiteral("图层名称"),
        QLineEdit::Normal,
        QStringLiteral("Layer1"),
        &ok).trimmed();
    if (!ok || name.isEmpty()) {
        return;
    }
    const std::string layerName = name.toStdString();
    document_.EnsureLayer(layerName);
    document_.SetCurrentLayerName(layerName);
    Populate();
}

void LayerManagerDialog::DeleteSelectedLayer()
{
    const int row = CurrentRow();
    if (row < 0) {
        return;
    }
    const std::string name = LayerNameAt(row);
    if (QMessageBox::question(
            this,
            QStringLiteral("删除图层"),
            QStringLiteral("将强制删除图层及该图层上的所有对象。继续吗？"))
        != QMessageBox::Yes) {
        return;
    }
    if (!document_.DeleteLayerAndEntities(name)) {
        QMessageBox::warning(this, QStringLiteral("删除图层"), QStringLiteral("0、Defpoints、当前图层不能删除。"));
    }
    Populate();
}

void LayerManagerDialog::SetSelectedCurrent()
{
    const int row = CurrentRow();
    if (row >= 0 && !document_.SetCurrentLayerName(LayerNameAt(row))) {
        QMessageBox::warning(this, QStringLiteral("当前图层"), QStringLiteral("不能将冻结图层设为当前图层。"));
    }
    Populate();
}

void LayerManagerDialog::MoveSelectedEntitiesToLayer()
{
    const int row = CurrentRow();
    if (row >= 0 && !document_.SetEntitiesLayer(document_.SelectedEntityIds(), LayerNameAt(row))) {
        QMessageBox::information(this, QStringLiteral("图层匹配"), QStringLiteral("没有可移动的选中对象。"));
    }
}

void LayerManagerDialog::IsolateSelectedLayer()
{
    const int row = CurrentRow();
    if (row < 0) {
        return;
    }
    if (!document_.BeginLayerIsolation()) {
        return;
    }
    const std::string active = LayerNameAt(row);
    for (Layer& layer : document_.Layers()) {
        layer.SetOn(layer.Name() == active);
    }
    document_.NotifyAllEntitiesChanged();
    Populate();
}

void LayerManagerDialog::RestoreIsolation()
{
    if (!document_.RestoreLayerIsolation()) {
        QMessageBox::information(this, QStringLiteral("取消隔离"), QStringLiteral("没有可恢复的图层隔离状态。"));
    }
    Populate();
}

void LayerManagerDialog::MergeSelectedLayer()
{
    const int row = CurrentRow();
    if (row < 0) {
        return;
    }
    QStringList names;
    for (const Layer& layer : document_.Layers()) {
        if (layer.Name() != LayerNameAt(row)) {
            names << QString::fromUtf8(layer.Name().c_str());
        }
    }
    bool ok = false;
    const QString target = QInputDialog::getItem(
        this,
        QStringLiteral("图层合并"),
        QStringLiteral("目标图层"),
        names,
        0,
        false,
        &ok);
    if (ok && !target.isEmpty() && !document_.MergeLayerInto(LayerNameAt(row), target.toStdString())) {
        QMessageBox::warning(this, QStringLiteral("图层合并"), QStringLiteral("不能合并 0、Defpoints 或当前图层。"));
    }
    Populate();
}

void LayerManagerDialog::SaveState()
{
    bool ok = false;
    const QString name = QInputDialog::getText(this, QStringLiteral("保存图层状态"), QStringLiteral("状态名称"), QLineEdit::Normal, QStringLiteral("LayerState"), &ok);
    if (ok && !name.trimmed().isEmpty()) {
        ApplyTable();
        document_.SaveLayerState(name.trimmed().toStdString());
    }
}

void LayerManagerDialog::RestoreState()
{
    QStringList names;
    for (const LayerStateSnapshot& state : document_.LayerStates()) {
        if (state.name != "__LAYISO") {
            names << QString::fromUtf8(state.name.c_str());
        }
    }
    if (names.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("恢复图层状态"), QStringLiteral("暂无已保存的图层状态。"));
        return;
    }
    bool ok = false;
    const QString name = QInputDialog::getItem(this, QStringLiteral("恢复图层状态"), QStringLiteral("状态名称"), names, 0, false, &ok);
    if (!ok) {
        return;
    }
    const auto& states = document_.LayerStates();
    const auto it = std::find_if(states.begin(), states.end(), [&](const LayerStateSnapshot& state) {
        return QString::fromUtf8(state.name.c_str()) == name;
    });
    if (it != states.end()) {
        document_.RestoreLayerState(*it);
        Populate();
    }
}

int LayerManagerDialog::CurrentRow() const
{
    return table_->currentRow();
}

void LayerManagerDialog::OnCellClicked(const int row, const int column)
{
    if (row < 0) {
        return;
    }
    if (column == ColorColumn) {
        EditColorAt(row);
    } else if (column == LineTypeColumn) {
        EditLineTypeAt(row);
    } else if (column == LineWeightColumn) {
        EditLineWeightAt(row);
    }
}

void LayerManagerDialog::EditColorAt(const int row)
{
    const std::uint32_t current = ColorFromItem(table_->item(row, ColorColumn), 0xFFFFFFU);
    LayerColorPickerDialog dialog(current, this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    UpdateColorItem(row, dialog.selectedRgb());
}

void LayerManagerDialog::EditLineTypeAt(const int row)
{
    const QString current = table_->item(row, LineTypeColumn)
        ? table_->item(row, LineTypeColumn)->text()
        : QStringLiteral("Continuous");
    LayerLineTypePickerDialog dialog(document_, current.toStdString(), this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    UpdateLineTypeItem(row, QString::fromUtf8(dialog.selectedLineType().c_str()));
}

void LayerManagerDialog::EditLineWeightAt(const int row)
{
    const double current = LineWeightFromItem(table_->item(row, LineWeightColumn), 0.0);
    LayerLineWeightPickerDialog dialog(current, this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    UpdateLineWeightItem(row, dialog.selectedLineWeightMm());
}

void LayerManagerDialog::UpdateLineWeightItem(const int row, const double lineWeightMm)
{
    delete table_->takeItem(row, LineWeightColumn);
    table_->setItem(row, LineWeightColumn, LineWeightItem(lineWeightMm));
}

void LayerManagerDialog::UpdateColorItem(const int row, const std::uint32_t rgb)
{
    delete table_->takeItem(row, ColorColumn);
    table_->setItem(row, ColorColumn, ColorItem(rgb));
}

void LayerManagerDialog::UpdateLineTypeItem(const int row, const QString& lineTypeName)
{
    delete table_->takeItem(row, LineTypeColumn);
    table_->setItem(row, LineTypeColumn, LineTypeItem(lineTypeName));
}

std::string LayerManagerDialog::LayerNameAt(const int row) const
{
    if (row < 0 || row >= table_->rowCount()) {
        return {};
    }
    return table_->item(row, CurrentColumn)->data(Qt::UserRole).toString().toStdString();
}

} // namespace cad
