#pragma once

#include <QString>
#include <QVector>
#include <QWidget>
#include <functional>

class QFrame;
class QLabel;
class QLineEdit;
class QToolButton;
class QVBoxLayout;

namespace cad {

struct DynCommandOption {
    QString keyword;
    QString label;
    bool enabled = true;
};

// AutoCAD grip hover menu: appears when the cursor rests on a selected spline grip.
class GripHoverMenuWidget : public QWidget {
public:
    explicit GripHoverMenuWidget(QWidget* parent = nullptr);

    void SetItems(const QVector<DynCommandOption>& items);
    void MoveNearCursor(const QPoint& viewPos);
    bool ContainsGlobalPos(const QPoint& globalPos) const;

    std::function<void(const QString& keyword)> onOptionChosen;
    std::function<void()> onEntered;
    std::function<void()> onLeft;

protected:
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    QVBoxLayout* layout_ = nullptr;
};

// AutoCAD DYN: prompt + down-arrow option list + keyword input next to the cursor.
class DynamicOptionInputWidget : public QWidget {
public:
    explicit DynamicOptionInputWidget(QWidget* parent = nullptr);

    void SetPrompt(const QString& prompt);
    void SetOptions(const QVector<DynCommandOption>& options);
    void SetCycleList(const QString& title, const QString& hint, const QVector<QString>& items, int currentIndex);
    void ClearCycleList();
    bool IsCycleList() const { return cycleList_; }
    void MoveToViewPosition(const QPoint& viewPos);
    void BeginTyping(const QString& firstChar);
    void FocusKeywordEdit();
    void BeginValueEdit(const QString& current);
    void ClearInput();
    void CancelKeywordEdit();
    void HideKeywordEdit();
    bool HasInput() const;
    bool IsKeywordEditing() const { return keywordEditing_; }
    QString InputText() const;
    void ShowOptionMenu();
    void ConfirmInput();
    bool IsMenuOpen() const { return menuOpen_; }
    bool ContainsGlobalPos(const QPoint& globalPos) const;

    std::function<void(const QString& keyword)> onOptionChosen;
    std::function<void()> onKeywordEditCancelled;
    std::function<void()> onEscapeRequested;
    std::function<void(const QString& message)> onInvalidKeyword;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    QString MatchKeyword(const QString& typed) const;
    void ApplyKeyword(const QString& keyword);
    void SetKeywordEditVisible(bool visible);
    void SyncInputWidth();

    QFrame* promptBar_ = nullptr;
    QLabel* promptLabel_ = nullptr;
    QToolButton* arrowButton_ = nullptr;
    QLineEdit* edit_ = nullptr;
    QVector<DynCommandOption> options_;
    bool menuOpen_ = false;
    bool keywordEditing_ = false;
    bool cycleList_ = false;
};

} // namespace cad
