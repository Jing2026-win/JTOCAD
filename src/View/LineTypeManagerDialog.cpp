#include "View/LineTypeManagerDialog.h"

#include "IO/LineTypeLibrary.h"
#include "View/LayerLineTypeLoadDialog.h"

#include <QAbstractItemView>
#include <QAbstractSpinBox>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QStyledItemDelegate>
#include <QTableWidget>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace cad {

namespace {

enum Column {
    NameColumn = 0,
    AppearanceColumn = 1,
    DescriptionColumn = 2
};

class LineTypeAppearanceDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        QStyleOptionViewItem opt = option;
        initStyleOption(&opt, index);
        painter->save();
        if (opt.state & QStyle::State_Selected) {
            painter->fillRect(opt.rect, QColor(0, 120, 215));
        } else {
            painter->fillRect(opt.rect, opt.palette.base());
        }

        const QByteArray patternBytes = index.data(Qt::UserRole).toByteArray();
        LineTypeDefinition def;
        if (!patternBytes.isEmpty()) {
            const int count = patternBytes.size() / static_cast<int>(sizeof(double));
            def.pattern.resize(static_cast<std::size_t>(count));
            std::memcpy(def.pattern.data(), patternBytes.constData(), patternBytes.size());
        }

        const QColor lineColor = (opt.state & QStyle::State_Selected) ? Qt::white : QColor(30, 30, 30);
        QPen pen(lineColor, 1.4);
        LineTypeLibrary::ApplyPatternToPen(pen, def, 1.0);
        painter->setPen(pen);
        const int y = opt.rect.center().y();
        painter->drawLine(opt.rect.left() + 8, y, opt.rect.right() - 8, y);
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

QByteArray PatternBytes(const std::vector<double>& pattern)
{
    QByteArray bytes;
    bytes.resize(static_cast<int>(pattern.size() * sizeof(double)));
    if (!pattern.empty()) {
        std::memcpy(bytes.data(), pattern.data(), bytes.size());
    }
    return bytes;
}

} // namespace

