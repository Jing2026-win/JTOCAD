#pragma once

#include <QColor>

namespace cad {

// AutoCAD factory defaults: PICKBOX=3, APERTURE=10. CURSORSIZE 默认 30
// （有限十字线，兼顾可见性与 AutoCAD 风格；滚轮缩放后由 overlay 强制重绘）。
struct CursorSettings {
    int cursorType = 0;
    int crosshairSizePercent = 30;
    int pickboxPixels = 3;
    int aperturePixels = 10;
    int snapMarkerSizePixels = 10;
    bool showAutoSnapAperture = true;
    QColor crosshairColor{255, 255, 255};
};

enum class CadCursorDisplay {
    Hidden,
    SystemOnly,
    CrosshairOnly,
    CrosshairWithPickbox,
    CrosshairWithAperture,
    PickboxOnly,
};

} // namespace cad