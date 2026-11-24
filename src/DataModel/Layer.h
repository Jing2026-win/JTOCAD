#pragma once

#include <cstdint>
#include <string>

namespace cad {

class Layer {
public:
    explicit Layer(std::string name = "0", std::uint32_t color = 0xFFFFFF, std::string lineTypeName = "Continuous");

    const std::string& Name() const { return name_; }
    std::uint32_t Color() const { return color_; }
    bool IsOn() const { return on_; }
    bool IsFrozen() const { return frozen_; }
    bool IsLocked() const { return locked_; }
    bool IsPlottable() const { return plottable_; }
    bool IsVisible() const { return on_ && !frozen_; }
    const std::string& LineTypeName() const { return lineTypeName_; }
    double LineWeightMm() const { return lineWeightMm_; }
    int Transparency() const { return transparency_; }
    const std::string& PlotStyleName() const { return plotStyleName_; }
    bool NewViewportFrozen() const { return newViewportFrozen_; }
    bool ViewportFrozen() const { return viewportFrozen_; }
    const std::string& Description() const { return description_; }
    bool IsSystemLayer() const { return name_ == "0" || name_ == "Defpoints"; }

    void SetName(std::string name) { name_ = std::move(name); }
    void SetColor(std::uint32_t color) { color_ = color; }
    void SetOn(bool on) { on_ = on; }
    void SetVisible(bool visible) { on_ = visible; }
    void SetFrozen(bool frozen) { frozen_ = frozen; }
    void SetLocked(bool locked) { locked_ = locked; }
    void SetPlottable(bool plottable) { plottable_ = plottable; }
    void SetLineTypeName(std::string lineTypeName) { lineTypeName_ = std::move(lineTypeName); }
    void SetLineWeightMm(double lineWeightMm);
    void SetTransparency(int transparency);
    void SetPlotStyleName(std::string plotStyleName) { plotStyleName_ = std::move(plotStyleName); }
    void SetNewViewportFrozen(bool frozen) { newViewportFrozen_ = frozen; }
    void SetViewportFrozen(bool frozen) { viewportFrozen_ = frozen; }
    void SetDescription(std::string description) { description_ = std::move(description); }

private:
    std::string name_;
    std::uint32_t color_ = 0xFFFFFF;
    std::string lineTypeName_ = "Continuous";
    double lineWeightMm_ = 0.0;
    int transparency_ = 0;
    std::string plotStyleName_ = "Normal";
    std::string description_;
    bool on_ = true;
    bool frozen_ = false;
    bool locked_ = false;
    bool plottable_ = true;
    bool newViewportFrozen_ = false;
    bool viewportFrozen_ = false;
};

} // namespace cad