LineTypeManagerDialog::LineTypeManagerDialog(CadDocument& document, QWidget* parent)
    : QDialog(parent)
    , document_(document)
{
    setWindowTitle(QStringLiteral("线型管理器"));
    setModal(true);
    resize(640, 480);
    setMinimumSize(560, 400);
    setStyleSheet(QStringLiteral(
        "QDialog { background-color: #f0f0f0; color: #000000; }"
        "QLabel { color: #000000; }"
        "QLineEdit, QComboBox, QDoubleSpinBox, QTableWidget {"
        "  background-color: #ffffff; color: #000000; border: 1px solid #a0a0a0;"
        "}"
        "QLineEdit:disabled, QComboBox:disabled, QDoubleSpinBox:disabled {"
        "  background-color: #e8e8e8; color: #6d6d6d;"
        "}"
        "QPushButton {"
        "  background-color: #e1e1e1; color: #000000; border: 1px solid #adadad;"
        "  padding: 3px 10px; min-height: 23px; min-width: 88px;"
        "}"
        "QPushButton:hover { background-color: #cce4f7; border-color: #0078d7; }"
        "QPushButton:disabled { color: #6d6d6d; background-color: #f0f0f0; }"
        "QGroupBox {"
        "  color: #000000; border: 1px solid #a0a0a0; margin-top: 10px;"
        "  padding: 10px 8px 8px 8px; font-weight: normal;"
        "}"
        "QGroupBox::title {"
        "  subcontrol-origin: margin; subcontrol-position: top left;"
        "  left: 8px; padding: 0 4px;"
        "}"
        "QCheckBox { color: #000000; }"
        "QHeaderView::section {"
        "  background-color: #e8e8e8; color: #000000; padding: 4px;"
        "  border: 1px solid #c0c0c0;"
        "}"));

    CaptureSnapshot();
    workingCurrentName_ = CurrentLineTypeDisplayName();
    acceptedCurrentName_ = workingCurrentName_;

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(10, 10, 10, 10);
    root->setSpacing(8);

    // Top: filter group (left) + 2x2 action buttons (right) — matches AutoCAD
    auto* topRow = new QHBoxLayout();
    topRow->setSpacing(8);

    auto* filterGroup = new QGroupBox(QStringLiteral("线型过滤器"), this);
    auto* filterLayout = new QHBoxLayout(filterGroup);
    filterLayout->setContentsMargins(8, 12, 8, 8);
    filterCombo_ = new QComboBox(filterGroup);
    filterCombo_->addItem(QStringLiteral("显示所有线型"), static_cast<int>(FilterMode::All));
    filterCombo_->addItem(QStringLiteral("显示使用的线型"), static_cast<int>(FilterMode::Used));
    filterCombo_->addItem(QStringLiteral("显示未使用的线型"), static_cast<int>(FilterMode::Unused));
    invertFilterCheck_ = new QCheckBox(QStringLiteral("反转过滤器(I)"), filterGroup);
    filterLayout->addWidget(filterCombo_, 1);
    filterLayout->addWidget(invertFilterCheck_);
    topRow->addWidget(filterGroup, 1);

    auto* buttonGrid = new QGridLayout();
    buttonGrid->setContentsMargins(0, 4, 0, 0);
    buttonGrid->setHorizontalSpacing(6);
    buttonGrid->setVerticalSpacing(6);
    auto* loadButton = new QPushButton(QStringLiteral("加载(L)..."), this);
    deleteButton_ = new QPushButton(QStringLiteral("删除"), this);
    auto* currentButton = new QPushButton(QStringLiteral("当前(C)"), this);
    detailsToggleButton_ = new QPushButton(QStringLiteral("隐藏细节(D)"), this);
    buttonGrid->addWidget(loadButton, 0, 0);
    buttonGrid->addWidget(deleteButton_, 0, 1);
    buttonGrid->addWidget(currentButton, 1, 0);
    buttonGrid->addWidget(detailsToggleButton_, 1, 1);
    topRow->addLayout(buttonGrid);
    topRow->setAlignment(buttonGrid, Qt::AlignTop);
    root->addLayout(topRow);

    currentLabel_ = new QLabel(this);
    root->addWidget(currentLabel_);

    table_ = new QTableWidget(this);
    table_->setColumnCount(3);
    table_->setHorizontalHeaderLabels({
        QStringLiteral("线型"),
        QStringLiteral("外观"),
        QStringLiteral("说明")
    });
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setShowGrid(false);
    table_->setAlternatingRowColors(false);
    table_->verticalHeader()->setVisible(false);
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->horizontalHeader()->setHighlightSections(false);
    table_->setColumnWidth(NameColumn, 150);
    table_->setColumnWidth(AppearanceColumn, 200);
    table_->setItemDelegateForColumn(AppearanceColumn, new LineTypeAppearanceDelegate(table_));
    root->addWidget(table_, 1);

    // Details: AutoCAD-style "详细信息" group with 2-column grid
    auto* detailsGroup = new QGroupBox(QStringLiteral("详细信息"), this);
    detailsPanel_ = detailsGroup;
    auto* detailsGrid = new QGridLayout(detailsGroup);
    detailsGrid->setContentsMargins(10, 14, 10, 8);
    detailsGrid->setHorizontalSpacing(16);
    detailsGrid->setVerticalSpacing(8);
    detailsGrid->setColumnStretch(1, 1);
    detailsGrid->setColumnStretch(3, 1);

    auto makeRightLabel = [](const QString& text, QWidget* parent) {
        auto* label = new QLabel(text, parent);
        label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        return label;
    };

    nameEdit_ = new QLineEdit(detailsGroup);
    descriptionEdit_ = new QLineEdit(detailsGroup);
    paperSpaceCheck_ = new QCheckBox(QStringLiteral("缩放时使用图纸空间单位(U)"), detailsGroup);
    paperSpaceCheck_->setChecked(document_.UsePaperSpaceLinetypeScale());

    globalScaleSpin_ = new QDoubleSpinBox(detailsGroup);
    globalScaleSpin_->setDecimals(4);
    globalScaleSpin_->setRange(0.0001, 100000.0);
    globalScaleSpin_->setButtonSymbols(QAbstractSpinBox::NoButtons);
    globalScaleSpin_->setValue(document_.GlobalLinetypeScale());
    objectScaleSpin_ = new QDoubleSpinBox(detailsGroup);
    objectScaleSpin_->setDecimals(4);
    objectScaleSpin_->setRange(0.0001, 100000.0);
    objectScaleSpin_->setButtonSymbols(QAbstractSpinBox::NoButtons);
    objectScaleSpin_->setValue(document_.CurrentLinetypeScale());
    isoPenCombo_ = new QComboBox(detailsGroup);
    const double isoPens[] = {0.13, 0.18, 0.25, 0.35, 0.50, 0.70, 1.0, 1.4, 2.0};
    for (double mm : isoPens) {
        isoPenCombo_->addItem(QStringLiteral("%1 毫米").arg(mm, 0, 'f', 1), mm);
    }
    isoPenCombo_->setCurrentIndex(6);

    detailsGrid->addWidget(makeRightLabel(QStringLiteral("名称(N)"), detailsGroup), 0, 0);
    detailsGrid->addWidget(nameEdit_, 0, 1);
    detailsGrid->addWidget(makeRightLabel(QStringLiteral("全局比例因子(G)"), detailsGroup), 0, 2);
    detailsGrid->addWidget(globalScaleSpin_, 0, 3);

    detailsGrid->addWidget(makeRightLabel(QStringLiteral("说明(E)"), detailsGroup), 1, 0);
    detailsGrid->addWidget(descriptionEdit_, 1, 1);
    detailsGrid->addWidget(makeRightLabel(QStringLiteral("当前对象缩放比例(O)"), detailsGroup), 1, 2);
    detailsGrid->addWidget(objectScaleSpin_, 1, 3);

    detailsGrid->addWidget(paperSpaceCheck_, 2, 0, 1, 2);
    detailsGrid->addWidget(makeRightLabel(QStringLiteral("ISO 笔宽(P)"), detailsGroup), 2, 2);
    detailsGrid->addWidget(isoPenCombo_, 2, 3);
    root->addWidget(detailsGroup);

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Help, this);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("确定"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
    buttons->button(QDialogButtonBox::Help)->setText(QStringLiteral("帮助(H)"));
    buttons->button(QDialogButtonBox::Ok)->setMinimumWidth(75);
    buttons->button(QDialogButtonBox::Cancel)->setMinimumWidth(75);
    buttons->button(QDialogButtonBox::Help)->setMinimumWidth(75);
    root->addWidget(buttons);

    connect(filterCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() { OnFilterChanged(); });
    connect(invertFilterCheck_, &QCheckBox::toggled, this, [this]() { OnFilterChanged(); });
    connect(loadButton, &QPushButton::clicked, this, [this]() { OnLoad(); });
    connect(deleteButton_, &QPushButton::clicked, this, [this]() { OnDelete(); });
    connect(currentButton, &QPushButton::clicked, this, [this]() { OnSetCurrent(); });
    connect(detailsToggleButton_, &QPushButton::clicked, this, [this]() { ToggleDetails(); });
    connect(table_, &QTableWidget::itemSelectionChanged, this, [this]() { OnSelectionChanged(); });
    connect(table_, &QTableWidget::cellDoubleClicked, this, [this](int row, int column) { OnRenameCell(row, column); });
    connect(nameEdit_, &QLineEdit::editingFinished, this, [this]() { ApplyDetailsToSelection(); });
    connect(descriptionEdit_, &QLineEdit::editingFinished, this, [this]() { ApplyDetailsToSelection(); });
    connect(paperSpaceCheck_, &QCheckBox::toggled, this, [this](bool on) {
        if (!updatingUi_) {
            document_.SetUsePaperSpaceLinetypeScale(on);
        }
    });
    connect(globalScaleSpin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double v) {
        if (!updatingUi_) {
            document_.SetGlobalLinetypeScale(v);
        }
    });
    connect(objectScaleSpin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double v) {
        if (!updatingUi_) {
            document_.SetCurrentLinetypeScale(v);
        }
    });
    connect(isoPenCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() {
        ApplyDetailsToSelection();
    });
    connect(buttons, &QDialogButtonBox::accepted, this, [this]() { OnAccept(); });
    connect(buttons, &QDialogButtonBox::rejected, this, [this]() {
        RestoreSnapshot();
        reject();
    });
    connect(buttons->button(QDialogButtonBox::Help), &QPushButton::clicked, this, [this]() {
        QMessageBox::information(
            this,
            QStringLiteral("线型管理器"),
            QStringLiteral("用于加载、删除线型，设置当前线型，并调整全局/对象线型比例 (LTSCALE)。\n"
                           "规范制图建议保持当前线型为 ByLayer。"));
    });

    PopulateTable();
    UpdateCurrentLabel();
    int selectRow = 0;
    for (int r = 0; r < table_->rowCount(); ++r) {
        if (table_->item(r, NameColumn)->data(Qt::UserRole).toString().toStdString() == workingCurrentName_) {
            selectRow = r;
            break;
        }
    }
    if (table_->rowCount() > 0) {
        table_->selectRow(selectRow);
    }
    UpdateDetailsFromSelection();
}

