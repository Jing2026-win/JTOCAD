#include "View/TextEditorDialog.h"

#include "View/TextRenderUtil.h"
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QColorDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QFile>
#include <QKeyEvent>
#include <QLabel>
#include <QRegularExpression>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QTextDocument>
#include <QToolButton>
#include <QVBoxLayout>

namespace cad {

namespace {

TextHorizontalAlign HAlignFromIndex(int i)
{
    switch (i) {
    case 1: return TextHorizontalAlign::Center;
    case 2: return TextHorizontalAlign::Right;
    default: return TextHorizontalAlign::Left;
    }
}

int HAlignIndex(TextHorizontalAlign a)
{
    switch (a) {
    case TextHorizontalAlign::Center: return 1;
    case TextHorizontalAlign::Right: return 2;
    default: return 0;
    }
}

TextVerticalAlign VAlignFromIndex(int i)
{
    switch (i) {
    case 1: return TextVerticalAlign::Bottom;
    case 2: return TextVerticalAlign::Middle;
    case 3: return TextVerticalAlign::Top;
    default: return TextVerticalAlign::Baseline;
    }
}

int VAlignIndex(TextVerticalAlign a)
{
    switch (a) {
    case TextVerticalAlign::Bottom: return 1;
    case TextVerticalAlign::Middle: return 2;
    case TextVerticalAlign::Top: return 3;
    default: return 0;
    }
}

MTextAttachment AttachmentFromIndex(int i) { return static_cast<MTextAttachment>(i + 1); }
int AttachmentIndex(MTextAttachment a) { return static_cast<int>(a) - 1; }

MTextParagraphAlign ParaFromIndex(int i)
{
    switch (i) {
    case 1: return MTextParagraphAlign::Center;
    case 2: return MTextParagraphAlign::Right;
    case 3: return MTextParagraphAlign::Justify;
    default: return MTextParagraphAlign::Left;
    }
}

int ParaIndex(MTextParagraphAlign a)
{
    switch (a) {
    case MTextParagraphAlign::Center: return 1;
    case MTextParagraphAlign::Right: return 2;
    case MTextParagraphAlign::Justify: return 3;
    default: return 0;
    }
}

void PopulateStyles(QComboBox* combo, const CadDocument& document)
{
    combo->clear();
    for (const TextStyleDefinition& style : document.TextStyles()) {
        combo->addItem(QString::fromUtf8(style.name.c_str()));
    }
    if (combo->count() == 0) {
        combo->addItem(QStringLiteral("Standard"));
    }
}

QString ColorButtonText(const EntityStyle& style)
{
    return QStringLiteral("RGB %1,%2,%3").arg(style.red).arg(style.green).arg(style.blue);
}

} // namespace

SingleLineTextEditorDialog::SingleLineTextEditorDialog(const CadDocument& document, QWidget* parent)
    : QDialog(parent)
    , document_(document)
{
    setWindowTitle(tr("单行文字编辑器"));
    resize(520, 320);
    BuildUi();
}

void SingleLineTextEditorDialog::BuildUi()
{
    textEdit_ = new QLineEdit(this);
    styleCombo_ = new QComboBox(this);
    PopulateStyles(styleCombo_, document_);
    heightSpin_ = new QDoubleSpinBox(this);
    heightSpin_->setRange(0.1, 10000.0);
    heightSpin_->setValue(12.0);
    rotationSpin_ = new QDoubleSpinBox(this);
    rotationSpin_->setRange(-360.0, 360.0);
    widthFactorSpin_ = new QDoubleSpinBox(this);
    widthFactorSpin_->setRange(0.1, 10.0);
    widthFactorSpin_->setValue(1.0);
    obliqueSpin_ = new QDoubleSpinBox(this);
    obliqueSpin_->setRange(-85.0, 85.0);
    hAlignCombo_ = new QComboBox(this);
    hAlignCombo_->addItems({tr("左对齐"), tr("居中"), tr("右对齐")});
    vAlignCombo_ = new QComboBox(this);
    vAlignCombo_->addItems({tr("基线"), tr("底端"), tr("居中"), tr("顶端")});
    colorButton_ = new QPushButton(ColorButtonText(style_), this);
    connect(colorButton_, &QPushButton::clicked, this, [this]() { ChooseColor(); });

    auto* clipBar = new QToolBar(this);
    clipBar->addAction(tr("剪切"), textEdit_, &QLineEdit::cut);
    clipBar->addAction(tr("复制"), textEdit_, &QLineEdit::copy);
    clipBar->addAction(tr("粘贴"), textEdit_, &QLineEdit::paste);
    clipBar->addAction(tr("选择性粘贴"), this, [this]() {
        textEdit_->setText(QApplication::clipboard()->text());
    });

    auto* symBar = new QToolBar(this);
    const struct { const char* label; const char* sym; } symbols[] = {
        { "φ", "\xCF\x86" }, { "°", "\xC2\xB0" }, { "±", "\xC2\xB1" }, { "×", "\xC3\x97" }
    };
    for (const auto& s : symbols) {
        symBar->addAction(QString::fromUtf8(s.label), this, [this, sym = QString::fromUtf8(s.sym)]() {
            InsertSymbol(sym);
        });
    }

    auto* form = new QFormLayout();
    form->addRow(tr("文字内容"), textEdit_);
    form->addRow(tr("文字样式"), styleCombo_);
    form->addRow(tr("文字高度"), heightSpin_);
    form->addRow(tr("旋转角度"), rotationSpin_);
    form->addRow(tr("宽度比例"), widthFactorSpin_);
    form->addRow(tr("倾斜角度"), obliqueSpin_);
    form->addRow(tr("水平对齐"), hAlignCombo_);
    form->addRow(tr("垂直对齐"), vAlignCombo_);
    form->addRow(tr("文字颜色"), colorButton_);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel(tr("单行文字：适用于简短标注；Ctrl+Enter 确定，Esc 取消。"), this));
    layout->addWidget(clipBar);
    layout->addWidget(symBar);
    layout->addLayout(form);
    layout->addWidget(buttons);
}

