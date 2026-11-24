#pragma once

#include <QWidget>
#include <functional>

class QToolButton;

namespace cad {

class BlockRibbonWidget : public QWidget {
public:
    explicit BlockRibbonWidget(QWidget* parent = nullptr);

    void SetInsertCallback(std::function<void()> callback);
    void SetDetectCallback(std::function<void()> callback);
    void SetCreateCallback(std::function<void()> callback);
    void SetEditCallback(std::function<void()> callback);
    void SetEditAttributesCallback(std::function<void()> callback);
    void SetExplodeCallback(std::function<void()> callback);
    void SetPurgeCallback(std::function<void()> callback);
    void SetWblockCallback(std::function<void()> callback);
    void SetDemoDynamicBlockCallback(std::function<void()> callback);
    void SetRefEditCallback(std::function<void()> callback);
    void SetDesignCenterCallback(std::function<void()> callback);
    void SetBlocksPaletteCallback(std::function<void()> callback);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

private:
    QToolButton* insertButton_ = nullptr;
    QToolButton* detectButton_ = nullptr;
    QToolButton* createButton_ = nullptr;
    QToolButton* editButton_ = nullptr;
    QToolButton* attrButton_ = nullptr;

    std::function<void()> insertCallback_;
    std::function<void()> detectCallback_;
    std::function<void()> createCallback_;
    std::function<void()> editCallback_;
    std::function<void()> editAttributesCallback_;
    std::function<void()> explodeCallback_;
    std::function<void()> purgeCallback_;
    std::function<void()> wblockCallback_;
    std::function<void()> demoDynamicBlockCallback_;
    std::function<void()> refEditCallback_;
    std::function<void()> designCenterCallback_;
    std::function<void()> blocksPaletteCallback_;
};

} // namespace cad