void LineTypeManagerDialog::CaptureSnapshot()
{
    snapshot_.lineTypes = document_.LineTypes();
    snapshot_.currentStyle = document_.CurrentEntityStyle();
    snapshot_.globalScale = document_.GlobalLinetypeScale();
    snapshot_.currentScale = document_.CurrentLinetypeScale();
    snapshot_.usePaperSpace = document_.UsePaperSpaceLinetypeScale();
    snapshot_.layerLineTypes.clear();
    for (const Layer& layer : document_.Layers()) {
        snapshot_.layerLineTypes.emplace_back(layer.Name(), layer.LineTypeName());
    }
    snapshot_.entityStyles.clear();
    for (const auto& entity : document_.Entities()) {
        if (entity) {
            snapshot_.entityStyles.emplace_back(entity->Id(), entity->Style());
        }
    }
}

void LineTypeManagerDialog::RestoreSnapshot()
{
    document_.ReplaceLineTypes(snapshot_.lineTypes);
    for (const auto& entry : snapshot_.layerLineTypes) {
        if (Layer* layer = document_.FindLayer(entry.first)) {
            layer->SetLineTypeName(entry.second);
        }
    }
    for (const auto& entry : snapshot_.entityStyles) {
        if (Entity* entity = document_.FindEntity(entry.first)) {
            entity->SetStyle(entry.second);
        }
    }
    document_.SetCurrentEntityStyle(snapshot_.currentStyle);
    document_.SetGlobalLinetypeScale(snapshot_.globalScale);
    document_.SetCurrentLinetypeScale(snapshot_.currentScale);
    document_.SetUsePaperSpaceLinetypeScale(snapshot_.usePaperSpace);
    document_.NotifyAllEntitiesChanged();
}