void SingleLineTextEditorDialog::ChooseColor()
{
    const QColor color = QColorDialog::getColor(QColor(style_.red, style_.green, style_.blue), this, tr("文字颜色"));
    if (!color.isValid()) return;
    style_.red = static_cast<std::uint8_t>(color.red());
    style_.green = static_cast<std::uint8_t>(color.green());
    style_.blue = static_cast<std::uint8_t>(color.blue());
    colorButton_->setText(ColorButtonText(style_));
}

void SingleLineTextEditorDialog::InsertSymbol(const QString& symbol)
{
    textEdit_->insert(symbol);
}

void SingleLineTextEditorDialog::SetOptions(const SingleLineTextOptions& options)
{
    textEdit_->setText(QString::fromUtf8(options.text.c_str()));
    heightSpin_->setValue(options.height);
    rotationSpin_->setValue(options.rotationDegrees);
    widthFactorSpin_->setValue(options.widthFactor);
    obliqueSpin_->setValue(options.obliqueDegrees);
    hAlignCombo_->setCurrentIndex(HAlignIndex(options.horizontalAlign));
    vAlignCombo_->setCurrentIndex(VAlignIndex(options.verticalAlign));
    style_ = options.style;
    colorButton_->setText(ColorButtonText(style_));
    const int idx = styleCombo_->findText(QString::fromUtf8(options.textStyle.c_str()));
    if (idx >= 0) styleCombo_->setCurrentIndex(idx);
}

SingleLineTextOptions SingleLineTextEditorDialog::Options() const
{
    SingleLineTextOptions options;
    options.text = textEdit_->text().toUtf8().constData();
    options.height = heightSpin_->value();
    options.rotationDegrees = rotationSpin_->value();
    options.widthFactor = widthFactorSpin_->value();
    options.obliqueDegrees = obliqueSpin_->value();
    options.horizontalAlign = HAlignFromIndex(hAlignCombo_->currentIndex());
    options.verticalAlign = VAlignFromIndex(vAlignCombo_->currentIndex());
    options.textStyle = styleCombo_->currentText().toUtf8().constData();
    options.style = style_;
    options.style.textStyle = options.textStyle;
    return options;
}

