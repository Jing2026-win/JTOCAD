#pragma once

#include "DataModel/CadDocument.h"
#include <QWidget>
#include <functional>

namespace cad {

class LayerControlBar;

class LayerRibbonWidget : public QWidget {
public:
    explicit LayerRibbonWidget(CadDocument& document, QWidget* parent = nullptr);

    void SetStatusCallback(std::function<void(const QString&)> callback);
    void SetOpenLayerManagerCallback(std::function<void()> callback);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

private:
    void NotifyStatus(const QString& text) const;
    void IsolateSelectedLayers();
    void RestoreIsolation();
    void TurnOffSelectedLayers();
    void FreezeSelectedLayers();
    void LockSelectedLayers();
    void SetCurrentFromSelection();
    void TurnOnAllLayers();
    void ThawAllLayers();
    void UnlockAllLayers();
    void MatchSelectedLayers();

    CadDocument& document_;
    LayerControlBar* controlBar_ = nullptr;
    std::function<void(const QString&)> statusCallback_;
    std::function<void()> openLayerManagerCallback_;
};

} // namespace cad