bool LineTypeManagerDialog::IsSpecialName(const std::string& name) const
{
    return name == "ByLayer" || name == "ByBlock" || name == "Continuous";
}

std::string LineTypeManagerDialog::CurrentLineTypeDisplayName() const
{
    const EntityStyle& style = document_.CurrentEntityStyle();
    if (style.lineTypeSource == PropertySource::ByLayer) {
        return "ByLayer";
    }
    if (style.lineTypeSource == PropertySource::ByBlock) {
        return "ByBlock";
    }
    return style.lineTypeName.empty() ? "Continuous" : style.lineTypeName;
}

void LineTypeManagerDialog::SetDocumentCurrentLineType(const std::string& name)
{
    EntityStyle style = document_.CurrentEntityStyle();
    if (name == "ByLayer") {
        style.lineTypeSource = PropertySource::ByLayer;
    } else if (name == "ByBlock") {
        style.lineTypeSource = PropertySource::ByBlock;
        style.lineTypeName = "Continuous";
        style.linePattern = LinePattern::Continuous;
    } else {
        style.lineTypeSource = PropertySource::Override;
        style.lineTypeName = name;
        style.linePattern = LinePatternFromLineTypeName(name);
    }
    style.lineTypeScale = document_.CurrentLinetypeScale();
    document_.SetCurrentEntityStyle(style);
    workingCurrentName_ = name;
}

bool LineTypeManagerDialog::PassesFilter(const std::string& name) const
{
    FilterMode mode = FilterMode::All;
    if (filterCombo_) {
        mode = static_cast<FilterMode>(filterCombo_->currentData().toInt());
    }

    bool used = false;
    if (name == "ByLayer" || name == "ByBlock") {
        used = true;
    } else if (name == "Continuous") {
        used = true;
    } else {
        used = document_.IsLineTypeInUse(name);
    }

    bool pass = true;
    if (mode == FilterMode::Used) {
        pass = used;
    } else if (mode == FilterMode::Unused) {
        pass = !used;
    }
    if (invertFilterCheck_ && invertFilterCheck_->isChecked()) {
        pass = !pass;
    }
    return pass;
}

