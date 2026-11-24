#include "Ui/MainWindow.h"
#include "DataModel/DynamicBlockEvaluator.h"
#include <QSurfaceFormat>
#include <QtWidgets/QApplication>
#include <iostream>
#include <string>

int main(int argc, char* argv[])
{
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i] ? argv[i] : "";
        if (arg == "--test-dynblock") {
            std::string report;
            const bool ok = cad::DynamicBlockEvaluator::RunSelfTest(report);
            std::cout << report;
            return ok ? 0 : 1;
        }
    }

    // Must be set before QApplication: enables MSAA for QOpenGLWidget viewports
    // used by GRAPHICSCONFIG hardware acceleration.
    QSurfaceFormat format;
    format.setRenderableType(QSurfaceFormat::OpenGL);
    format.setSwapBehavior(QSurfaceFormat::DoubleBuffer);
    format.setDepthBufferSize(24);
    format.setStencilBufferSize(8);
    format.setSamples(8);
    QSurfaceFormat::setDefaultFormat(format);

    // High-DPI fractional scaling keeps cosmetic pens on sub-pixel positions,
    // which is required for smooth anti-aliased CAD strokes.
    QApplication::setHighDpiScaleFactorRoundingPolicy(
        Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);

    QApplication app(argc, argv);
    cad::MainWindow window;
    window.show();
    return app.exec();
}
