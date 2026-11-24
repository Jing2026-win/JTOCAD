#pragma once

#include "Base/TextTypes.h"
#include "Base/Style.h"
#include "DataModel/CadDocument.h"
#include <QComboBox>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QTextEdit>
#include <QToolBar>
#include <string>

class QCheckBox;
class QPushButton;
class QWidget;

namespace cad {

struct SingleLineTextOptions {
    std::string text;
    double height = 12.0;
    double rotationDegrees = 0.0;
    double widthFactor = 1.0;
    double obliqueDegrees = 0.0;
    TextHorizontalAlign horizontalAlign = TextHorizontalAlign::Left;
    TextVerticalAlign verticalAlign = TextVerticalAlign::Baseline;
    std::string textStyle = "Standard";
    EntityStyle style;
};

struct MTextEditorOptions {
    std::string content;
    double height = 12.0;
    double rotationDegrees = 0.0;
    double referenceWidth = 0.0;
    double lineSpacingFactor = 1.0;
    MTextAttachment attachment = MTextAttachment::TopLeft;
    MTextParagraphAlign paragraphAlign = MTextParagraphAlign::Left;
    bool backgroundMask = false;
    std::string textStyle = "Standard";
    EntityStyle style;
};

class SingleLineTextEditorDialog : public QDialog {
public:
    explicit SingleLineTextEditorDialog(const CadDocument& document, QWidget* parent = nullptr);
    void SetOptions(const SingleLineTextOptions& options);
    SingleLineTextOptions Options() const;

protected:
    void keyPressEvent(QKeyEvent* event) override;

private:
    void BuildUi();
    void ChooseColor();
    void InsertSymbol(const QString& symbol);

    const CadDocument& document_;
    QLineEdit* textEdit_ = nullptr;
    QComboBox* styleCombo_ = nullptr;
    QDoubleSpinBox* heightSpin_ = nullptr;
    QDoubleSpinBox* rotationSpin_ = nullptr;
    QDoubleSpinBox* widthFactorSpin_ = nullptr;
    QDoubleSpinBox* obliqueSpin_ = nullptr;
    QComboBox* hAlignCombo_ = nullptr;
    QComboBox* vAlignCombo_ = nullptr;
    QPushButton* colorButton_ = nullptr;
    EntityStyle style_;
};

class MTextEditorDialog : public QDialog {
public:
    explicit MTextEditorDialog(const CadDocument& document, QWidget* parent = nullptr);
    void SetOptions(const MTextEditorOptions& options);
    MTextEditorOptions Options() const;

protected:
    void keyPressEvent(QKeyEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void BuildUi();
    void BuildClipboardToolbar(QToolBar* bar);
    void BuildFormatToolbar(QToolBar* bar);
    void BuildParagraphToolbar(QToolBar* bar);
    void BuildInsertToolbar(QToolBar* bar);
    void ToggleFindReplace();
    void FindNext();
    void ReplaceOne();
    void ReplaceAll();
    void PastePlain();
    void ImportTextFile();
    void ChooseColor();
    void InsertAtCursor(const QString& text);
    void WrapSelection(const QString& prefix, const QString& suffix);
    void InsertStacked();
    void ClearSelectionFormat();
    void ApplyCase(bool upper, bool lower, bool title);

    const CadDocument& document_;
    QTextEdit* editor_ = nullptr;
    QWidget* findPanel_ = nullptr;
    QLineEdit* findEdit_ = nullptr;
    QLineEdit* replaceEdit_ = nullptr;
    QComboBox* styleCombo_ = nullptr;
    QDoubleSpinBox* heightSpin_ = nullptr;
    QDoubleSpinBox* rotationSpin_ = nullptr;
    QDoubleSpinBox* widthSpin_ = nullptr;
    QDoubleSpinBox* lineSpacingSpin_ = nullptr;
    QComboBox* attachmentCombo_ = nullptr;
    QComboBox* paragraphAlignCombo_ = nullptr;
    QCheckBox* backgroundMaskCheck_ = nullptr;
    QPushButton* colorButton_ = nullptr;
    EntityStyle style_;
};

} // namespace cad