void LineTypeManagerDialog::PopulateTable()
{
    updatingUi_ = true;
    table_->setRowCount(0);

    const auto addRow = [&](const QString& name, const QString& description, const std::vector<double>& pattern, bool renameable) {
        if (!PassesFilter(name.toStdString())) {
            return;
        }
        const int row = table_->rowCount();
        table_->insertRow(row);
        auto* nameItem = new QTableWidgetItem(name);
        nameItem->setData(Qt::UserRole, name);
        nameItem->setFlags(renameable
            ? (Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsEditable)
            : (Qt::ItemIsSelectable | Qt::ItemIsEnabled));
        table_->setItem(row, NameColumn, nameItem);

        auto* appearanceItem = new QTableWidgetItem();
        appearanceItem->setData(Qt::UserRole, PatternBytes(pattern));
        appearanceItem->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);
        table_->setItem(row, AppearanceColumn, appearanceItem);

        auto* descItem = new QTableWidgetItem(description);
        descItem->setFlags(renameable
            ? (Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsEditable)
            : (Qt::ItemIsSelectable | Qt::ItemIsEnabled));
        table_->setItem(row, DescriptionColumn, descItem);
        table_->setRowHeight(row, 28);
    };

    addRow(QStringLiteral("ByLayer"), QStringLiteral("随层"), {12.0, -3.0}, false);
    addRow(QStringLiteral("ByBlock"), QStringLiteral("随块"), {}, false);

    for (const LineTypeDefinition& lt : document_.LineTypes()) {
        const bool renameable = !IsSpecialName(lt.name);
        addRow(
            QString::fromUtf8(lt.name.c_str()),
            QString::fromUtf8(lt.description.c_str()),
            lt.pattern,
            renameable);
    }

    updatingUi_ = false;
    UpdateCurrentLabel();
}

void LineTypeManagerDialog::UpdateCurrentLabel()
{
    currentLabel_->setText(
        QStringLiteral("当前线型: %1").arg(QString::fromUtf8(workingCurrentName_.c_str())));
}

void LineTypeManagerDialog::UpdateDetailsFromSelection()
{
    updatingUi_ = true;
    const int row = table_->currentRow();
    if (row < 0) {
        nameEdit_->clear();
        descriptionEdit_->clear();
        nameEdit_->setEnabled(false);
        descriptionEdit_->setEnabled(false);
        isoPenCombo_->setEnabled(false);
        if (deleteButton_) {
            deleteButton_->setEnabled(false);
        }
        updatingUi_ = false;
        return;
    }

    const std::string name = table_->item(row, NameColumn)->data(Qt::UserRole).toString().toStdString();
    nameEdit_->setText(QString::fromUtf8(name.c_str()));
    descriptionEdit_->setText(table_->item(row, DescriptionColumn)->text());

    const bool special = IsSpecialName(name);
    nameEdit_->setEnabled(!special);
    descriptionEdit_->setEnabled(!special);
    if (deleteButton_) {
        deleteButton_->setEnabled(document_.CanDeleteLineType(name));
    }

    paperSpaceCheck_->setChecked(document_.UsePaperSpaceLinetypeScale());
    globalScaleSpin_->setValue(document_.GlobalLinetypeScale());
    objectScaleSpin_->setValue(document_.CurrentLinetypeScale());

    if (const LineTypeDefinition* lt = document_.FindLineType(name)) {
        isoPenCombo_->setEnabled(!special);
        int best = 6;
        for (int i = 0; i < isoPenCombo_->count(); ++i) {
            if (std::abs(isoPenCombo_->itemData(i).toDouble() - lt->isoPenWidthMm) < 0.001) {
                best = i;
                break;
            }
        }
        isoPenCombo_->setCurrentIndex(best);
    } else {
        isoPenCombo_->setEnabled(false);
    }
    updatingUi_ = false;
}

