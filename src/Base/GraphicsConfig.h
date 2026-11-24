#pragma once

#include <algorithm>

namespace cad {

// AutoCAD-like GRAPHICSCONFIG / hardware acceleration settings.
// Controls GPU viewport (OpenGL + MSAA) and display-quality flags.
struct GraphicsConfig {
    // Master switch: QOpenGLWidget viewport + MSAA framebuffer.
    bool hardwareAcceleration = true;

    // MSAA sample count (0/2/4/8). 0 = no MSAA (still uses GL if accel on).
    int msaaSamples = 8;

    // LINESMOOTHING: QPainter antialiasing for strokes.
    bool lineSmoothing = true;

    // HQGEOM: screen display uses true arcs/circles (math-smooth) instead of
    // VIEWRES chord tessellation. Print always uses true curves.
    bool highQualityGeometry = true;

    static int ClampMsaaSamples(int samples)
    {
        if (samples <= 0) {
            return 0;
        }
        if (samples <= 2) {
            return 2;
        }
        if (samples <= 4) {
            return 4;
        }
        return 8;
    }

    void Normalize()
    {
        msaaSamples = ClampMsaaSamples(msaaSamples);
    }
};

} // namespace cad
