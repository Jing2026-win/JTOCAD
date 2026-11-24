#pragma once

#include "Base/CadTypes.h"
#include "DataModel/Entity.h"
#include "Geometry/DrawAlgorithms.h"
#include <memory>
#include <string>
#include <vector>

namespace cad {

class CadDocument;
class Entity;

struct DrawContext {
    const std::vector<const Entity*>* entities = nullptr;
    double pickTolerance = 1.0;
};

struct ArcContinueInfo {
    bool valid = false;
    GeoPoint start;
    GeoPoint tangentDirection;
};

struct DrawToolOptions {
    DrawMethod method = DrawMethod::LineTwoPoints;
    int polygonSides = 6;
    double numericValue = 90.0;
    bool largeArc = false;
    bool clockwise = false;
    // SEA：DYN 锁定/键入的包含角（度）。有值时优先于光标 atan2，以支持 (0,360°]。
    bool hasExplicitIncludedAngle = false;
    double explicitIncludedAngleDegrees = 0.0;
    // SCL / CSL：DYN 锁定/键入的弦长（可负，负值=优弧）。有值时优先于光标距离。
    bool hasExplicitChordLength = false;
    double explicitChordLength = 0.0;
    ArcContinueInfo arcContinue;
    double pickTolerance = 20.0;
    // AutoCAD PLINE: arc-segment mode, close flag, Length option, per-vertex bulges.
    bool polylineArcMode = false;
    bool polylineClosed = false;
    bool polylineLengthMode = false;
    std::vector<double> polylineBulges;
    FillMode fillMode = FillMode::Solid;
    double hatchSpacing = 10.0;
    double hatchAngleRadians = 0.0;
};

struct DrawToolResult {
    bool needsMorePoints = true;
    int requiredPoints = 0;
    std::unique_ptr<Entity> entity;
    GeoShape preview;
    std::string prompt;
    // Ray: keep the first input point and continue prompting.
    bool keepBasePoint = false;
    // LINE (AutoCAD): keep the last endpoint and continue prompting for the next segment.
    bool continueFromLastPoint = false;
    std::string nextPrompt;
};

class DrawToolEngine {
public:
    static DrawToolResult ProcessInput(
        ToolMode tool,
        const DrawToolOptions& options,
        const std::vector<GeoPoint>& inputPoints,
        const GeoPoint& cursorPoint,
        const CadDocument* document);

    static DrawMethod DefaultMethod(ToolMode tool);
    static std::vector<DrawMethod> AvailableMethods(ToolMode tool);
    static std::string MethodLabel(DrawMethod method);

    static bool IsContinuousDrawTool(ToolMode tool);
    static bool IsRepeatingBasePointTool(ToolMode tool);
    static bool ArcMethodUsesNumericParameter(DrawMethod method);
    static void UpdateArcContinueInfo(const Entity& entity, ArcContinueInfo& info);
    static std::unique_ptr<Entity> FinalizeContinuousDraw(
        ToolMode tool,
        const DrawToolOptions& options,
        const std::vector<GeoPoint>& inputPoints);
};

} // namespace cad