void SingleLineTextEditorDialog::keyPressEvent(QKeyEvent* event)
{
    if (event->matches(QKeySequence::Cancel)) {
        reject();
        return;
    }
    if ((event->modifiers() & Qt::ControlModifier) && event->key() == Qt::Key_Return) {
        accept();
        return;
    }
    QDialog::keyPressEvent(event);
}

MTextEditorDialog::MTextEditorDialog(const CadDocument& document, QWidget* parent)
    : QDialog(parent)
    , document_(document)
{
    setWindowTitle(tr("在位文字编辑器"));
    resize(780, 580);
    BuildUi();
    editor_->installEventFilter(this);
}

void MTextEditorDialog::BuildUi()
{
    editor_ = new QTextEdit(this);
    editor_->setAcceptRichText(false);

    findPanel_ = new QWidget(this);
    findEdit_ = new QLineEdit(findPanel_);
    replaceEdit_ = new QLineEdit(findPanel_);
    findEdit_->setPlaceholderText(tr("查找"));
    replaceEdit_->setPlaceholderText(tr("替换为"));
    auto* findNextBtn = new QPushButton(tr("查找下一个"), findPanel_);
    auto* replaceBtn = new QPushButton(tr("替换"), findPanel_);
    auto* replaceAllBtn = new QPushButton(tr("全部替换"), findPanel_);
    connect(findNextBtn, &QPushButton::clicked, this, &MTextEditorDialog::FindNext);
    connect(replaceBtn, &QPushButton::clicked, this, &MTextEditorDialog::ReplaceOne);
    connect(replaceAllBtn, &QPushButton::clicked, this, &MTextEditorDialog::ReplaceAll);
    auto* findLayout = new QHBoxLayout(findPanel_);
    findLayout->addWidget(findEdit_);
    findLayout->addWidget(replaceEdit_);
    findLayout->addWidget(findNextBtn);
    findLayout->addWidget(replaceBtn);
    findLayout->addWidget(replaceAllBtn);
    findPanel_->setVisible(false);

    auto* bar1 = new QToolBar(tr("剪贴板"), this);
    BuildClipboardToolbar(bar1);
    auto* bar2 = new QToolBar(tr("格式"), this);
    BuildFormatToolbar(bar2);
    auto* bar3 = new QToolBar(tr("段落"), this);
    BuildParagraphToolbar(bar3);
    auto* bar4 = new QToolBar(tr("插入"), this);
    BuildInsertToolbar(bar4);

    styleCombo_ = new QComboBox(this);
    PopulateStyles(styleCombo_, document_);
    heightSpin_ = new QDoubleSpinBox(this);
    heightSpin_->setRange(0.1, 10000.0);
    heightSpin_->setValue(12.0);
    rotationSpin_ = new QDoubleSpinBox(this);
    rotationSpin_->setRange(-360.0, 360.0);
    widthSpin_ = new QDoubleSpinBox(this);
    widthSpin_->setRange(0.0, 100000.0);
    widthSpin_->setSpecialValueText(tr("自动"));
    lineSpacingSpin_ = new QDoubleSpinBox(this);
    lineSpacingSpin_->setRange(0.5, 3.0);
    lineSpacingSpin_->setSingleStep(0.1);
    lineSpacingSpin_->setValue(1.0);
    attachmentCombo_ = new QComboBox(this);
    attachmentCombo_->addItems({tr("左上"), tr("中上"), tr("右上"), tr("左中"), tr("正中"), tr("右中"), tr("左下"), tr("中下"), tr("右下")});
    paragraphAlignCombo_ = new QComboBox(this);
    paragraphAlignCombo_->addItems({tr("左对齐"), tr("居中"), tr("右对齐"), tr("两端对齐")});
    backgroundMaskCheck_ = new QCheckBox(tr("背景遮罩"), this);
    colorButton_ = new QPushButton(ColorButtonText(style_), this);
    connect(colorButton_, &QPushButton::clicked, this, [this]() { ChooseColor(); });

    auto* props = new QFormLayout();
    props->addRow(tr("文字样式"), styleCombo_);
    props->addRow(tr("文字高度"), heightSpin_);
    props->addRow(tr("旋转角度"), rotationSpin_);
    props->addRow(tr("文本框宽度"), widthSpin_);
    props->addRow(tr("行距因子"), lineSpacingSpin_);
    props->addRow(tr("附着点"), attachmentCombo_);
    props->addRow(tr("段落对齐"), paragraphAlignCombo_);
    props->addRow(tr("文字颜色"), colorButton_);
    props->addRow(QString(), backgroundMaskCheck_);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("关闭文字编辑器"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel(tr("多行文字编辑器：支持 AutoCAD 格式码、段落排版、符号/字段/堆叠/遮罩；Ctrl+Enter 保存，Esc 放弃。"), this));
    layout->addWidget(bar1);
    layout->addWidget(bar2);
    layout->addWidget(bar3);
    layout->addWidget(bar4);
    layout->addWidget(findPanel_);
    layout->addWidget(editor_, 1);
    layout->addLayout(props);
    layout->addWidget(buttons);
}

