#pragma once

#include <cstdint>

namespace cad {

using EntityId = std::uint64_t;

enum class EntityType {
    Point,
    Line,
    Ray,
    XLine,
    Rectangle,
    Polygon,
    Circle,
    Arc,
    Ellipse,
    Parabola,
    Hyperbola,
    Spline,
    Polyline,
    Region,
    Text,
    MText,
    Dimension,
    BlockReference,
    Group
};

enum class LayerState {
    Visible,
    Hidden,
    Locked
};

enum class ToolMode {
    Select,
    DrawPoint,
    DrawLine,
    DrawRay,
    DrawXLine,
    DrawRectangle,
    DrawPolygon,
    DrawCircle,
    DrawArc,
    DrawEllipse,
    DrawParabola,
    DrawHyperbola,
    DrawSpline,
    DrawPolyline,
    DrawText,
    DrawMText,
    DrawDimLinear,
    DrawDimAligned,
    DrawDimRadius,
    DrawDimJoggedRadius,
    DrawDimDiameter,
    DrawDimAngular,
    DrawDimOrdinate,
    DrawSolidFill,
    DrawHatchFill,
    Move,
    Mirror,
    Rotate,
    Scale,
    Trim,
    Extend,
    Break,
    Offset,
    ArrayRectangular,
    ArrayPath,
    ArrayPolar,
    Fillet,
    Chamfer,
    BooleanUnion,
    BooleanSubtract,
    Hatch,
    InsertBlock
};

enum class DrawMethod {
    PointSingle,

    LineTwoPoints,
    LinePointLengthAngle,

    RayStartDirection,
    RayTwoPoints,

    XLineTwoPoints,
    XLinePointAngle,

    RectTwoCorners,
    RectCenterCorner,

    PolygonCenterRadius,
    PolygonInscribed,
    PolygonCircumscribed,
    PolygonEdge,

    CircleCenterRadius,
    CircleCenterDiameter,
    CircleTwoPoints,
    CircleThreePoints,
    CircleTanTanRadius,
    CircleTanTanTan,

    ArcThreePoints,
    ArcCenterStartEnd,
    ArcCenterStartAngle,
    ArcCenterStartChord,
    ArcStartCenterEnd,
    ArcStartCenterAngle,
    ArcStartCenterChord,
    ArcStartEndAngle,
    ArcStartEndRadius,
    ArcStartEndDirection,
    ArcContinue,

    EllipseCenterAxis,
    EllipseAxisEndpoints,

    ParabolaVertexFocus,
    ParabolaThreePoints,

    HyperbolaCenterVertexMinor,

    SplineControlPoints,
    SplineFitPoints,

    PolylineContinuous,

    TextInsertionPoint,
    MTextCornerWidth,
    MTextFixedWidth,

    FillFromSelection,
};

enum class FillMode {
    None,
    Solid,
    Hatch
};

} // namespace cad
