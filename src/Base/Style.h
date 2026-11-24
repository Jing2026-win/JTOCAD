#pragma once

#include <cctype>
#include <cstdint>
#include <string>
#include <vector>

namespace cad {

enum class LinePattern {
    Continuous,
    Dashed,
    Center,
    Hidden
};

// Entity property inheritance (AutoCAD ByLayer / ByBlock / override).
enum class PropertySource {
    ByLayer = 0,
    ByBlock = 1,
    Override = 2
};

struct EntityStyle {
    PropertySource colorSource = PropertySource::ByLayer;
    std::uint8_t red = 255;
    std::uint8_t green = 255;
    std::uint8_t blue = 255;

    PropertySource lineTypeSource = PropertySource::ByLayer;
    std::string lineTypeName = "Continuous";
    LinePattern linePattern = LinePattern::Continuous;

    PropertySource lineWeightSource = PropertySource::ByLayer;
    double lineWidth = 0.0; // mm when Override
    double lineTypeScale = 1.0; // CELTSCALE / object linetype scale

    std::string textStyle = "Standard";
};

inline LinePattern LinePatternFromLineTypeName(const std::string& name)
{
    std::string upper;
    upper.reserve(name.size());
    for (char ch : name) {
        upper.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(ch))));
    }
    if (upper == "DASHED" || upper == "DASH" || upper == "DASHDOT") {
        return LinePattern::Dashed;
    }
    if (upper == "CENTER" || upper == "CENTRE") {
        return LinePattern::Center;
    }
    if (upper == "HIDDEN") {
        return LinePattern::Hidden;
    }
    return LinePattern::Continuous;
}

inline std::string LineTypeNameFromPattern(LinePattern pattern)
{
    switch (pattern) {
    case LinePattern::Dashed:
        return "DASHED";
    case LinePattern::Center:
        return "CENTER";
    case LinePattern::Hidden:
        return "HIDDEN";
    case LinePattern::Continuous:
    default:
        return "Continuous";
    }
}

struct PageLayout {
    std::string name = "Model";
    double widthMm = 297.0;
    double heightMm = 210.0;
    double marginMm = 10.0;
    double scale = 1.0;
    bool landscape = true;
};

struct LineTypeDefinition {
    std::string name = "Continuous";
    std::string description = "Solid line";
    std::vector<double> pattern;
    double isoPenWidthMm = 1.0;
};

struct TextStyleDefinition {
    std::string name = "Standard";
    std::string fontFamily = "Arial";
    double fixedHeight = 0.0;
};

struct DimensionStyle {
    std::string name = "Standard";
    double textHeight = 12.0;
    double arrowSize = 4.0;
    double extensionOffset = 2.0;
    double dimensionLineGap = 6.0;
    bool showRadiusCenterLeader = true;
    double radiusExtensionGap = 2.0;
    double radiusJogAngleDegrees = 45.0;
    double radiusJogHeightFactor = 1.5;
};

} // namespace cad