void MTextEditorDialog::BuildClipboardToolbar(QToolBar* bar)
{
    bar->addAction(tr("剪切"), editor_, &QTextEdit::cut);
    bar->addAction(tr("复制"), editor_, &QTextEdit::copy);
    bar->addAction(tr("粘贴"), editor_, &QTextEdit::paste);
    bar->addAction(tr("选择性粘贴"), this, &MTextEditorDialog::PastePlain);
    bar->addSeparator();
    bar->addAction(tr("查找和替换"), this, &MTextEditorDialog::ToggleFindReplace);
}

void MTextEditorDialog::BuildFormatToolbar(QToolBar* bar)
{
    bar->addAction(tr("下划线\\L"), this, [this]() { InsertAtCursor(QStringLiteral("\\L")); });
    bar->addAction(tr("上划线\\O"), this, [this]() { InsertAtCursor(QStringLiteral("\\O")); });
    bar->addAction(tr("删除线\\K"), this, [this]() { InsertAtCursor(QStringLiteral("\\K")); });
    bar->addSeparator();
    bar->addAction(tr("堆叠\\S"), this, &MTextEditorDialog::InsertStacked);
    bar->addAction(tr("清除格式"), this, &MTextEditorDialog::ClearSelectionFormat);
    bar->addSeparator();
    bar->addAction(tr("大写"), this, [this]() { ApplyCase(true, false, false); });
    bar->addAction(tr("小写"), this, [this]() { ApplyCase(false, true, false); });
    bar->addAction(tr("首字母大写"), this, [this]() { ApplyCase(false, false, true); });
}

void MTextEditorDialog::BuildParagraphToolbar(QToolBar* bar)
{
    bar->addAction(tr("换行\\P"), this, [this]() { InsertAtCursor(QStringLiteral("\\P")); });
    bar->addAction(tr("制表符"), this, [this]() { InsertAtCursor(QStringLiteral("\t")); });
    bar->addAction(tr("项目符号"), this, [this]() { InsertAtCursor(QStringLiteral("\\pxt;\\U+2022 ")); });
    bar->addSeparator();
    bar->addAction(tr("左对齐"), this, [this]() { paragraphAlignCombo_->setCurrentIndex(0); });
    bar->addAction(tr("居中"), this, [this]() { paragraphAlignCombo_->setCurrentIndex(1); });
    bar->addAction(tr("右对齐"), this, [this]() { paragraphAlignCombo_->setCurrentIndex(2); });
    bar->addAction(tr("两端对齐"), this, [this]() { paragraphAlignCombo_->setCurrentIndex(3); });
}

