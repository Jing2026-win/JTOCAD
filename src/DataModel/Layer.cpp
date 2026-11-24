#include "DataModel/Layer.h"

#include <algorithm>
#include <utility>

namespace cad {

Layer::Layer(std::string name, std::uint32_t color, std::string lineTypeName)
    : name_(std::move(name))
    , color_(color)
    , lineTypeName_(std::move(lineTypeName))
{
}

void Layer::SetLineWeightMm(const double lineWeightMm)
{
    if (lineWeightMm < 0.0) {
        lineWeightMm_ = -1.0;
    } else {
        lineWeightMm_ = lineWeightMm;
    }
}

void Layer::SetTransparency(const int transparency)
{
    transparency_ = std::clamp(transparency, 0, 90);
}

} // namespace cad
