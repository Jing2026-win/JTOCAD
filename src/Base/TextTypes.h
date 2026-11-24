#pragma once

namespace cad {

enum class TextHorizontalAlign {
    Left = 0,
    Center = 1,
    Right = 2,
    Aligned = 3,
    Middle = 4,
    Fit = 5
};

enum class TextVerticalAlign {
    Baseline = 0,
    Bottom = 1,
    Middle = 2,
    Top = 3
};

enum class MTextAttachment {
    TopLeft = 1,
    TopCenter = 2,
    TopRight = 3,
    MiddleLeft = 4,
    MiddleCenter = 5,
    MiddleRight = 6,
    BottomLeft = 7,
    BottomCenter = 8,
    BottomRight = 9
};

enum class MTextParagraphAlign {
    Left = 0,
    Center = 1,
    Right = 2,
    Justify = 3
};

} // namespace cad