void MTextEditorDialog::BuildInsertToolbar(QToolBar* bar)
{
    auto* symMenu = new QMenu(tr("插入符号"), bar);
    const struct { const char* name; const char* code; } syms[] = {
        {"直径 φ", "%%c"}, {"度数 °", "%%d"}, {"正负 ±", "%%p"}, {"不换行空格", "\\~"}
    };
    for (const auto& s : syms) {
        symMenu->addAction(QString::fromUtf8(s.name), this, [this, c = QString::fromUtf8(s.code)]() { InsertAtCursor(c); });
    }
    auto* symBtn = new QToolButton(bar);
    symBtn->setText(tr("符号"));
    symBtn->setMenu(symMenu);
    symBtn->setPopupMode(QToolButton::InstantPopup);
    bar->addWidget(symBtn);

    auto* fieldMenu = new QMenu(tr("插入字段"), bar);
    fieldMenu->addAction(tr("日期 {DATE}"), this, [this]() { InsertAtCursor(QStringLiteral("{DATE}")); });
    fieldMenu->addAction(tr("时间 {TIME}"), this, [this]() { InsertAtCursor(QStringLiteral("{TIME}")); });
    fieldMenu->addAction(tr("文件名 {FILENAME}"), this, [this]() { InsertAtCursor(QStringLiteral("{FILENAME}")); });
    auto* fieldBtn = new QToolButton(bar);
    fieldBtn->setText(tr("字段"));
    fieldBtn->setMenu(fieldMenu);
    fieldBtn->setPopupMode(QToolButton::InstantPopup);
    bar->addWidget(fieldBtn);

    bar->addAction(tr("导入 TXT/RTF"), this, &MTextEditorDialog::ImportTextFile);
}

void MTextEditorDialog::ToggleFindReplace() { findPanel_->setVisible(!findPanel_->isVisible()); }

void MTextEditorDialog::FindNext()
{
    if (!editor_->find(findEdit_->text())) {
        editor_->moveCursor(QTextCursor::Start);
        editor_->find(findEdit_->text());
    }
}

void MTextEditorDialog::ReplaceOne()
{
    if (editor_->textCursor().hasSelection() && editor_->textCursor().selectedText() == findEdit_->text()) {
        editor_->textCursor().insertText(replaceEdit_->text());
    }
    FindNext();
}

void MTextEditorDialog::ReplaceAll()
{
    editor_->moveCursor(QTextCursor::Start);
    int count = 0;
    while (editor_->find(findEdit_->text())) {
        editor_->textCursor().insertText(replaceEdit_->text());
        ++count;
    }
    QMessageBox::information(this, tr("全部替换"), tr("已替换 %1 处。").arg(count));
}

void MTextEditorDialog::PastePlain() { editor_->insertPlainText(QApplication::clipboard()->text()); }

void MTextEditorDialog::ImportTextFile()
{
    const QString path = QFileDialog::getOpenFileName(this, tr("导入文字文件"), {}, tr("文本 (*.txt);;RTF (*.rtf);;所有 (*.*)"));
    if (path.isEmpty()) return;
    if (path.endsWith(QStringLiteral(".rtf"), Qt::CaseInsensitive)) {
        QTextDocument doc;
        doc.setHtml(QString());
        if (QFile file(path); file.open(QIODevice::ReadOnly)) {
            doc.setPlainText(QString::fromUtf8(file.readAll()));
        }
        editor_->setPlainText(doc.toPlainText());
    } else if (QFile file(path); file.open(QIODevice::ReadOnly)) {
        editor_->setPlainText(QString::fromUtf8(file.readAll()));
    }
}

void MTextEditorDialog::ChooseColor()
{
    const QColor color = QColorDialog::getColor(QColor(style_.red, style_.green, style_.blue), this, tr("文字颜色"));
    if (!color.isValid()) return;
    style_.red = static_cast<std::uint8_t>(color.red());
    style_.green = static_cast<std::uint8_t>(color.green());
    style_.blue = static_cast<std::uint8_t>(color.blue());
    colorButton_->setText(ColorButtonText(style_));
}

void MTextEditorDialog::InsertAtCursor(const QString& text) { editor_->insertPlainText(text); }

void MTextEditorDialog::WrapSelection(const QString& prefix, const QString& suffix)
{
    QTextCursor cursor = editor_->textCursor();
    if (!cursor.hasSelection()) {
        InsertAtCursor(prefix + suffix);
        return;
    }
    cursor.insertText(prefix + cursor.selectedText() + suffix);
}

