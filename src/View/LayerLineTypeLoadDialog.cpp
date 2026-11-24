#include "View/LayerLineTypeLoadDialog.h"

#include "IO/LineTypeLibrary.h"

#include <QDialogButtonBox>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>
#include <cstring>
#include <QTemporaryFile>

namespace cad {

namespace {

QString EnsureDefaultLinOnDisk()
{
    const QString path = LineTypeLibrary::DefaultLinFilePath();
    if (QFile::exists(path)) {
        return path;
    }

    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        file.write(LineTypeLibrary::DefaultLinFileContent().toUtf8());
        file.close();
    }
    return path;
}

} // namespace

LayerLineTypeLoadDialog::LayerLineTypeLoadDialog(CadDocument& document, QWidget* parent)
    : QDialog(parent)
    , document_(document)
{
    setWindowTitle(QStringLiteral("加载或重载线型"));
    setModal(true);
    resize(520, 420);

    auto* layout = new QVBoxLayout(this);

    auto* fileRow = new QHBoxLayout();
    fileRow->addWidget(new QLabel(QStringLiteral("文件(F):"), this));
    fileEdit_ = new QLineEdit(this);
    fileEdit_->setText(EnsureDefaultLinOnDisk());
    fileRow->addWidget(fileEdit_, 1);
    auto* browseButton = new QPushButton(QStringLiteral("浏览..."), this);
    connect(browseButton, &QPushButton::clicked, this, [this]() { BrowseFile(); });
    fileRow->addWidget(browseButton);
    layout->addLayout(fileRow);

    layout->addWidget(new QLabel(QStringLiteral("要加载的线型:"), this));

    list_ = new QListWidget(this);
    list_->setSelectionMode(QAbstractItemView::NoSelection);
    layout->addWidget(list_, 1);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, [this]() { LoadSelected(); });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    connect(fileEdit_, &QLineEdit::editingFinished, this, [this]() { ReloadFromFile(); });

    ReloadFromFile();
}

void LayerLineTypeLoadDialog::BrowseFile()
{
    const QString path = QFileDialog::getOpenFileName(
        this,
        QStringLiteral("选择线型文件"),
        QFileInfo(fileEdit_->text()).absolutePath(),
        QStringLiteral("线型文件 (*.lin);;所有文件 (*.*)"));
    if (path.isEmpty()) {
        return;
    }
    fileEdit_->setText(path);
    ReloadFromFile();
}

void LayerLineTypeLoadDialog::ReloadFromFile()
{
    list_->clear();

    QString path = fileEdit_->text().trimmed();
    std::vector<LineTypeDefinition> definitions;
    if (path.isEmpty() || !QFile::exists(path)) {
        definitions = LineTypeLibrary::ParseLinContent(LineTypeLibrary::DefaultLinFileContent());
        path = EnsureDefaultLinOnDisk();
        fileEdit_->setText(path);
    } else {
        definitions = LineTypeLibrary::ParseLinFile(path);
        if (definitions.empty()) {
            definitions = LineTypeLibrary::ParseLinContent(LineTypeLibrary::DefaultLinFileContent());
        }
    }

    for (const LineTypeDefinition& lineType : definitions) {
        if (LineTypeLibrary::HasDefinition(document_, lineType.name)) {
            continue;
        }
        const QString label = QString::fromUtf8(lineType.description.c_str()).isEmpty()
            ? QString::fromUtf8(lineType.name.c_str())
            : QStringLiteral("%1    %2")
                  .arg(QString::fromUtf8(lineType.name.c_str()),
                       QString::fromUtf8(lineType.description.c_str()));
        auto* item = new QListWidgetItem(label, list_);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsEnabled);
        item->setCheckState(Qt::Checked);
        item->setData(Qt::UserRole, QString::fromUtf8(lineType.name.c_str()));
        item->setData(Qt::UserRole + 1, QString::fromUtf8(lineType.description.c_str()));
        QByteArray patternBytes;
        patternBytes.resize(static_cast<int>(lineType.pattern.size() * sizeof(double)));
        if (!lineType.pattern.empty()) {
            std::memcpy(patternBytes.data(), lineType.pattern.data(), patternBytes.size());
        }
        item->setData(Qt::UserRole + 2, patternBytes);
    }

    if (list_->count() == 0) {
        auto* item = new QListWidgetItem(QStringLiteral("文件中的线型已全部加载"), list_);
        item->setFlags(Qt::NoItemFlags);
    }
}

void LayerLineTypeLoadDialog::LoadSelected()
{
    loadedCount_ = 0;
    for (int row = 0; row < list_->count(); ++row) {
        QListWidgetItem* item = list_->item(row);
        if (!item || !(item->flags() & Qt::ItemIsUserCheckable) || item->checkState() != Qt::Checked) {
            continue;
        }

        LineTypeDefinition lineType;
        lineType.name = item->data(Qt::UserRole).toString().toStdString();
        lineType.description = item->data(Qt::UserRole + 1).toString().toStdString();
        const QByteArray patternBytes = item->data(Qt::UserRole + 2).toByteArray();
        if (!patternBytes.isEmpty()) {
            const int count = patternBytes.size() / static_cast<int>(sizeof(double));
            lineType.pattern.resize(count);
            std::memcpy(lineType.pattern.data(), patternBytes.constData(), patternBytes.size());
        }
        document_.AddLineType(std::move(lineType));
        ++loadedCount_;
    }

    if (loadedCount_ == 0) {
        QMessageBox::information(this, QStringLiteral("加载线型"), QStringLiteral("没有选中可加载的线型。"));
        return;
    }

    accept();
}

} // namespace cad
