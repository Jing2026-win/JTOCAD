#pragma once

#include <cstdint>
#include <QString>

namespace cad {

// AutoCAD ACI (1-255) color index helpers.
std::uint32_t AcadColorIndexToRgb(int index);
int RgbToAcadColorIndex(std::uint32_t rgb);
QString AcadColorLabel(int index, std::uint32_t rgb);

} // namespace cad