void MTextEditorDialog::InsertStacked()
{
    QTextCursor cursor = editor_->textCursor();
    if (cursor.hasSelection()) {
        const QString selected = cursor.selectedText();
        const bool hasSeparator = selected.contains(QLatin1Char('^')) || selected.contains(QLatin1Char('/'));
        WrapSelection(QStringLiteral("\\S"), hasSeparator ? QStringLiteral(";") : QStringLiteral("^;"));
    } else {
        InsertAtCursor(QStringLiteral("\\S2^3;"));
    }
}

void MTextEditorDialog::ClearSelectionFormat()
{
    QString text = editor_->toPlainText();
    text.replace(QRegularExpression(QStringLiteral("\\\\[LlOoKk]")), QString());
    text.replace(QRegularExpression(QStringLiteral("\\\\[WHPFQAf][^;]*;")), QString());
    editor_->setPlainText(text);
}

void MTextEditorDialog::ApplyCase(bool upper, bool lower, bool title)
{
    QTextCursor cursor = editor_->textCursor();
    if (!cursor.hasSelection()) return;
    QString selected = cursor.selectedText();
    if (upper) selected = selected.toUpper();
    else if (lower) selected = selected.toLower();
    else if (title) selected = selected.toLower(), selected[0] = selected[0].toUpper();
    cursor.insertText(selected);
}

void MTextEditorDialog::SetOptions(const MTextEditorOptions& options)
{
    editor_->setPlainText(QString::fromUtf8(PlainTextFromMText(options.content).c_str()));
    heightSpin_->setValue(options.height);
    rotationSpin_->setValue(options.rotationDegrees);
    widthSpin_->setValue(options.referenceWidth);
    lineSpacingSpin_->setValue(options.lineSpacingFactor);
    attachmentCombo_->setCurrentIndex(AttachmentIndex(options.attachment));
    paragraphAlignCombo_->setCurrentIndex(ParaIndex(options.paragraphAlign));
    backgroundMaskCheck_->setChecked(options.backgroundMask);
    style_ = options.style;
    colorButton_->setText(ColorButtonText(style_));
    const int idx = styleCombo_->findText(QString::fromUtf8(options.textStyle.c_str()));
    if (idx >= 0) styleCombo_->setCurrentIndex(idx);
}

MTextEditorOptions MTextEditorDialog::Options() const
{
    MTextEditorOptions options;
    QString content = editor_->toPlainText();
    content.replace(QStringLiteral("\n"), QStringLiteral("\\P"));
    options.content = content.toUtf8().constData();
    options.height = heightSpin_->value();
    options.rotationDegrees = rotationSpin_->value();
    options.referenceWidth = widthSpin_->value();
    options.lineSpacingFactor = lineSpacingSpin_->value();
    options.attachment = AttachmentFromIndex(attachmentCombo_->currentIndex());
    options.paragraphAlign = ParaFromIndex(paragraphAlignCombo_->currentIndex());
    options.backgroundMask = backgroundMaskCheck_->isChecked();
    options.textStyle = styleCombo_->currentText().toUtf8().constData();
    options.style = style_;
    options.style.textStyle = options.textStyle;
    return options;
}

void MTextEditorDialog::keyPressEvent(QKeyEvent* event)
{
    if (event->matches(QKeySequence::Cancel)) {
        reject();
        return;
    }
    if ((event->modifiers() & Qt::ControlModifier) && event->key() == Qt::Key_Return) {
        accept();
        return;
    }
    QDialog::keyPressEvent(event);
}

bool MTextEditorDialog::eventFilter(QObject* watched, QEvent* event)
{
    Q_UNUSED(watched);
    if (event->type() == QEvent::KeyPress) {
        auto* keyEvent = static_cast<QKeyEvent*>(event);
        if ((keyEvent->modifiers() & Qt::ControlModifier) && keyEvent->key() == Qt::Key_Return) {
            accept();
            return true;
        }
    }
    return QDialog::eventFilter(watched, event);
}

} // namespace cad