void LineTypeManagerDialog::ApplyDetailsToSelection()
{
    if (updatingUi_) {
        return;
    }
    const int row = table_->currentRow();
    if (row < 0) {
        return;
    }
    const std::string oldName = table_->item(row, NameColumn)->data(Qt::UserRole).toString().toStdString();
    if (IsSpecialName(oldName)) {
        return;
    }
    LineTypeDefinition* lt = document_.FindLineType(oldName);
    if (!lt) {
        return;
    }

    const QString newNameText = nameEdit_->text().trimmed();
    if (!newNameText.isEmpty() && newNameText.toStdString() != oldName) {
        if (!document_.RenameLineType(oldName, newNameText.toStdString())) {
            QMessageBox::warning(this, QStringLiteral("重命名线型"), QStringLiteral("无法重命名该线型（名称冲突或无效）。"));
            nameEdit_->setText(QString::fromUtf8(oldName.c_str()));
        } else {
            if (workingCurrentName_ == oldName) {
                workingCurrentName_ = newNameText.toStdString();
            }
            PopulateTable();
            // Reselect
            for (int r = 0; r < table_->rowCount(); ++r) {
                if (table_->item(r, NameColumn)->data(Qt::UserRole).toString() == newNameText) {
                    table_->selectRow(r);
                    break;
                }
            }
            UpdateCurrentLabel();
            return;
        }
    }

    lt->description = descriptionEdit_->text().trimmed().toStdString();
    lt->isoPenWidthMm = isoPenCombo_->currentData().toDouble();
    table_->item(row, DescriptionColumn)->setText(descriptionEdit_->text().trimmed());
}

void LineTypeManagerDialog::OnFilterChanged()
{
    PopulateTable();
}

void LineTypeManagerDialog::OnSelectionChanged()
{
    UpdateDetailsFromSelection();
}

void LineTypeManagerDialog::OnLoad()
{
    LayerLineTypeLoadDialog dialog(document_, this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    PopulateTable();
}

void LineTypeManagerDialog::OnDelete()
{
    const int row = table_->currentRow();
    if (row < 0) {
        return;
    }
    const std::string name = table_->item(row, NameColumn)->data(Qt::UserRole).toString().toStdString();
    if (IsSpecialName(name)) {
        QMessageBox::warning(this, QStringLiteral("删除线型"), QStringLiteral("不能删除 ByLayer、ByBlock 或 Continuous。"));
        return;
    }
    if (!document_.CanDeleteLineType(name)) {
        QMessageBox::warning(
            this,
            QStringLiteral("删除线型"),
            QStringLiteral("不能删除正在被图形或图层使用的线型。"));
        return;
    }
    if (QMessageBox::question(this, QStringLiteral("删除线型"), QStringLiteral("确定删除线型 %1 吗？").arg(QString::fromUtf8(name.c_str())))
        != QMessageBox::Yes) {
        return;
    }
    document_.RemoveLineType(name);
    if (workingCurrentName_ == name) {
        SetDocumentCurrentLineType("ByLayer");
    }
    PopulateTable();
    UpdateCurrentLabel();
}

void LineTypeManagerDialog::OnSetCurrent()
{
    const int row = table_->currentRow();
    if (row < 0) {
        return;
    }
    const std::string name = table_->item(row, NameColumn)->data(Qt::UserRole).toString().toStdString();
    SetDocumentCurrentLineType(name);
    UpdateCurrentLabel();
}

void LineTypeManagerDialog::ToggleDetails()
{
    const bool visible = detailsPanel_->isVisible();
    detailsPanel_->setVisible(!visible);
    detailsToggleButton_->setText(visible ? QStringLiteral("显示细节(D)") : QStringLiteral("隐藏细节(D)"));
}

void LineTypeManagerDialog::OnRenameCell(int row, int column)
{
    if (column != NameColumn && column != DescriptionColumn) {
        return;
    }
    if (row < 0) {
        return;
    }
    const std::string name = table_->item(row, NameColumn)->data(Qt::UserRole).toString().toStdString();
    if (IsSpecialName(name)) {
        return;
    }
    if (column == NameColumn) {
        bool ok = false;
        const QString text = QInputDialog::getText(
            this,
            QStringLiteral("重命名线型"),
            QStringLiteral("新名称"),
            QLineEdit::Normal,
            QString::fromUtf8(name.c_str()),
            &ok).trimmed();
        if (!ok || text.isEmpty() || text.toStdString() == name) {
            return;
        }
        if (!document_.RenameLineType(name, text.toStdString())) {
            QMessageBox::warning(this, QStringLiteral("重命名线型"), QStringLiteral("无法重命名该线型。"));
            return;
        }
        if (workingCurrentName_ == name) {
            workingCurrentName_ = text.toStdString();
        }
        PopulateTable();
        UpdateCurrentLabel();
    }
}

void LineTypeManagerDialog::OnAccept()
{
    ApplyDetailsToSelection();
    SetDocumentCurrentLineType(workingCurrentName_);
    acceptedCurrentName_ = workingCurrentName_;
    accept();
}

} // namespace cad
