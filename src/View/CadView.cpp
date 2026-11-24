#include "View/CadView.h"

#include "Base/IdGenerator.h"
#include "Base/Tolerance.h"
#include "Command/CommandManager.h"
#include "Command/DrawToolEngine.h"
#include "Command/EditKernel.h"
#include "DataModel/ArcEntity.h"
#include "DataModel/CadDocument.h"
#include "DataModel/CircleEntity.h"
#include "DataModel/ConicEntity.h"
#include "DataModel/ArcEntity.h"
#include "DataModel/CircleEntity.h"
#include "DataModel/DimensionEntity.h"
#include "Geometry/GeoArc.h"
#include "Geometry/DimensionGeometry.h"
#include "DataModel/Entity.h"
#include "DataModel/LineEntity.h"
#include "DataModel/MTextEntity.h"
#include "DataModel/PolylineEntity.h"
#include "DataModel/RegionEntity.h"
#include "DataModel/TextEntity.h"
#include "View/CadScene.h"
#include "View/DimensionRenderUtil.h"
#include "View/DynamicOptionInputWidget.h"
#include "View/DynamicPointInputWidget.h"
#include "View/DynamicPolarInputWidget.h"
#include "View/GeoShapePainter.h"
#include "View/GripItem.h"
#include "View/ObjectSnapUtil.h"
#include "View/TextEditorDialog.h"
#include "View/TextRenderUtil.h"
#include "View/BlockAttributeEditDialog.h"
#include "DataModel/BlockReferenceEntity.h"
#include "IO/LineTypeLibrary.h"
#include "Geometry/GeoCircle.h"
#include "Geometry/GeoAlgorithms.h"
#include "Geometry/DrawAlgorithms.h"
#include "Geometry/GeoLine.h"
#include "Geometry/GeoShape.h"
#include <QDialog>
#include <QGraphicsEllipseItem>
#include <QGraphicsSimpleTextItem>
#include <QInputDialog>
#include <QEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QHoverEvent>
#include <QMouseEvent>
#include "View/BlockEditorDialog.h"
#include <QPainter>
#include <QOpenGLWidget>
#include <QSurfaceFormat>
#include <QPainterPath>
#include <QResizeEvent>
#include <QScrollBar>
#include <QShowEvent>
#include <QTimer>
#include <QWheelEvent>
#include <QCursor>
#include <QGuiApplication>
#include <QtMath>
#include <functional>
#include <algorithm>
#include <cmath>
#include <limits>

namespace cad {

namespace {

class AntialiasedPathItem : public QGraphicsPathItem {
public:
    using QGraphicsPathItem::QGraphicsPathItem;

    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override
    {
        if (painter) {
            painter->setRenderHint(QPainter::Antialiasing, true);
        }
        QGraphicsPathItem::paint(painter, option, widget);
    }
};

double DistancePointToSegment(const GeoPoint& point, const GeoPoint& start, const GeoPoint& end)
{
    const double vx = end.X() - start.X();
    const double vy = end.Y() - start.Y();
    const double wx = point.X() - start.X();
    const double wy = point.Y() - start.Y();
    const double len2 = vx * vx + vy * vy;
    if (len2 <= Tolerance::Model * Tolerance::Model) {
        return Distance2D(point, start);
    }
    const double t = std::clamp((wx * vx + wy * vy) / len2, 0.0, 1.0);
    return Distance2D(point, GeoPoint(start.X() + t * vx, start.Y() + t * vy));
}

double NormalizeAnglePositive(double angle)
{
    constexpr double kTwoPi = 6.28318530717958647692;
    angle = std::fmod(angle, kTwoPi);
    if (angle < 0.0) {
        angle += kTwoPi;
    }
    return angle;
}

// Map angle to (-pi, pi] for AutoCAD-like dimension arcs (shortest sweep).
double NormalizeAngleSigned(double angle)
{
    constexpr double kPi = 3.14159265358979323846;
    constexpr double kTwoPi = 6.28318530717958647692;
    angle = std::fmod(angle, kTwoPi);
    if (angle > kPi) {
        angle -= kTwoPi;
    } else if (angle <= -kPi) {
        angle += kTwoPi;
    }
    return angle;
}

// CCW included angle (0, 360°] from start radial to pick radial; 0 when ≈360°.
double IncludedAngleSweepRadians(double startAng, double pickAng)
{
    constexpr double kTwoPi = 6.28318530717958647692;
    double included = pickAng - startAng;
    while (included <= 0.0) {
        included += kTwoPi;
    }
    while (included > kTwoPi) {
        included -= kTwoPi;
    }
    if (included >= kTwoPi - 1.0e-9) {
        return 0.0;
    }
    return included;
}

constexpr double kPi = 3.14159265358979323846;
constexpr double kDegToRad = kPi / 180.0;
constexpr double kRadToDeg = 180.0 / kPi;

// AutoCAD Dynamic Input (LINE): angle tooltip is the sweep from +X toward the
// cursor side, range [0, 180]. Above start → CCW; below start → CW.
// Absolute CCW 330° (down-right) therefore displays as 30°, not 330°.
double DynInputDisplayAngleDegrees(double angleRadians)
{
    return std::abs(NormalizeAngleSigned(angleRadians) * kRadToDeg);
}

// Convert a DYN-locked angle back to atan2 radians using the cursor side.
// Values > 180° are treated as absolute CCW (0..360], matching typed `<330`.
double DynInputLockedAngleRadians(double lockedDegrees, double freeAngleRadians)
{
    const double magnitude = std::abs(lockedDegrees);
    if (magnitude > 180.0) {
        return DrawAlgorithms::ToPositiveAngleDegrees(lockedDegrees) * kDegToRad;
    }
    const double sign = (NormalizeAngleSigned(freeAngleRadians) < 0.0) ? -1.0 : 1.0;
    return sign * magnitude * kDegToRad;
}

GeoPoint PointOnArc(const GeoArc& arc, double angle)
{
    return GeoPoint(
        arc.Center().X() + std::cos(angle) * arc.Radius(),
        arc.Center().Y() + std::sin(angle) * arc.Radius());
}

bool IsAngleOnArcSpan(const GeoArc& arc, double angle)
{
    constexpr double kTwoPi = 6.28318530717958647692;
    const double span = arc.EndAngleRadians() - arc.StartAngleRadians();
    if (std::abs(span) >= kTwoPi - Tolerance::Angular) {
        return true;
    }
    if (std::abs(span) <= Tolerance::Angular) {
        return false;
    }

    if (span > 0.0) {
        const double relative = NormalizeAnglePositive(angle - arc.StartAngleRadians());
        return relative <= span + Tolerance::Angular;
    }

    const double relative = NormalizeAnglePositive(arc.StartAngleRadians() - angle);
    return relative <= -span + Tolerance::Angular;
}

double DistancePointToCircleCurve(const GeoPoint& point, const GeoCircle& circle)
{
    return std::abs(Distance2D(point, circle.Center()) - circle.Radius());
}

double DistancePointToArcCurve(const GeoPoint& point, const GeoArc& arc)
{
    if (arc.Radius() <= Tolerance::Model) {
        return Distance2D(point, arc.Center());
    }

    const double dx = point.X() - arc.Center().X();
    const double dy = point.Y() - arc.Center().Y();
    const double distToCenter = std::hypot(dx, dy);
    if (distToCenter > Tolerance::Model) {
        const double angle = std::atan2(dy, dx);
        if (IsAngleOnArcSpan(arc, angle)) {
            return std::abs(distToCenter - arc.Radius());
        }
    }

    return std::min(
        Distance2D(point, PointOnArc(arc, arc.StartAngleRadians())),
        Distance2D(point, PointOnArc(arc, arc.EndAngleRadians())));
}

struct DimensionSnapHint {
    GeoPoint snapPoint;
    GeoPoint endpointA;
    GeoPoint endpointB;
    bool snapIsEndpointA = true;
};

GeoPoint NearerEndpoint(const GeoPoint& point, const GeoPoint& a, const GeoPoint& b)
{
    return Distance2D(point, a) <= Distance2D(point, b) ? a : b;
}

DimensionSnapHint MakeEndpointHint(
    const GeoPoint& point,
    const GeoPoint& endpointA,
    const GeoPoint& endpointB)
{
    DimensionSnapHint hint;
    hint.endpointA = endpointA;
    hint.endpointB = endpointB;
    hint.snapPoint = NearerEndpoint(point, endpointA, endpointB);
    hint.snapIsEndpointA = Distance2D(hint.snapPoint, endpointA) <= Distance2D(hint.snapPoint, endpointB);
    return hint;
}

void AppendEndpointSquare(QPainterPath& path, const QPointF& center, const double halfSize)
{
    path.addRect(center.x() - halfSize, center.y() - halfSize, halfSize * 2.0, halfSize * 2.0);
}

void AppendBidirectionalModelLine(
    QPainterPath& path,
    const GeoPoint& anchor,
    const GeoPoint& direction,
    const double extent,
    const std::function<QPointF(const GeoPoint&)>& toScene)
{
    const double length = std::hypot(direction.X(), direction.Y());
    if (length <= Tolerance::Model) {
        return;
    }
    const GeoPoint unit(direction.X() / length, direction.Y() / length);
    const GeoPoint start(
        anchor.X() - unit.X() * extent,
        anchor.Y() - unit.Y() * extent);
    const GeoPoint end(
        anchor.X() + unit.X() * extent,
        anchor.Y() + unit.Y() * extent);
    path.moveTo(toScene(start));
    path.lineTo(toScene(end));
}

GeoBounds VisibleRectToBounds(const QRectF& rect)
{
    const QRectF normalized = rect.normalized();
    return GeoBounds{normalized.left(), normalized.top(), normalized.right(), normalized.bottom(), true};
}

std::optional<DimensionSnapHint> ComputeDimensionSnapHint(
    const CadDocument& document,
    const GeoPoint& point,
    const double pickTolerance,
    const QRectF& visibleModelRect,
    const std::function<bool(const Entity&, const QRectF&, double)>& intersectsVisible)
{
    struct SegmentCandidate {
        GeoPoint endpointA;
        GeoPoint endpointB;
        double distance = std::numeric_limits<double>::max();
    };

    std::optional<SegmentCandidate> best;

    auto considerSegment = [&](const GeoPoint& start, const GeoPoint& end) {
        const double distance = DistancePointToSegment(point, start, end);
        if (distance > pickTolerance) {
            return;
        }
        if (!best || distance < best->distance) {
            best = SegmentCandidate{start, end, distance};
        }
    };

    for (const auto& entity : document.Entities()) {
        if (!intersectsVisible(*entity, visibleModelRect, pickTolerance)) {
            continue;
        }

        switch (entity->Type()) {
        case EntityType::Line: {
            const auto& line = static_cast<const LineEntity&>(*entity);
            considerSegment(line.Line().Start(), line.Line().End());
            break;
        }
        case EntityType::Arc: {
            const auto& arcEntity = static_cast<const ArcEntity&>(*entity);
            const GeoPoint start = arcEntity.StartPoint();
            const GeoPoint end = arcEntity.EndPoint();
            if (Distance2D(point, start) <= pickTolerance) {
                if (!best || Distance2D(point, start) < best->distance) {
                    best = SegmentCandidate{start, end, Distance2D(point, start)};
                }
            } else if (Distance2D(point, end) <= pickTolerance) {
                if (!best || Distance2D(point, end) < best->distance) {
                    best = SegmentCandidate{start, end, Distance2D(point, end)};
                }
            }
            break;
        }
        case EntityType::Polyline: {
            const auto& polyline = static_cast<const PolylineEntity&>(*entity);
            const std::vector<GeoPoint>& vertices = polyline.Polyline().Points();
            if (vertices.size() < 2) {
                break;
            }
            for (std::size_t i = 1; i < vertices.size(); ++i) {
                considerSegment(vertices[i - 1], vertices[i]);
            }
            if (polyline.Polyline().Closed()) {
                considerSegment(vertices.back(), vertices.front());
            }
            break;
        }
        case EntityType::Circle: {
            const auto& circle = static_cast<const CircleEntity&>(*entity);
            const GeoPoint center = circle.Circle().Center();
            const double radius = circle.Circle().Radius();
            const double centerDistance = Distance2D(point, center);
            const double distance = std::abs(centerDistance - radius);
            if (distance > pickTolerance) {
                break;
            }
            const double angle = std::atan2(point.Y() - center.Y(), point.X() - center.X());
            const GeoPoint nearPoint(
                center.X() + std::cos(angle) * radius,
                center.Y() + std::sin(angle) * radius);
            const GeoPoint oppositePoint(
                center.X() - std::cos(angle) * radius,
                center.Y() - std::sin(angle) * radius);
            if (!best || distance < best->distance) {
                best = SegmentCandidate{nearPoint, oppositePoint, distance};
            }
            break;
        }
        default:
            break;
        }
    }

    if (!best) {
        return std::nullopt;
    }

    return MakeEndpointHint(point, best->endpointA, best->endpointB);
}

GeoBounds ModelRectToBounds(const QRectF& rect)
{
    const QRectF normalized = rect.normalized();
    return GeoBounds{normalized.left(), normalized.top(), normalized.right(), normalized.bottom(), true};
}

// AutoCAD-style first-point HUD prompt (absolute X/Y dyninput).
QString DynPointPromptFor(ToolMode mode, DrawMethod method)
{
    switch (mode) {
    case ToolMode::DrawPoint:
        return QStringLiteral("指定点");
    case ToolMode::DrawLine:
        return QStringLiteral("指定第一点");
    case ToolMode::DrawRay:
        return QStringLiteral("指定起点");
    case ToolMode::DrawXLine:
        return method == DrawMethod::XLinePointAngle
            ? QStringLiteral("指定已知点")
            : QStringLiteral("指定通过点");
    case ToolMode::DrawRectangle:
        return method == DrawMethod::RectCenterCorner
            ? QStringLiteral("指定矩形中心")
            : QStringLiteral("指定第一个角点");
    case ToolMode::DrawPolygon:
        return method == DrawMethod::PolygonEdge
            ? QStringLiteral("指定边的第一个端点")
            : QStringLiteral("指定正多边形的中心点或");
    case ToolMode::DrawCircle:
        switch (method) {
        case DrawMethod::CircleTwoPoints:
            return QStringLiteral("指定圆直径的第一个端点");
        case DrawMethod::CircleThreePoints:
            return QStringLiteral("指定圆上的第一个点");
        case DrawMethod::CircleTanTanRadius:
        case DrawMethod::CircleTanTanTan:
            return QStringLiteral("指定圆的第一条切线");
        default:
            return QStringLiteral("指定圆的圆心");
        }
    case ToolMode::DrawArc:
        switch (method) {
        case DrawMethod::ArcCenterStartEnd:
        case DrawMethod::ArcCenterStartAngle:
        case DrawMethod::ArcCenterStartChord:
            return QStringLiteral("指定圆弧的圆心");
        case DrawMethod::ArcContinue:
            return QStringLiteral("指定圆弧的端点");
        default:
            return QStringLiteral("指定圆弧的起点");
        }
    case ToolMode::DrawEllipse:
        return method == DrawMethod::EllipseAxisEndpoints
            ? QStringLiteral("指定轴的端点")
            : QStringLiteral("指定椭圆的中心点");
    case ToolMode::DrawParabola:
        return method == DrawMethod::ParabolaThreePoints
            ? QStringLiteral("指定抛物线上的点")
            : QStringLiteral("指定抛物线顶点");
    case ToolMode::DrawHyperbola:
        return QStringLiteral("指定双曲线中心");
    case ToolMode::DrawSpline:
        return QStringLiteral("指定第一个点或");
    case ToolMode::DrawPolyline:
        return QStringLiteral("指定起点");
    case ToolMode::DrawText:
        return QStringLiteral("指定文字的起点");
    case ToolMode::DrawMText:
        return QStringLiteral("指定第一角点");
    case ToolMode::DrawDimLinear:
    case ToolMode::DrawDimAligned:
        return QStringLiteral("指定第一条尺寸界线原点");
    case ToolMode::DrawDimOrdinate:
        return QStringLiteral("指定特征位置");
    case ToolMode::DrawDimAngular:
        return QStringLiteral("指定角的顶点");
    case ToolMode::InsertBlock:
        return QStringLiteral("指定插入点");
    case ToolMode::Move:
    case ToolMode::Rotate:
    case ToolMode::Scale:
        return QStringLiteral("指定基点");
    case ToolMode::Mirror:
        return QStringLiteral("指定镜像线的第一点");
    case ToolMode::Offset:
        return QStringLiteral("指定通过点");
    case ToolMode::ArrayPolar:
        return QStringLiteral("指定阵列的中心点");
    default:
        return QStringLiteral("指定点");
    }
}

QString ToolStatusPrompt(ToolMode mode)
{
    switch (mode) {
    case ToolMode::Select:
        return QStringLiteral("选择：左键框选；选中 TEXT/MTEXT 后 Ctrl+C 复制、Ctrl+V 粘贴；中/右键平移");
    case ToolMode::Move:
        return QStringLiteral("移动：指定基点 → 指定目标点");
    case ToolMode::Rotate:
        return QStringLiteral("旋转：指定中心点 → 指定角度点");
    case ToolMode::Scale:
        return QStringLiteral("缩放：指定基点 → 参考长度点 → 目标长度点");
    case ToolMode::Trim:
        return QStringLiteral("修剪：点击要修剪的直线，再点击剪切边界");
    case ToolMode::Extend:
        return QStringLiteral("延伸：点击要延伸的直线，再点击延伸边界");
    case ToolMode::Break:
        return QStringLiteral("打断：点击对象指定第一点 → 第二点（删除中间段）；Enter/右键在一点处打断（支持样条曲线）");
    case ToolMode::Fillet:
        return QStringLiteral("圆角：选择第一个对象或 [放弃(U)/多段线(P)/半径(R)/修剪(T)/多个(M)]");
    case ToolMode::Chamfer:
        return QStringLiteral("倒角：选择第一条直线或 [放弃(U)/多段线(P)/距离(D)/角度(A)/修剪(T)/方法(E)/多个(M)]");
    case ToolMode::Offset:
        return QStringLiteral("偏移：选中对象 → 指定距离起点 → 指定距离终点");
    case ToolMode::ArrayRectangular:
        return QStringLiteral("矩形阵列：设置行、列、间距和阵列角度");
    case ToolMode::ArrayPath:
        return QStringLiteral("路径阵列：选择作为阵列路径的曲线");
    case ToolMode::ArrayPolar:
        return QStringLiteral("环形阵列：指定阵列中心点");
    case ToolMode::BooleanUnion:
        return QStringLiteral("并集：选中至少 2 个可闭合对象后点击「并集」");
    case ToolMode::BooleanSubtract:
        return QStringLiteral("差集：先选被减对象，再选减去对象，点击「差集」");
    case ToolMode::Hatch:
        return QStringLiteral("填充线：选中可闭合对象后点击「填充线」");
    case ToolMode::DrawText:
        return QStringLiteral("单行文字：指定插入点 → 在位文字编辑器（Ctrl+Enter 保存，Esc 放弃）");
    case ToolMode::DrawMText:
        return QStringLiteral("多行文字：指定插入点/对角点 → 在位文字编辑器");
    case ToolMode::DrawDimLinear:
        return QStringLiteral("线性标注(DLI)：指定第一条尺寸界线原点 → 第二条原点 → 尺寸线位置；选项 M/T/A/R");
    case ToolMode::DrawDimAligned:
        return QStringLiteral("对齐标注(DAL)：指定第一条尺寸界线原点 → 第二条原点 → 尺寸线位置；选项 M/T/A");
    case ToolMode::DrawDimRadius:
        return QStringLiteral("半径标注(DRA)：选择圆或圆弧 → 指定尺寸线位置；选项 M/T/A");
    case ToolMode::DrawDimJoggedRadius:
        return QStringLiteral("折弯半径(DJO)：选圆/弧 → 圆心覆盖点 → 尺寸线位置 → 折弯点；选项 M/T/A");
    case ToolMode::DrawDimDiameter:
        return QStringLiteral("直径标注(DDI)：选择圆或圆弧 → 指定尺寸线位置；选项 M/T/A");
    case ToolMode::DrawDimAngular:
        return QStringLiteral("角度标注(DAN)：选择圆弧/圆/两直线，或回车后用三点定义角 → 指定标注弧位置；选项 M/T/A/Q");
    case ToolMode::DrawDimOrdinate:
        return QStringLiteral("坐标标注(DOR)：指定特征点 → 拖动引线方向放置文字；垂直拖动标 X，水平拖动标 Y；选项 M/T/A/X/Y");
    case ToolMode::DrawPolygon:
        return QStringLiteral("正多边形：指定中心点或 [边(E)] → 内接于圆(I)/外切于圆(C) → 指定半径");
    case ToolMode::DrawLine:
        return QStringLiteral("直线(L)：指定第一点 → 连续指定下一点；U 放弃 / C 闭合；Enter/右键结束");
    case ToolMode::DrawPolyline:
        return QStringLiteral("多段线(PL)：指定起点 → 下一点；A 圆弧 / L 直线或长度 / U 放弃 / C 闭合；Enter/右键结束");
    case ToolMode::DrawSpline:
        return QStringLiteral("样条曲线(SPL)：默认拟合点；指定第一点 → 下一点；U 放弃 / C 闭合；Enter/右键结束");
    case ToolMode::InsertBlock:
        return QStringLiteral("插入块：在图纸上指定插入点");
    default:
        return QStringLiteral("Tool changed");
    }
}

GeoPoint TextInsertionPoint(const Entity& entity)
{
    if (entity.Type() == EntityType::Text) {
        return static_cast<const TextEntity&>(entity).Position();
    }
    if (entity.Type() == EntityType::MText) {
        return static_cast<const MTextEntity&>(entity).Position();
    }
    return GeoPoint();
}

DimensionStyle ResolveDimensionStyleForEntity(const CadDocument* document, const DimensionEntity& dimension)
{
    if (!document) {
        return DimensionStyle{};
    }
    for (const DimensionStyle& style : document->DimensionStyles()) {
        if (style.name == dimension.DimensionStyleName()) {
            return style;
        }
    }
    return document->DimensionStyles().empty() ? DimensionStyle{} : document->DimensionStyles().front();
}

DimensionStyle ResolveDimensionStyleByName(const CadDocument* document, const std::string& name)
{
    if (!document) {
        return DimensionStyle{};
    }
    for (const DimensionStyle& style : document->DimensionStyles()) {
        if (style.name == name) {
            return style;
        }
    }
    return document->DimensionStyles().empty() ? DimensionStyle{} : document->DimensionStyles().front();
}

GeoPoint CentroidOfTextEntities(const std::vector<std::unique_ptr<Entity>>& entities)
{
    if (entities.empty()) {
        return GeoPoint();
    }
    double sumX = 0.0;
    double sumY = 0.0;
    for (const auto& entity : entities) {
        const GeoPoint point = TextInsertionPoint(*entity);
        sumX += point.X();
        sumY += point.Y();
    }
    const double count = static_cast<double>(entities.size());
    return GeoPoint(sumX / count, sumY / count);
}

double ParameterOnSegment01(const GeoPoint& start, const GeoPoint& end, const GeoPoint& point)
{
    const double vx = end.X() - start.X();
    const double vy = end.Y() - start.Y();
    const double len2 = vx * vx + vy * vy;
    if (len2 <= Tolerance::Model * Tolerance::Model) {
        return 0.0;
    }
    const double wx = point.X() - start.X();
    const double wy = point.Y() - start.Y();
    return std::clamp((wx * vx + wy * vy) / len2, 0.0, 1.0);
}

bool PointInsideModelRect(const GeoPoint& point, const QRectF& rect, double tolerance)
{
    return point.X() >= rect.left() - tolerance
        && point.X() <= rect.right() + tolerance
        && point.Y() >= rect.top() - tolerance
        && point.Y() <= rect.bottom() + tolerance;
}

QRectF NormalizedModelRect(const GeoPoint& a, const GeoPoint& b)
{
    const double left = std::min(a.X(), b.X());
    const double right = std::max(a.X(), b.X());
    const double bottom = std::min(a.Y(), b.Y());
    const double top = std::max(a.Y(), b.Y());
    return QRectF(left, bottom, right - left, top - bottom);
}

struct MTextCornerPlacement {
    GeoPoint insertionPoint;
    double referenceWidth = 0.0;
    MTextAttachment attachment = MTextAttachment::TopLeft;
};

MTextCornerPlacement ResolveMTextCornerPlacement(const GeoPoint& firstCorner, const GeoPoint& secondCorner)
{
    const QRectF bounds = NormalizedModelRect(firstCorner, secondCorner);
    MTextCornerPlacement placement;
    // AutoCAD: two diagonal corners define the text width; insertion is always the top-left corner.
    placement.insertionPoint = GeoPoint(bounds.left(), bounds.bottom());
    placement.referenceWidth = std::max(Tolerance::Model, bounds.width());
    placement.attachment = MTextAttachment::TopLeft;
    return placement;
}

QRectF ModelRectToSceneRect(const QRectF& modelRect)
{
    return QRectF(
        modelRect.left(),
        -modelRect.bottom(),
        modelRect.width(),
        modelRect.height());
}

QPainterPath ModelRectToScenePath(const GeoPoint& a, const GeoPoint& b)
{
    QPainterPath path;
    path.addRect(ModelRectToSceneRect(NormalizedModelRect(a, b)));
    return path;
}

GeoBounds ModelRectToGeoBounds(const QRectF& rect)
{
    return GeoBounds{rect.left(), rect.top(), rect.right(), rect.bottom(), true};
}

std::optional<GeoPoint> SegmentSegmentIntersection(
    const GeoPoint& a1,
    const GeoPoint& a2,
    const GeoPoint& b1,
    const GeoPoint& b2)
{
    const GeoLine lineA(a1, a2);
    const GeoLine lineB(b1, b2);
    const double x1 = lineA.Start().X();
    const double y1 = lineA.Start().Y();
    const double x2 = lineA.End().X();
    const double y2 = lineA.End().Y();
    const double x3 = lineB.Start().X();
    const double y3 = lineB.Start().Y();
    const double x4 = lineB.End().X();
    const double y4 = lineB.End().Y();
    const double denom = (x1 - x2) * (y3 - y4) - (y1 - y2) * (x3 - x4);
    if (std::abs(denom) <= Tolerance::Model) {
        return std::nullopt;
    }
    const double px = ((x1 * y2 - y1 * x2) * (x3 - x4) - (x1 - x2) * (x3 * y4 - y3 * x4)) / denom;
    const double py = ((x1 * y2 - y1 * x2) * (y3 - y4) - (y1 - y2) * (x3 * y4 - y3 * x4)) / denom;
    const GeoPoint intersection(px, py);
    const double ta = ParameterOnSegment01(a1, a2, intersection);
    const double tb = ParameterOnSegment01(b1, b2, intersection);
    if (ta < -Tolerance::Model || ta > 1.0 + Tolerance::Model || tb < -Tolerance::Model || tb > 1.0 + Tolerance::Model) {
        return std::nullopt;
    }
    return intersection;
}

bool SegmentIntersectsModelRect(
    const GeoPoint& start,
    const GeoPoint& end,
    const QRectF& rect,
    double tolerance)
{
    const double xmin = rect.left() - tolerance;
    const double xmax = rect.right() + tolerance;
    const double ymin = rect.top() - tolerance;
    const double ymax = rect.bottom() + tolerance;

    const double x0 = start.X();
    const double y0 = start.Y();
    const double dx = end.X() - x0;
    const double dy = end.Y() - y0;

    double t0 = 0.0;
    double t1 = 1.0;

    auto clip = [&](double p, double q) {
        if (std::abs(p) <= Tolerance::Model) {
            return q >= -Tolerance::Model;
        }
        const double r = q / p;
        if (p < 0.0) {
            if (r > t1) {
                return false;
            }
            if (r > t0) {
                t0 = r;
            }
        } else {
            if (r < t0) {
                return false;
            }
            if (r < t1) {
                t1 = r;
            }
        }
        return true;
    };

    if (!clip(-dx, x0 - xmin)) {
        return false;
    }
    if (!clip(dx, xmax - x0)) {
        return false;
    }
    if (!clip(-dy, y0 - ymin)) {
        return false;
    }
    if (!clip(dy, ymax - y0)) {
        return false;
    }

    return t0 <= t1 + Tolerance::Model;
}

bool SegmentFullyInsideModelRect(
    const GeoPoint& start,
    const GeoPoint& end,
    const QRectF& rect,
    double tolerance)
{
    return PointInsideModelRect(start, rect, tolerance) && PointInsideModelRect(end, rect, tolerance);
}

bool CircleFullyInsideModelRect(const GeoCircle& circle, const QRectF& rect, double tolerance)
{
    if (circle.Radius() <= Tolerance::Model) {
        return PointInsideModelRect(circle.Center(), rect, tolerance);
    }
    return circle.Center().X() - circle.Radius() >= rect.left() - tolerance
        && circle.Center().X() + circle.Radius() <= rect.right() + tolerance
        && circle.Center().Y() - circle.Radius() >= rect.top() - tolerance
        && circle.Center().Y() + circle.Radius() <= rect.bottom() + tolerance;
}

bool CircleCrossesModelRect(const GeoCircle& circle, const QRectF& rect, double tolerance)
{
    const double closestX = std::clamp(circle.Center().X(), rect.left(), rect.right());
    const double closestY = std::clamp(circle.Center().Y(), rect.top(), rect.bottom());
    const double dx = circle.Center().X() - closestX;
    const double dy = circle.Center().Y() - closestY;
    return dx * dx + dy * dy <= (circle.Radius() + tolerance) * (circle.Radius() + tolerance);
}

struct CircleArcPick {
    GeoPoint center;
    double radius = 0.0;
    bool hasArcSpan = false;
    double arcStartRadians = 0.0;
    double arcEndRadians = 0.0;
};

std::optional<CircleArcPick> PickCircleOrArcAt(
    const CadDocument& document,
    const GeoPoint& point,
    double tolerance,
    const QRectF& visibleModelRect)
{
    Q_UNUSED(visibleModelRect);
    double bestDistance = tolerance;
    std::optional<CircleArcPick> bestPick;

    for (const auto& entity : document.Entities()) {
        if (entity->Type() == EntityType::Circle) {
            const auto& circle = static_cast<const CircleEntity&>(*entity);
            const GeoPoint& center = circle.Circle().Center();
            const double radius = circle.Circle().Radius();
            const double distance = std::abs(Distance2D(center, point) - radius);
            if (distance <= bestDistance) {
                bestDistance = distance;
                bestPick = CircleArcPick{center, radius, false, 0.0, 0.0};
            }
            continue;
        }

        if (entity->Type() == EntityType::Arc) {
            const auto& arcEntity = static_cast<const ArcEntity&>(*entity);
            const GeoArc& arc = arcEntity.Arc();
            const double distance = DistancePointToArcCurve(point, arc);
            if (distance <= bestDistance) {
                bestDistance = distance;
                bestPick = CircleArcPick{
                    arc.Center(),
                    arc.Radius(),
                    true,
                    arc.StartAngleRadians(),
                    arc.EndAngleRadians()};
            }
        }
    }

    return bestPick;
}

// When CEN osnap has already pulled the pick to a circle/arc center, curve-distance
// picking fails (distance == radius). Resolve the entity by center instead.
std::optional<CircleArcPick> PickCircleOrArcByCenter(
    const CadDocument& document,
    const GeoPoint& centerPoint,
    double tolerance)
{
    double bestDistance = tolerance;
    std::optional<CircleArcPick> bestPick;

    for (const auto& entity : document.Entities()) {
        if (entity->Type() == EntityType::Circle) {
            const auto& circle = static_cast<const CircleEntity&>(*entity);
            const double distance = Distance2D(circle.Circle().Center(), centerPoint);
            if (distance <= bestDistance) {
                bestDistance = distance;
                bestPick = CircleArcPick{
                    circle.Circle().Center(),
                    circle.Circle().Radius(),
                    false,
                    0.0,
                    0.0};
            }
            continue;
        }

        if (entity->Type() == EntityType::Arc) {
            const auto& arcEntity = static_cast<const ArcEntity&>(*entity);
            const GeoArc& arc = arcEntity.Arc();
            const double distance = Distance2D(arc.Center(), centerPoint);
            if (distance <= bestDistance) {
                bestDistance = distance;
                bestPick = CircleArcPick{
                    arc.Center(),
                    arc.Radius(),
                    true,
                    arc.StartAngleRadians(),
                    arc.EndAngleRadians()};
            }
        }
    }

    return bestPick;
}

std::optional<GeoPoint> InfiniteLineIntersection(const GeoLine& a, const GeoLine& b)
{
    const double x1 = a.Start().X();
    const double y1 = a.Start().Y();
    const double x2 = a.End().X();
    const double y2 = a.End().Y();
    const double x3 = b.Start().X();
    const double y3 = b.Start().Y();
    const double x4 = b.End().X();
    const double y4 = b.End().Y();
    const double denom = (x1 - x2) * (y3 - y4) - (y1 - y2) * (x3 - x4);
    if (std::abs(denom) <= Tolerance::Model) {
        return std::nullopt;
    }
    const double px = ((x1 * y2 - y1 * x2) * (x3 - x4) - (x1 - x2) * (x3 * y4 - y3 * x4)) / denom;
    const double py = ((x1 * y2 - y1 * x2) * (y3 - y4) - (y1 - y2) * (x3 * y4 - y3 * x4)) / denom;
    return GeoPoint(px, py);
}

GeoPoint ArmPointOnLine(const GeoLine& line, const GeoPoint& vertex, const GeoPoint& pickNear)
{
    const double dx = line.End().X() - line.Start().X();
    const double dy = line.End().Y() - line.Start().Y();
    const double len = std::hypot(dx, dy);
    if (len <= Tolerance::Model) {
        return pickNear;
    }
    const double ux = dx / len;
    const double uy = dy / len;
    const double vx = pickNear.X() - vertex.X();
    const double vy = pickNear.Y() - vertex.Y();
    const double sign = (vx * ux + vy * uy) >= 0.0 ? 1.0 : -1.0;
    return GeoPoint(vertex.X() + sign * ux * 100.0, vertex.Y() + sign * uy * 100.0);
}

std::optional<GeoLine> LineSegmentFromEntity(
    const Entity& entity,
    const GeoPoint& pick,
    const QRectF& visibleModelRect,
    const double tolerance)
{
    if (entity.Type() == EntityType::Line) {
        return static_cast<const LineEntity&>(entity).Line();
    }
    if (entity.Type() == EntityType::Polyline) {
        const GeoShape shape = entity.Shaper();
        GeoBounds clip = ModelRectToGeoBounds(visibleModelRect);
        const GeoBounds bounds = shape.Bounds();
        if (bounds.valid) {
            clip = bounds;
        }
        double bestDistance = tolerance;
        std::optional<GeoLine> bestLine;
        for (const GeoLine& segment : shape.ToSegments(clip)) {
            const double distance = DistancePointToSegment(pick, segment.Start(), segment.End());
            if (distance <= bestDistance) {
                bestDistance = distance;
                bestLine = segment;
            }
        }
        return bestLine;
    }
    return std::nullopt;
}

GeoPoint PointOnCircle(const GeoPoint& center, const double radius, const double angleRadians)
{
    return GeoPoint(
        center.X() + std::cos(angleRadians) * radius,
        center.Y() + std::sin(angleRadians) * radius);
}

} // namespace

CadView::CadView(QWidget* parent)
    : QGraphicsView(parent)
{
    setDragMode(QGraphicsView::NoDrag);
    // CAD-style navigation: never use AnchorUnderMouse. Wheel zoom anchors
    // explicitly; resize must not re-anchor around a stale mouse cache or the
    // next pick after zoom lands on the wrong model point / appears dead.
    setTransformationAnchor(QGraphicsView::NoAnchor);
    setResizeAnchor(QGraphicsView::AnchorViewCenter);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setViewportUpdateMode(QGraphicsView::FullViewportUpdate);
    // Expand item paint bounds for AA fringe so soft edges are not clipped.
    setOptimizationFlag(QGraphicsView::DontAdjustForAntialiasing, false);
    setOptimizationFlag(QGraphicsView::DontSavePainterState, true);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setInteractive(true);
    // 去掉 QGraphicsView 默认的 QFrame 边框，使 viewport 填满整个 view，
    // 消除边框"死区"——否则鼠标移到边框区域会离开 viewport，十字线 overlay
    // 不再跟踪，系统光标恢复成箭头（且该区域无网格），看起来像光标跳到了外侧。
    setFrameShape(QFrame::NoFrame);
    setLineWidth(0);
    setMidLineWidth(0);

    ApplyCursorSettingsToPickSnap();
    ApplyGraphicsConfigToViewport();
    EnsureViewportInputReady();
    // 十字线改由 drawForeground 绘制，不再使用透明 overlay widget。
    // 透明 overlay + CompositionMode_Clear 会擦掉下方网格/场景，导致鼠标进入
    // 时画面变黑、移出后才恢复网格（图1/图2 问题）。
}

void CadView::SetDocument(CadDocument* document)
{
    document_ = document;
}

void CadView::SetScene(CadScene* scene)
{
    cadScene_ = scene;
    QGraphicsView::setScene(scene);
    if (cadScene_) {
        cadScene_->SetViewportRefreshCallback([this]() {
            RefreshViewport();
        });
    }
}

void CadView::SetCommandManager(CommandManager* commandManager)
{
    commandManager_ = commandManager;
}

void CadView::SetStatusCallback(std::function<void(const QString&)> callback)
{
    statusCallback_ = std::move(callback);
}

void CadView::SetToolModeChangedCallback(std::function<void(ToolMode)> callback)
{
    toolModeChangedCallback_ = std::move(callback);
}

void CadView::SetToolMode(ToolMode mode)
{
    ClearTextHover();
    ResetInteractionState();
    editHelperEntityId_ = 0;
    if (mode != ToolMode::InsertBlock) {
        pendingBlockInsert_.reset();
    }
    toolMode_ = mode;
    if (mode != ToolMode::ArrayPath && mode != ToolMode::ArrayPolar) {
        ClearArraySession();
    }
    drawOptions_.method = DrawToolEngine::DefaultMethod(mode);
    drawSegmentFinished_ = false;
    ResetTransientState();
    if (mode == ToolMode::Mirror) {
        BeginMirrorWorkflow();
    } else {
        ResetMirrorState();
        if (mode == ToolMode::Fillet || mode == ToolMode::Chamfer) {
            ResetFilletChamferSession();
            EmitStatus(FilletChamferStatusBarText());
            UpdateFilletChamferHud();
        } else if (mode == ToolMode::DrawPolygon) {
            EmitStatus(PolygonPromptText());
            UpdatePolygonHud();
        } else if (mode == ToolMode::ArrayPolar) {
            EmitStatus(ArrayPromptText());
            UpdateArrayHud();
        } else {
            EmitStatus(ToolStatusPrompt(mode));
        }
    }
    if (toolModeChangedCallback_) {
        toolModeChangedCallback_(mode);
    }
    UpdateCursorOverlay(lastCursorViewportPos_);
    setFocus(Qt::OtherFocusReason);
    QTimer::singleShot(0, this, [this]() {
        setFocus(Qt::OtherFocusReason);
    });
    if (ShouldShowDynPointInput() && viewport()) {
        QPoint viewPos = lastCursorViewportPos_;
        if (!viewport()->rect().contains(viewPos)) {
            viewPos = viewport()->rect().center();
        }
        UpdatePointInput(viewPos, ApplyOrtho(ApplySnap(MapToModel(viewPos))));
    } else if (!pasteTextPending_) {
        HidePointInput();
    }
}

bool CadView::CreateRectangularArray(const RectangularArrayOptions& options)
{
    if (!document_ || !commandManager_) {
        return false;
    }
    const std::vector<EntityId> sourceIds =
        document_->EditableEntityIds(document_->SelectedEntityIds());
    if (sourceIds.empty()) {
        EmitStatus(QStringLiteral("矩形阵列：请先选择要阵列的对象"));
        return false;
    }
    const bool ok = commandManager_->CreateRectangularArray(sourceIds, options);
    EmitStatus(ok
        ? QStringLiteral("矩形阵列完成：%1 行 × %2 列")
              .arg(std::max(1, options.rows))
              .arg(std::max(1, options.columns))
        : QStringLiteral("矩形阵列失败：行列数量必须产生至少一个副本"));
    SetToolMode(ToolMode::Select);
    return ok;
}

bool CadView::BeginPathArray(const PathArrayOptions& options)
{
    if (!document_ || !commandManager_) {
        return false;
    }
    const std::vector<EntityId> sourceIds =
        document_->EditableEntityIds(document_->SelectedEntityIds());
    if (sourceIds.empty()) {
        EmitStatus(QStringLiteral("路径阵列：请先选择要阵列的对象"));
        return false;
    }
    arraySourceIds_ = sourceIds;
    pathArrayOptions_ = options;
    SetToolMode(ToolMode::ArrayPath);
    EmitStatus(QStringLiteral("路径阵列：选择直线、圆弧、圆、椭圆、多段线、多边形或样条作为路径"));
    return true;
}

bool CadView::BeginPolarArray(const PolarArrayOptions& options)
{
    if (!document_ || !commandManager_) {
        return false;
    }
    const std::vector<EntityId> sourceIds =
        document_->EditableEntityIds(document_->SelectedEntityIds());
    if (sourceIds.empty()) {
        EmitStatus(QStringLiteral("环形阵列：请先选择要阵列的对象"));
        return false;
    }
    arraySourceIds_ = sourceIds;
    polarArrayOptions_ = options;
    polarArrayOptions_.itemCount = 6;
    polarArrayOptions_.fillAngleDegrees = 360.0;
    polarArrayOptions_.rotateItems = true;
    polarArrayOptions_.basePoint.reset();
    arrayPhase_ = ArrayCommandPhase::Center;
    arrayCenter_.reset();
    SetToolMode(ToolMode::ArrayPolar);
    EmitStatus(ArrayPromptText());
    UpdateArrayHud();
    return true;
}

void CadView::ClearArraySession()
{
    arraySourceIds_.clear();
    arrayPhase_ = ArrayCommandPhase::Center;
    arrayCenter_.reset();
    polarArrayOptions_.basePoint.reset();
}

bool CadView::CancelActiveCommand()
{
    if (IsSplineGripEditing()) {
        CancelSplineGripEdit();
        EmitStatus(QStringLiteral("已取消样条夹点编辑"));
        RefreshViewport();
        return true;
    }
    if (pasteTextPending_) {
        EndTextPasteSession(true);
        RefreshViewport();
        return true;
    }
    if (toolMode_ == ToolMode::Select) {
        const bool hadSelection = document_ && !document_->SelectedEntityIds().empty();
        ResetInteractionState();
        ResetTransientState();
        if (hadSelection) {
            document_->ClearSelection();
        }
        RefreshViewport();
        return hadSelection;
    }
    SetToolMode(ToolMode::Select);
    RefreshViewport();
    return true;
}

void CadView::BeginInsertBlock(PendingBlockInsert params)
{
    pendingBlockInsert_ = std::move(params);
    SetToolMode(ToolMode::InsertBlock);
    EmitStatus(QStringLiteral("插入块「%1」：指定插入点")
        .arg(QString::fromStdString(pendingBlockInsert_->blockName)));
}

void CadView::SetCursorSettings(const CursorSettings& settings)
{
    cursorSettings_ = settings;
    ApplyCursorSettingsToPickSnap();
    UpdateCursorOverlay(lastCursorViewportPos_);
}

void CadView::SetGraphicsConfig(const GraphicsConfig& config)
{
    GraphicsConfig normalized = config;
    normalized.Normalize();
    graphicsConfig_ = normalized;
    ApplyGraphicsConfigToViewport();
    RegenerateCurveDisplay();
}

bool CadView::IsHardwareAccelerationActive() const
{
    return qobject_cast<const QOpenGLWidget*>(viewport()) != nullptr;
}

void CadView::ApplyGraphicsConfigToViewport()
{
    graphicsConfig_.Normalize();

    QPainter::RenderHints hints = QPainter::TextAntialiasing | QPainter::SmoothPixmapTransform;
    if (graphicsConfig_.lineSmoothing || graphicsConfig_.hardwareAcceleration) {
        hints |= QPainter::Antialiasing;
    }
    setRenderHints(hints);
    setViewportUpdateMode(QGraphicsView::FullViewportUpdate);

    const bool wantGl = graphicsConfig_.hardwareAcceleration;
    auto* currentGl = qobject_cast<QOpenGLWidget*>(viewport());

    if (wantGl) {
        const int samples = graphicsConfig_.msaaSamples;
        const bool needNew = currentGl == nullptr
            || currentGl->format().samples() != samples;
        if (needNew) {
            auto* glViewport = new QOpenGLWidget;
            QSurfaceFormat format = QSurfaceFormat::defaultFormat();
            format.setRenderableType(QSurfaceFormat::OpenGL);
            format.setSwapBehavior(QSurfaceFormat::DoubleBuffer);
            format.setDepthBufferSize(24);
            format.setStencilBufferSize(8);
            format.setSamples(samples);
            glViewport->setFormat(format);
            glViewport->setUpdateBehavior(QOpenGLWidget::NoPartialUpdate);
            setViewport(glViewport);
        }
    } else if (currentGl != nullptr) {
        // Restore software QWidget viewport (QGraphicsView takes ownership).
        setViewport(new QWidget);
    }
    // setViewport() can reset tracking; re-assert CAD input defaults.
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setTransformationAnchor(QGraphicsView::NoAnchor);
    setResizeAnchor(QGraphicsView::AnchorViewCenter);
    EnsureViewportInputReady();
}

void CadView::EnsureViewportInputReady()
{
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setInteractive(true);
    if (QWidget* vp = viewport()) {
        vp->setMouseTracking(true);
        vp->setAttribute(Qt::WA_Hover, true);
    }
}

void CadView::BeginSuppressViewportLeave(int holdMs)
{
    ignoreViewportLeave_ = true;
    ++viewportLeaveSuppressDepth_;
    const int token = viewportLeaveSuppressDepth_;
    QTimer::singleShot(std::max(0, holdMs), this, [this, token]() {
        if (token != viewportLeaveSuppressDepth_) {
            return;
        }
        ignoreViewportLeave_ = false;
        RestorePointerInteractionFromGlobalCursor();
    });
}

bool CadView::ShouldSuppressViewportLeave() const
{
    if (ignoreViewportLeave_) {
        return true;
    }

    const QPoint globalPos = QCursor::pos();
    if (rect().contains(mapFromGlobal(globalPos))) {
        return true;
    }
    if (viewport()) {
        const QPoint viewportPos = viewport()->mapFromGlobal(globalPos);
        if (viewport()->rect().contains(viewportPos)) {
            return true;
        }
    }
    return false;
}

void CadView::RestorePointerInteractionFromGlobalCursor()
{
    if (!viewport()) {
        return;
    }
    const QPoint viewportPos = viewport()->mapFromGlobal(QCursor::pos());
    if (!viewport()->rect().contains(viewportPos)) {
        return;
    }

    cursorInsideView_ = true;
    crosshairPosValid_ = true;
    EnsureViewportInputReady();
    UpdateCursorOverlay(viewportPos);
    RefreshDrawInteractionAfterViewChange(viewportPos);
}

void CadView::ZoomByFactorAtViewportPos(double factor, const QPoint& viewportPos)
{
    if (factor <= 0.0 || !viewport()) {
        return;
    }

    const double currentScale = ViewScale();
    const double targetScale = std::clamp(currentScale * factor, Tolerance::MinZoom, Tolerance::MaxZoom);
    if (Tolerance::IsEqual(currentScale, targetScale)) {
        return;
    }

    // Zoom about the cursor with an explicit scene anchor. Keep anchors on
    // NoAnchor / ViewCenter so Qt never re-anchors around a stale mouse cache.
    const QPointF sceneAnchor = mapToScene(viewportPos);
    scale(targetScale / currentScale, targetScale / currentScale);
    const QPointF delta = mapToScene(viewportPos) - sceneAnchor;
    translate(delta.x(), delta.y());

    // Cosmetic linetype dash lengths depend on pixels-per-model-unit; refresh
    // after zoom so Divide/Center/etc. keep AutoCAD-correct on-screen density.
    if (previewItem_) {
        ApplyPreviewPen();
    }
    if (cadScene_) {
        cadScene_->InvalidateItemRegions();
    }
}

void CadView::SetViewRes(int value)
{
    const int clamped = ViewDisplaySettings::ClampViewRes(value);
    if (viewRes_ == clamped) {
        return;
    }
    viewRes_ = clamped;
    RegenerateCurveDisplay();
}

double CadView::PixelsPerModelUnit() const
{
    return ViewScale();
}

CurveDisplayOptions CadView::MakeCurveDisplayOptions(bool useTrueCurves) const
{
    CurveDisplayOptions options;
    options.viewRes = viewRes_;
    options.pixelsPerModelUnit = PixelsPerModelUnit();
    // Printer always wants true curves.
    // Software viewport + HQGEOM: Qt's native arcTo is excellent.
    // OpenGL paint engine tessellates arcTo with a coarse fixed LOD — when zoomed
    // in, arcs become visible polygons. Prefer zoom-adaptive chord fitting instead.
    const bool openGlViewport = IsHardwareAccelerationActive();
    options.useTrueCurves = useTrueCurves
        || (!openGlViewport && graphicsConfig_.highQualityGeometry);
    return options;
}

void CadView::RegenerateCurveDisplay()
{
    if (cadScene_) {
        cadScene_->InvalidateItemRegions();
    }
    RefreshViewport();
}

void CadView::RefreshDrawInteractionAfterViewChange(const QPoint& viewportPos)
{
    // Keep active draw/edit commands alive across zoom/pan: refresh snap + preview
    // at the current cursor so the next pick continues without an extra click.
    if (panning_ || boxSelecting_ || draggingSelection_ || draggingGrip_) {
        return;
    }
    if (IsPolylineOptionEditing()) {
        return;
    }
    if (!IsDrawTool(toolMode_) && !IsModifyTool(toolMode_) && !IsDimensionTool(toolMode_)
        && !IsDrawInProgress() && toolMode_ != ToolMode::Mirror) {
        return;
    }

    const GeoPoint rawPoint = MapToModel(viewportPos);
    const bool dimDefPick = IsDimensionDefPointPickActive();
    const bool ttrObjectPick = IsCircleTangentObjectPick();
    GeoPoint modelPoint;
    if (ttrObjectPick) {
        ClearDimensionSnapPreview();
        if (const std::optional<ObjectSnapHit> objectSnap = FindObjectSnapHit(rawPoint)) {
            modelPoint = ResolveTtrObjectPickPoint(rawPoint);
            UpdateSnapMarker(objectSnap->point, true, objectSnap->kind);
        } else {
            modelPoint = rawPoint;
            UpdateSnapMarker(rawPoint, false, ObjectSnapKind::Unknown);
        }
        if (cadScene_) {
            cadScene_->SetHoveredEntity(HitTest(rawPoint));
        }
    } else if (dimDefPick) {
        UpdateDimensionSnapPreview(rawPoint);
        if (const std::optional<ObjectSnapHit> objectSnap = FindObjectSnapHit(rawPoint)) {
            modelPoint = objectSnap->point;
            UpdateSnapMarker(objectSnap->point, true, objectSnap->kind);
        } else if (const std::optional<GeoPoint> dimSnap = DimensionSnapPointAt(rawPoint)) {
            modelPoint = *dimSnap;
            UpdateSnapMarker(*dimSnap, true, ObjectSnapKind::Endpoint);
        } else {
            modelPoint = ResolveInputPoint(rawPoint);
            UpdateSnapMarker(modelPoint, IsGridSnapActive(false), ObjectSnapKind::Unknown);
        }
    } else {
        ClearDimensionSnapPreview();
        if (const std::optional<ObjectSnapHit> objectSnap = FindObjectSnapHit(rawPoint)) {
            modelPoint = objectSnap->point;
            UpdateSnapMarker(objectSnap->point, true, objectSnap->kind);
        } else {
            modelPoint = ResolveInputPoint(rawPoint);
            UpdateSnapMarker(
                modelPoint,
                IsGridSnapActive(false),
                ObjectSnapKind::Unknown);
        }
    }

    UpdatePreview(modelPoint);

    // DYN HUD boxes are viewport widgets (not scene items). Re-anchor after zoom/pan.
    if (ShouldShowDynPointInput()) {
        UpdatePointInput(viewportPos, modelPoint);
    } else {
        GeoPoint polarBase;
        if (TryGetPolarBasePoint(&polarBase)) {
            UpdatePolarInput(viewportPos, modelPoint);
        } else if (IsCircleTangentObjectPick() && !inputPoints_.empty()) {
            HidePolarInput();
            ShowDynPrompt(
                inputPoints_.size() == 1
                    ? QStringLiteral("指定圆的第二条切线:")
                    : QStringLiteral("指定圆的第三条切线:"),
                viewportPos);
        }
    }
}

void CadView::ApplyCursorSettingsToPickSnap()
{
    snapTolerancePixels_ = static_cast<double>(qMax(1, cursorSettings_.pickboxPixels));
    snapAperturePixels_ = static_cast<double>(qMax(1, cursorSettings_.aperturePixels));
}

bool CadView::IsDimensionTool(const ToolMode mode) const
{
    switch (mode) {
    case ToolMode::DrawDimLinear:
    case ToolMode::DrawDimAligned:
    case ToolMode::DrawDimRadius:
    case ToolMode::DrawDimJoggedRadius:
    case ToolMode::DrawDimDiameter:
    case ToolMode::DrawDimAngular:
    case ToolMode::DrawDimOrdinate:
        return true;
    default:
        return false;
    }
}

bool CadView::IsModifyTool(const ToolMode mode) const
{
    switch (mode) {
    case ToolMode::Move:
    case ToolMode::Mirror:
    case ToolMode::Rotate:
    case ToolMode::Scale:
    case ToolMode::Trim:
    case ToolMode::Extend:
    case ToolMode::Break:
    case ToolMode::Offset:
    case ToolMode::ArrayRectangular:
    case ToolMode::ArrayPath:
    case ToolMode::ArrayPolar:
    case ToolMode::Fillet:
    case ToolMode::Chamfer:
        return true;
    default:
        return false;
    }
}

bool CadView::IsDrawingCommand(const ToolMode mode) const
{
    return IsDrawTool(mode)
        || IsDimensionTool(mode)
        || mode == ToolMode::DrawText
        || mode == ToolMode::DrawMText;
}

bool CadView::UsesObjectSnapAperture() const
{
    if (!snapEnabled_ || !cursorSettings_.showAutoSnapAperture || !EffectiveObjectSnapModes().AnyEnabled()) {
        return false;
    }
    return IsDrawingCommand(toolMode_) || IsDimensionDefPointPickActive();
}

CadCursorDisplay CadView::ResolveCursorDisplay() const
{
    if (panning_) {
        return CadCursorDisplay::Hidden;
    }
    if (hoveredTextEntityId_ != 0 && toolMode_ == ToolMode::Select) {
        return CadCursorDisplay::Hidden;
    }
    if (cursorSettings_.cursorType != 0) {
        return CadCursorDisplay::SystemOnly;
    }
    if (IsModifyTool(toolMode_)) {
        return CadCursorDisplay::PickboxOnly;
    }
    if (IsDrawingCommand(toolMode_)) {
        if (UsesObjectSnapAperture()) {
            return CadCursorDisplay::CrosshairWithAperture;
        }
        return CadCursorDisplay::CrosshairOnly;
    }
    return CadCursorDisplay::CrosshairWithPickbox;
}

void CadView::ApplyCrosshairCursorShape(const CadCursorDisplay display)
{
    if (display == CadCursorDisplay::Hidden) {
        if (panning_) {
            setCursor(Qt::ClosedHandCursor);
            if (viewport()) {
                viewport()->setCursor(Qt::ClosedHandCursor);
            }
        } else if (hoveredTextEntityId_ != 0) {
            setCursor(Qt::IBeamCursor);
            if (viewport()) {
                viewport()->setCursor(Qt::IBeamCursor);
            }
        } else {
            unsetCursor();
            if (viewport()) {
                viewport()->unsetCursor();
            }
        }
        return;
    }

    if (display == CadCursorDisplay::SystemOnly) {
        setCursor(Qt::ArrowCursor);
        if (viewport()) {
            viewport()->setCursor(Qt::ArrowCursor);
        }
        return;
    }

    setCursor(Qt::BlankCursor);
    if (viewport()) {
        viewport()->setCursor(Qt::BlankCursor);
    }
}

void CadView::PaintCadCursorForeground(QPainter* painter) const
{
    if (!crosshairPosValid_
        || !cursorInsideView_
        || cursorDisplay_ == CadCursorDisplay::Hidden
        || cursorDisplay_ == CadCursorDisplay::SystemOnly
        || cursorSettings_.cursorType != 0
        || !viewport()) {
        return;
    }

    const bool showCrosshair = cursorDisplay_ == CadCursorDisplay::CrosshairOnly
        || cursorDisplay_ == CadCursorDisplay::CrosshairWithPickbox
        || cursorDisplay_ == CadCursorDisplay::CrosshairWithAperture;
    const bool showPickbox = cursorDisplay_ == CadCursorDisplay::CrosshairWithPickbox
        || cursorDisplay_ == CadCursorDisplay::PickboxOnly;
    const bool showAperture = cursorDisplay_ == CadCursorDisplay::CrosshairWithAperture
        && cursorSettings_.showAutoSnapAperture;
    if (!showCrosshair && !showPickbox && !showAperture) {
        return;
    }

    // AutoCAD: crosshair / pickbox sizes are screen pixels and do not change with zoom.
    // Always paint in device (viewport) coordinates — never scene units.
    painter->save();
    painter->resetTransform();

    const QPointF deviceCenter = viewportTransform().map(mapToScene(lastCursorViewportPos_));
    const int cx = qRound(deviceCenter.x());
    const int cy = qRound(deviceCenter.y());
    const QColor color = cursorSettings_.crosshairColor;

    if (IsFilletChamferCommand() && !filletChamferHudNotice_.isEmpty()) {
        QPen prohibitPen(color);
        prohibitPen.setWidthF(1.6);
        prohibitPen.setCosmetic(true);
        painter->setPen(prohibitPen);
        painter->setBrush(Qt::NoBrush);
        const qreal radius = 8.0;
        painter->drawEllipse(deviceCenter, radius, radius);
        const qreal slash = radius * 0.72;
        painter->drawLine(deviceCenter + QPointF(-slash, -slash), deviceCenter + QPointF(slash, slash));
        painter->restore();
        return;
    }

    if (showCrosshair) {
        QPen crossPen(color);
        crossPen.setWidth(0);
        crossPen.setCosmetic(true);
        painter->setPen(crossPen);
        painter->setBrush(Qt::NoBrush);
        if (cursorSettings_.crosshairSizePercent >= 100) {
            painter->drawLine(cx, 0, cx, viewport()->height());
            painter->drawLine(0, cy, viewport()->width(), cy);
        } else {
            const int reference = qMin(viewport()->width(), viewport()->height());
            const int halfLen = qMax(1, reference * cursorSettings_.crosshairSizePercent / 200);
            painter->drawLine(cx - halfLen, cy, cx + halfLen, cy);
            painter->drawLine(cx, cy - halfLen, cx, cy + halfLen);
        }
    }

    if (showPickbox) {
        const int size = qMax(2, cursorSettings_.pickboxPixels);
        const int half = size / 2;
        QPen pickPen(color);
        pickPen.setWidth(0);
        pickPen.setCosmetic(true);
        painter->setPen(pickPen);
        painter->setBrush(Qt::NoBrush);
        painter->drawRect(QRectF(deviceCenter.x() - half, deviceCenter.y() - half, size, size));
    }

    if (showAperture) {
        const int size = qMax(4, cursorSettings_.aperturePixels);
        const int half = size / 2;
        QPen aperturePen(color);
        aperturePen.setWidth(0);
        aperturePen.setCosmetic(true);
        aperturePen.setStyle(Qt::DashLine);
        painter->setPen(aperturePen);
        painter->setBrush(Qt::NoBrush);
        painter->drawRect(QRectF(deviceCenter.x() - half, deviceCenter.y() - half, size, size));
    }

    painter->restore();
}

void CadView::drawForeground(QPainter* painter, const QRectF& rect)
{
    QGraphicsView::drawForeground(painter, rect);
    PaintCadCursorForeground(painter);
}

void CadView::RepaintViewport()
{
    if (viewport()) {
        viewport()->update(viewport()->rect());
    }
}

void CadView::ScheduleViewportRepaint(const QRectF& sceneDirtyRect)
{
    Q_UNUSED(sceneDirtyRect);
    RepaintViewport();
}

void CadView::UpdateCursorOverlay(const QPoint& viewportPos)
{
    lastCursorViewportPos_ = viewportPos;
    crosshairPosValid_ = cursorInsideView_;
    cursorDisplay_ = ResolveCursorDisplay();
    ApplyCrosshairCursorShape(cursorDisplay_);
    RepaintViewport();
}

void CadView::resizeEvent(QResizeEvent* event)
{
    QGraphicsView::resizeEvent(event);
    if (crosshairPosValid_ && cursorInsideView_) {
        UpdateCursorOverlay(lastCursorViewportPos_);
    }
    InvalidateGridBackground();
}

void CadView::showEvent(QShowEvent* event)
{
    QGraphicsView::showEvent(event);
    EnsureSceneCoversViewport();
    QTimer::singleShot(0, this, [this]() {
        EnsureSceneCoversViewport();
        if (QGraphicsScene* s = scene()) {
            s->invalidate(s->sceneRect(), QGraphicsScene::BackgroundLayer);
        }
        if (underMouse()) {
            cursorInsideView_ = true;
            const QPoint viewPos = mapFromGlobal(QCursor::pos());
            if (viewport() && viewport()->rect().contains(viewport()->mapFromParent(viewPos))) {
                UpdateCursorOverlay(viewport()->mapFromParent(viewPos));
            }
        }
        RefreshViewport();
    });
}

void CadView::SetDrawMethod(DrawMethod method)
{
    drawOptions_.method = method;
    drawOptions_.arcContinue = lastArcContinue_;
    drawSegmentFinished_ = false;
    ResetTransientState();
    if (method == DrawMethod::ArcContinue) {
        if (lastArcContinue_.valid) {
            EmitStatus(QStringLiteral("指定圆弧的端点"));
        } else {
            EmitStatus(QStringLiteral("连续：没有可衔接的直线、多段线或圆弧"));
        }
    } else if (method == DrawMethod::CircleTanTanRadius
        || method == DrawMethod::CircleTanTanTan) {
        EmitStatus(QStringLiteral("指定圆的第一条切线"));
    } else if (toolMode_ == ToolMode::DrawPolygon) {
        polygonPhase_ = (method == DrawMethod::PolygonEdge)
            ? PolygonCommandPhase::Edge
            : PolygonCommandPhase::Center;
        EmitStatus(PolygonPromptText());
        UpdatePolygonHud();
    }
    if (ShouldShowDynPointInput() && viewport()) {
        QPoint viewPos = lastCursorViewportPos_;
        if (!viewport()->rect().contains(viewPos)) {
            viewPos = viewport()->rect().center();
        }
        UpdatePointInput(viewPos, ApplyOrtho(ApplySnap(MapToModel(viewPos))));
    } else if (IsArcContinuePick() && viewport()) {
        QPoint viewPos = lastCursorViewportPos_;
        if (!viewport()->rect().contains(viewPos)) {
            viewPos = viewport()->rect().center();
        }
        const GeoPoint model = ApplyOrtho(ApplySnap(MapToModel(viewPos)));
        UpdatePolarInput(viewPos, model);
        UpdatePreview(model);
    }
}

void CadView::SetPolygonSides(int sides)
{
    drawOptions_.polygonSides = std::max(3, sides);
}

void CadView::SetCornerDistance(double distance)
{
    cornerDistance_ = std::max(0.0, distance);
    filletRadius_ = cornerDistance_;
    chamferDist1_ = cornerDistance_;
    chamferDist2_ = cornerDistance_;
    chamferLength_ = cornerDistance_;
}

void CadView::SetFilletRadius(double radius)
{
    filletRadius_ = std::max(0.0, radius);
    cornerDistance_ = filletRadius_;
}

void CadView::SetChamferDistances(double first, double second)
{
    chamferDist1_ = std::max(0.0, first);
    chamferDist2_ = std::max(0.0, second);
    cornerDistance_ = chamferDist1_;
}

void CadView::SetDrawOptions(const DrawToolOptions& options)
{
    drawOptions_ = options;
    drawOptions_.arcContinue = lastArcContinue_;
}

void CadView::SyncArcContinueOptions()
{
    drawOptions_.arcContinue = lastArcContinue_;
}

void CadView::SyncSeaIncludedAngleOptions()
{
    drawOptions_.hasExplicitIncludedAngle = false;
    drawOptions_.explicitIncludedAngleDegrees = 0.0;
    if (toolMode_ != ToolMode::DrawArc || !polarInput_ || !polarInput_->isVisible()
        || !polarInput_->IsAngleLocked()) {
        return;
    }
    // 按 method 判断，不依赖 IsArc*Pick（后者要求 size==2；提交时可能已 push 第三点）。
    // SEA：仅用幅值；圆心/起点-角度：保留符号（负=顺时针）。
    if (drawOptions_.method == DrawMethod::ArcStartEndAngle) {
        drawOptions_.hasExplicitIncludedAngle = true;
        drawOptions_.explicitIncludedAngleDegrees = std::abs(polarInput_->AngleDegrees());
        return;
    }
    if (drawOptions_.method == DrawMethod::ArcStartCenterAngle
        || drawOptions_.method == DrawMethod::ArcCenterStartAngle) {
        drawOptions_.hasExplicitIncludedAngle = true;
        drawOptions_.explicitIncludedAngleDegrees = polarInput_->AngleDegrees();
    }
}

void CadView::SyncSclChordLengthOptions()
{
    drawOptions_.hasExplicitChordLength = false;
    drawOptions_.explicitChordLength = 0.0;
    if (toolMode_ != ToolMode::DrawArc
        || (drawOptions_.method != DrawMethod::ArcStartCenterChord
            && drawOptions_.method != DrawMethod::ArcCenterStartChord)
        || !polarInput_ || !polarInput_->isVisible()
        || !polarInput_->IsLengthLocked()) {
        return;
    }
    // AutoCAD：键入负长度 → 优弧；幅值由 |length| 决定。
    drawOptions_.hasExplicitChordLength = true;
    drawOptions_.explicitChordLength = polarInput_->Length();
}

void CadView::NoteArcContinuation(const Entity& entity)
{
    DrawToolEngine::UpdateArcContinueInfo(entity, lastArcContinue_);
    drawOptions_.arcContinue = lastArcContinue_;
}

void CadView::ResetInteractionState()
{
    CancelDrag();
    CancelGripDrag();
}

void CadView::SetGridEnabled(bool enabled)
{
    gridVisible_ = enabled;
    if (QGraphicsScene* s = scene()) {
        s->invalidate(s->sceneRect(), QGraphicsScene::BackgroundLayer);
    }
    RefreshViewport();
}

void CadView::SetSnapEnabled(bool enabled)
{
    snapEnabled_ = enabled;
    if (!enabled) {
        ClearSnapMarker();
    }
    UpdateCursorOverlay(lastCursorViewportPos_);
}

void CadView::SetObjectSnapModes(const ObjectSnapModes& modes)
{
    objectSnapModes_ = modes;
    UpdateCursorOverlay(lastCursorViewportPos_);
}

void CadView::SetOrthoEnabled(bool enabled)
{
    orthoEnabled_ = enabled;
}

void CadView::drawBackground(QPainter* painter, const QRectF& rect)
{
    QGraphicsView::drawBackground(painter, rect);

    if (!gridVisible_) {
        return;
    }

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, false);
    double gridStep = gridSize_;
    while (gridStep * ViewScale() < Tolerance::MinGridPixelStep) {
        gridStep *= 2.0;
    }

    QPen minorPen(QColor(52, 58, 66), 1.0);
    minorPen.setCosmetic(true);
    QPen majorPen(QColor(72, 80, 90), 1.0);
    majorPen.setCosmetic(true);
    QPen axisPen(QColor(110, 120, 132), 1.0);
    axisPen.setCosmetic(true);

    const int left = static_cast<int>(std::floor(rect.left() / gridStep));
    const int right = static_cast<int>(std::ceil(rect.right() / gridStep));
    const int top = static_cast<int>(std::floor(rect.top() / gridStep));
    const int bottom = static_cast<int>(std::ceil(rect.bottom() / gridStep));
    for (int x = left; x <= right; ++x) {
        painter->setPen((x % 5 == 0) ? majorPen : minorPen);
        painter->drawLine(QPointF(x * gridStep, rect.top()), QPointF(x * gridStep, rect.bottom()));
    }
    for (int y = top; y <= bottom; ++y) {
        painter->setPen((y % 5 == 0) ? majorPen : minorPen);
        painter->drawLine(QPointF(rect.left(), y * gridStep), QPointF(rect.right(), y * gridStep));
    }
    painter->setPen(axisPen);
    painter->drawLine(QPointF(rect.left(), 0.0), QPointF(rect.right(), 0.0));
    painter->drawLine(QPointF(0.0, rect.top()), QPointF(0.0, rect.bottom()));
    painter->restore();
}

void CadView::wheelEvent(QWheelEvent* event)
{
    // AutoCAD-style wheel zoom: roll the wheel to zoom in/out centered on the
    // cursor. Ctrl accelerates the zoom factor; Shift pans horizontally.
    QPoint viewportPos = event->position().toPoint();
    if (viewport()) {
        const QPoint fromGlobal = viewport()->mapFromGlobal(event->globalPosition().toPoint());
        if (viewport()->rect().contains(fromGlobal)) {
            viewportPos = fromGlobal;
        }
    }

    if (event->modifiers() & Qt::ShiftModifier) {
        const int delta = event->angleDelta().y();
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - delta);
        EnsureSceneCoversViewport();
        InvalidateGridBackground();
        BeginSuppressViewportLeave();
        RestorePointerInteractionFromGlobalCursor();
        event->accept();
        return;
    }

    const double step = (event->modifiers() & Qt::ControlModifier) ? 1.5 : 1.2;
    const double factor = event->angleDelta().y() > 0 ? step : 1.0 / step;

    BeginSuppressViewportLeave();
    ZoomByFactorAtViewportPos(factor, viewportPos);
    EnsureSceneCoversViewport();
    InvalidateGridBackground();

    cursorInsideView_ = true;
    crosshairPosValid_ = true;
    EnsureViewportInputReady();
    UpdateCursorOverlay(viewportPos);
    RefreshDrawInteractionAfterViewChange(viewportPos);

    // Force a deferred move-sync so preview/snap resume even if Windows/OpenGL
    // suppressed mouse-move events during the wheel gesture.
    QTimer::singleShot(0, this, [this]() {
        RestorePointerInteractionFromGlobalCursor();
    });
    event->accept();
}

bool CadView::viewportEvent(QEvent* event)
{
    if (event->type() == QEvent::Leave && ShouldSuppressViewportLeave()) {
        cursorInsideView_ = true;
        return QGraphicsView::viewportEvent(event);
    }
    if (event->type() == QEvent::Enter || event->type() == QEvent::HoverEnter) {
        cursorInsideView_ = true;
        EnsureViewportInputReady();
    }

    // QGraphicsView filters viewport mouse events before child HUD widgets.
    // Let the option dropdown / keyword field receive clicks and hover.
    if (optionInput_ && optionInput_->isVisible()) {
        QPoint globalPos;
        bool hasGlobal = false;
        switch (event->type()) {
        case QEvent::MouseButtonPress:
        case QEvent::MouseButtonRelease:
        case QEvent::MouseButtonDblClick:
        case QEvent::MouseMove:
            globalPos = static_cast<QMouseEvent*>(event)->globalPosition().toPoint();
            hasGlobal = true;
            break;
        case QEvent::HoverMove:
        case QEvent::HoverEnter:
        case QEvent::HoverLeave:
            globalPos = static_cast<QHoverEvent*>(event)->globalPosition().toPoint();
            hasGlobal = true;
            break;
        default:
            break;
        }
        if (hasGlobal && optionInput_->ContainsGlobalPos(globalPos)) {
            return false;
        }
    }

    if (IsPolylineOptionEditing() && !panning_) {
        if (event->type() == QEvent::MouseMove || event->type() == QEvent::HoverMove) {
            QPoint pos;
            if (event->type() == QEvent::MouseMove) {
                pos = static_cast<QMouseEvent*>(event)->pos();
            } else {
                pos = static_cast<QHoverEvent*>(event)->position().toPoint();
            }
            KeepPolylineKeywordEditPointer(pos);
            return true;
        }
        if (event->type() == QEvent::MouseButtonPress
            || event->type() == QEvent::MouseButtonDblClick) {
            auto* mouse = static_cast<QMouseEvent*>(event);
            if (mouse->button() == Qt::LeftButton) {
                EmitStatus(QStringLiteral("请先确认或取消当前选项"));
                if (optionInput_) {
                    optionInput_->FocusKeywordEdit();
                }
                return true;
            }
        }
    }
    return QGraphicsView::viewportEvent(event);
}

void CadView::mousePressEvent(QMouseEvent* event)
{
    cursorInsideView_ = true;
    EnsureViewportInputReady();

    if (event->button() == Qt::RightButton && toolMode_ == ToolMode::ArrayPolar) {
        if (arrayPhase_ == ArrayCommandPhase::Edit) {
            CommitPolarArray();
        } else if (arrayPhase_ == ArrayCommandPhase::Center) {
            SetToolMode(ToolMode::Select);
        } else if (optionInput_ && optionInput_->IsKeywordEditing()
            && (IsArrayNumericPhase() || arrayPhase_ == ArrayCommandPhase::RotateItems)) {
            optionInput_->ConfirmInput();
        } else {
            ApplyArrayKeyword(QString());
        }
        event->accept();
        return;
    }

    if (event->button() == Qt::RightButton && IsModifyTool(toolMode_)) {
        SetToolMode(ToolMode::Select);
        event->accept();
        return;
    }

    if (event->button() == Qt::MiddleButton
        || (event->button() == Qt::RightButton
            && !IsDrawTool(toolMode_))) {
        panning_ = true;
        lastPanPoint_ = event->pos();
        ClearSnapMarker();
        setCursor(Qt::ClosedHandCursor);
        UpdateCursorOverlay(event->pos());
        event->accept();
        return;
    }

    if (event->button() == Qt::RightButton && IsDrawTool(toolMode_)) {
        if (IsPolylineOptionEditing()) {
            if (optionInput_->HasInput()) {
                optionInput_->ConfirmInput();
            } else {
                optionInput_->CancelKeywordEdit();
            }
            event->accept();
            return;
        }
        FinishDrawCommand();
        event->accept();
        return;
    }

    if (event->button() == Qt::LeftButton) {
        if (optionInput_ && optionInput_->isVisible()
            && optionInput_->ContainsGlobalPos(event->globalPosition().toPoint())) {
            return;
        }
        if (IsPolylineOptionEditing()) {
            EmitStatus(QStringLiteral("请先确认或取消当前选项"));
            if (optionInput_) {
                optionInput_->FocusKeywordEdit();
            }
            event->accept();
            return;
        }
        boxSelectStartView_ = event->pos();
        lastCursorViewportPos_ = event->pos();
        const GeoPoint rawPoint = MapToModel(event->pos());
        if (toolMode_ == ToolMode::Select && pasteTextPending_) {
            FinishPasteTextAt(ResolveInputPoint(rawPoint));
            event->accept();
            return;
        }
        if (toolMode_ == ToolMode::Select) {
            if (gripHoverMenu_ && gripHoverMenu_->isVisible()
                && gripHoverMenu_->ContainsGlobalPos(event->globalPosition().toPoint())) {
                event->accept();
                return;
            }
            HandleSelectPress(rawPoint, event->modifiers());
        } else {
            // AutoCAD-like: radius/diameter/angular first pick selects an object by
            // pickbox location. Do not pre-resolve osnap (CEN/END) or the curve
            // pick will miss the entity.
            const bool ttrObjectPick = IsCircleTangentObjectPick();
            const bool dimObjectPick =
                ((toolMode_ == ToolMode::DrawDimRadius
                     || toolMode_ == ToolMode::DrawDimJoggedRadius
                     || toolMode_ == ToolMode::DrawDimDiameter)
                    && inputPoints_.empty())
                || (toolMode_ == ToolMode::DrawDimAngular
                    && !dimAngularPlacementReady_
                    && !dimAngularThreePointMode_
                    && !dimAngularAwaitCircleArm2_);
            GeoPoint pickPoint = dimObjectPick
                ? rawPoint
                : (ttrObjectPick ? ResolveTtrObjectPickPoint(rawPoint) : ResolveInputPoint(rawPoint));
            GeoPoint polarBase;
            if (!dimObjectPick && !ttrObjectPick && TryGetPolarBasePoint(&polarBase)) {
                const bool hasObjectSnap = FindObjectSnapHit(rawPoint).has_value();
                const bool dynLocked = polarInput_
                    && polarInput_->isVisible()
                    && (polarInput_->IsLengthLocked() || polarInput_->IsAngleLocked());
                if (!hasObjectSnap || dynLocked) {
                    pickPoint = ResolveDynamicLineEndpoint(polarBase, pickPoint);
                }
            }
            HandleLeftPress(pickPoint, event->modifiers());
            if (SupportsPolarTracking() && polarInput_) {
                polarInput_->ResetLocks();
            }
            if (!ShouldShowDynPointInput()) {
                HidePointInput();
            }
        }
        UpdateCursorOverlay(event->pos());
        event->accept();
        return;
    }

    QGraphicsView::mousePressEvent(event);
}

void CadView::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (IsPolylineOptionEditing()) {
        EmitStatus(QStringLiteral("请先确认或取消当前选项"));
        if (optionInput_) {
            optionInput_->FocusKeywordEdit();
        }
        event->accept();
        return;
    }

    if (event->button() == Qt::MiddleButton) {
        ZoomExtents();
        event->accept();
        return;
    }

    if (!document_ || event->button() != Qt::LeftButton || toolMode_ != ToolMode::Select) {
        QGraphicsView::mouseDoubleClickEvent(event);
        return;
    }

    boxSelecting_ = false;
    draggingSelection_ = false;
    dragMoved_ = false;
    ClearBoxSelectPreview();
    CancelDrag();

    const GeoPoint rawPoint = MapToModel(event->pos());
    const EntityId hit = ResolveTextEditHit(rawPoint);
    if (hit == 0) {
        QGraphicsView::mouseDoubleClickEvent(event);
        return;
    }

    const Entity* entity = document_->FindEntity(hit);
    if (!entity) {
        QGraphicsView::mouseDoubleClickEvent(event);
        return;
    }

    if (entity->Type() == EntityType::Text) {
        EditTextEntity(hit);
        event->accept();
        return;
    }
    if (entity->Type() == EntityType::MText) {
        EditMTextEntity(hit);
        event->accept();
        return;
    }
    if (entity->Type() == EntityType::BlockReference) {
        auto* ref = static_cast<BlockReferenceEntity*>(document_->FindEntity(hit));
        if (!ref) {
            QGraphicsView::mouseDoubleClickEvent(event);
            return;
        }
        document_->SelectOnly(hit);
        const BlockDefinition* def = document_->FindBlockDefinition(ref->BlockName());
        const bool hasAttrs = def && !def->attributeDefs.empty();
        if (hasAttrs || !ref->AttributeValues().empty()) {
            BlockAttributeEditDialog dialog(*ref, def, this);
            if (dialog.exec() == QDialog::Accepted) {
                ref->SetAttributeValues(dialog.Result());
                document_->NotifyEntitiesUpdated({hit});
                EmitStatus(QStringLiteral("已更新块属性"));
            }
        } else {
            // AutoCAD: double-click enters BEDIT isolated editor.
            BlockEditorDialog dialog(*document_, ref->BlockName(), this);
            dialog.exec();
            if (dialog.definitionChanged()) {
                EmitStatus(QStringLiteral("块定义已更新（BEDIT）"));
            }
        }
        event->accept();
        return;
    }

    QGraphicsView::mouseDoubleClickEvent(event);
}

void CadView::mouseMoveEvent(QMouseEvent* event)
{
    // Self-heal after a spurious leaveEvent during wheel zoom / scrollbar layout.
    cursorInsideView_ = true;
    EnsureViewportInputReady();

    if (panning_) {
        const QPoint delta = event->pos() - lastPanPoint_;
        lastPanPoint_ = event->pos();
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - delta.x());
        verticalScrollBar()->setValue(verticalScrollBar()->value() - delta.y());
        EnsureSceneCoversViewport();
        event->accept();
        return;
    }

    // AutoCAD: while typing a PLINE option keyword, freeze rubber-band / polar HUD.
    if (IsPolylineOptionEditing()) {
        KeepPolylineKeywordEditPointer(event->pos());
        event->accept();
        return;
    }

    const GeoPoint rawPoint = MapToModel(event->pos());
    const bool forDrag = draggingSelection_ || boxSelecting_;
    const bool dimDefPick = IsDimensionDefPointPickActive();
    const bool ttrObjectPick = IsCircleTangentObjectPick();

    GeoPoint modelPoint;
    if (ttrObjectPick) {
        ClearDimensionSnapPreview();
        if (const std::optional<ObjectSnapHit> objectSnap = FindObjectSnapHit(rawPoint)) {
            modelPoint = ResolveTtrObjectPickPoint(rawPoint);
            UpdateSnapMarker(objectSnap->point, true, objectSnap->kind);
        } else {
            modelPoint = rawPoint;
            UpdateSnapMarker(rawPoint, false, ObjectSnapKind::Unknown);
        }
        if (cadScene_) {
            cadScene_->SetHoveredEntity(HitTest(rawPoint));
        }
    } else if (dimDefPick) {
        UpdateDimensionSnapPreview(rawPoint);
        const std::optional<ObjectSnapHit> objectSnap = FindObjectSnapHit(rawPoint);
        if (objectSnap) {
            modelPoint = objectSnap->point;
            UpdateSnapMarker(objectSnap->point, true, objectSnap->kind);
        } else if (const std::optional<GeoPoint> dimSnap = DimensionSnapPointAt(rawPoint)) {
            modelPoint = *dimSnap;
            UpdateSnapMarker(*dimSnap, true, ObjectSnapKind::Endpoint);
        } else {
            modelPoint = ResolveInputPoint(rawPoint, forDrag);
            UpdateSnapMarker(
                modelPoint,
                IsGridSnapActive(forDrag),
                ObjectSnapKind::Unknown);
        }
    } else {
        ClearDimensionSnapPreview();
        const std::optional<ObjectSnapHit> objectSnap = FindObjectSnapHit(rawPoint);
        if (objectSnap) {
            modelPoint = objectSnap->point;
            UpdateSnapMarker(objectSnap->point, true, objectSnap->kind);
        } else {
            modelPoint = ResolveInputPoint(rawPoint, forDrag);
            UpdateSnapMarker(
                modelPoint,
                IsGridSnapActive(forDrag),
                ObjectSnapKind::Unknown);
        }
    }

    if (!ttrObjectPick && cadScene_
        && toolMode_ == ToolMode::DrawCircle
        && (drawOptions_.method == DrawMethod::CircleTanTanRadius
            || drawOptions_.method == DrawMethod::CircleTanTanTan)) {
        cadScene_->SetHoveredEntity(0);
    }

    if (boxSelecting_) {
        dragCurrentPoint_ = modelPoint;
        UpdateBoxSelectPreview(event->pos());
        UpdateCursorOverlay(event->pos());
        event->accept();
        return;
    }

    if (draggingSelection_ && cadScene_ && document_) {
        dragCurrentPoint_ = modelPoint;
        const double dx = dragCurrentPoint_.X() - dragStartPoint_.X();
        const double dy = dragCurrentPoint_.Y() - dragStartPoint_.Y();
        if (!Tolerance::IsZero(dx) || !Tolerance::IsZero(dy)) {
            dragMoved_ = true;
        }
        cadScene_->SetSelectionDragOffset(dx, dy);
        UpdateCursorOverlay(event->pos());
        event->accept();
        return;
    }

    if (splineGripEditing_) {
        const GeoPoint editPoint = ResolveDynamicLineEndpoint(gripStartPoint_, modelPoint);
        if (splineGripEditMode_ == SplineGripEditMode::Stretch && cadScene_) {
            for (const auto& target : gripDragGroup_) {
                cadScene_->UpdateGripPosition(target.first, target.second, editPoint);
            }
            if (gripDragGroup_.empty()) {
                cadScene_->UpdateGripPosition(gripEntityId_, gripIndex_, editPoint);
            }
        }
        UpdateSplineGripEditPreview(editPoint);
        UpdateSplineGripEditHud(event->pos(), editPoint);
        UpdateCursorOverlay(event->pos());
        event->accept();
        return;
    }

    if (draggingGrip_ && cadScene_) {
        for (const auto& target : gripDragGroup_) {
            cadScene_->UpdateGripPosition(target.first, target.second, modelPoint);
        }
        if (gripDragGroup_.empty()) {
            cadScene_->UpdateGripPosition(gripEntityId_, gripIndex_, modelPoint);
        }
        UpdateCursorOverlay(event->pos());
        event->accept();
        return;
    }

    if (ShouldShowDynPointInput()) {
        UpdatePointInput(event->pos(), modelPoint);
        HidePolarInput();
    } else {
        HidePointInput();
        GeoPoint polarBase;
        if (TryGetPolarBasePoint(&polarBase)) {
            const bool hasObjectSnap = FindObjectSnapHit(rawPoint).has_value();
            const bool dynLocked = polarInput_
                && polarInput_->isVisible()
                && (polarInput_->IsLengthLocked() || polarInput_->IsAngleLocked());
            // AutoCAD: object snap wins over polar tracking when length/angle are not locked.
            if (!hasObjectSnap || dynLocked) {
                modelPoint = ResolveDynamicLineEndpoint(polarBase, modelPoint);
            }
            UpdatePolarInput(event->pos(), modelPoint);
        } else if (IsCircleTangentObjectPick() && !inputPoints_.empty()) {
            HidePolarInput();
            ShowDynPrompt(
                inputPoints_.size() == 1
                    ? QStringLiteral("指定圆的第二条切线:")
                    : QStringLiteral("指定圆的第三条切线:"),
                event->pos());
        } else if (toolMode_ == ToolMode::Select
            && !boxSelecting_ && !draggingSelection_ && !draggingGrip_
            && !splineGripEditing_) {
            UpdateSplineGripHover(event->pos(), rawPoint);
            UpdateTextHover(rawPoint);
            HidePolarInput();
        } else {
            HidePolarInput();
        }
    }

    UpdatePreview(modelPoint);
    UpdateCursorOverlay(event->pos());
    QGraphicsView::mouseMoveEvent(event);
}

void CadView::mouseReleaseEvent(QMouseEvent* event)
{
    if (panning_ && (event->button() == Qt::MiddleButton || event->button() == Qt::RightButton)) {
        panning_ = false;
        UpdateCursorOverlay(event->pos());
        InvalidateGridBackground();
        event->accept();
        return;
    }

    if (event->button() == Qt::LeftButton && (draggingGrip_ || splineGripEditing_)) {
        HandleLeftRelease(ApplyOrtho(ApplySnap(MapToModel(event->pos()), true)));
        event->accept();
        return;
    }

    if (event->button() == Qt::LeftButton && boxSelecting_) {
        FinalizeBoxSelection(event->pos(), event->modifiers());
        UpdateCursorOverlay(event->pos());
        event->accept();
        return;
    }

    if (event->button() == Qt::LeftButton && draggingSelection_) {
        HandleLeftRelease(ApplyOrtho(ApplySnap(MapToModel(event->pos()), true)));
        UpdateCursorOverlay(event->pos());
        event->accept();
        return;
    }

    QGraphicsView::mouseReleaseEvent(event);
}

void CadView::leaveEvent(QEvent* event)
{
    if (ShouldSuppressViewportLeave()) {
        cursorInsideView_ = true;
        EnsureViewportInputReady();
        QGraphicsView::leaveEvent(event);
        return;
    }

    ClearTextHover();
    ClearDimensionSnapPreview();
    crosshairPosValid_ = false;
    cursorInsideView_ = false;
    cursorDisplay_ = CadCursorDisplay::Hidden;
    unsetCursor();
    if (viewport()) {
        viewport()->unsetCursor();
        RefreshViewport();
    }
    QGraphicsView::leaveEvent(event);
}

void CadView::enterEvent(QEnterEvent* event)
{
    QGraphicsView::enterEvent(event);
    cursorInsideView_ = true;
    const QPoint viewPos = mapFromGlobal(QCursor::pos());
    if (viewport() && viewport()->rect().contains(viewport()->mapFromParent(viewPos))) {
        UpdateCursorOverlay(viewport()->mapFromParent(viewPos));
    }
}

bool CadView::IsAwaitingDimLinearPlacement() const
{
    return toolMode_ == ToolMode::DrawDimLinear && inputPoints_.size() == 2;
}

bool CadView::IsAwaitingDimAlignedPlacement() const
{
    return toolMode_ == ToolMode::DrawDimAligned && inputPoints_.size() == 2;
}

bool CadView::IsAwaitingDimRadiusPlacement() const
{
    return toolMode_ == ToolMode::DrawDimRadius && inputPoints_.size() == 1;
}

bool CadView::IsAwaitingDimJoggedRadiusPlacement() const
{
    return toolMode_ == ToolMode::DrawDimJoggedRadius
        && inputPoints_.size() >= 1
        && inputPoints_.size() <= 3;
}

bool CadView::IsAwaitingDimDiameterPlacement() const
{
    return toolMode_ == ToolMode::DrawDimDiameter && inputPoints_.size() == 1;
}

bool CadView::IsAwaitingDimAngularPlacement() const
{
    return toolMode_ == ToolMode::DrawDimAngular && dimAngularPlacementReady_;
}

bool CadView::IsAwaitingDimOrdinatePlacement() const
{
    return toolMode_ == ToolMode::DrawDimOrdinate && inputPoints_.size() == 1;
}

bool CadView::IsAwaitingDimPlacement() const
{
    return IsAwaitingDimLinearPlacement()
        || IsAwaitingDimAlignedPlacement()
        || IsAwaitingDimRadiusPlacement()
        || IsAwaitingDimJoggedRadiusPlacement()
        || IsAwaitingDimDiameterPlacement()
        || IsAwaitingDimAngularPlacement()
        || IsAwaitingDimOrdinatePlacement();
}

DimensionStyle CadView::ActiveDimensionStyle() const
{
    if (!document_) {
        return DimensionStyle{};
    }
    return ResolveDimensionStyleByName(document_, document_->CurrentDimensionStyleName());
}

void CadView::ApplyDimensionStyleName(DimensionEntity& entity) const
{
    if (document_) {
        entity.SetDimensionStyleName(document_->CurrentDimensionStyleName());
    }
}

bool CadView::TryBeginArcDimensionPick(const GeoPoint& point)
{
    if (!document_) {
        return false;
    }

    // Object selection uses the live cursor (and pickbox), matching AutoCAD DIMRADIUS /
    // DIMDIAMETER. Applying osnap first (especially CEN or a nearby END) moves the
    // sample off the curve so PickCircleOrArcAt fails.
    const GeoPoint rawCursor = MapToModel(
        viewport() ? viewport()->mapTo(this, lastCursorViewportPos_) : lastCursorViewportPos_);
    const double tolerance = ModelToleranceFromPixels(snapTolerancePixels_);
    const double centerTolerance = ModelToleranceFromPixels(std::max(2.0, snapTolerancePixels_));
    const QRectF visibleModelRect = VisibleModelRect(snapTolerancePixels_);

    std::optional<CircleArcPick> pick = PickCircleOrArcAt(
        *document_, rawCursor, tolerance, visibleModelRect);

    if (!pick) {
        pick = PickCircleOrArcAt(*document_, point, tolerance, visibleModelRect);
    }

    if (!pick) {
        if (const std::optional<ObjectSnapHit> hit = FindObjectSnapHit(rawCursor)) {
            if (hit->kind == ObjectSnapKind::Center) {
                pick = PickCircleOrArcByCenter(*document_, hit->point, centerTolerance);
            } else if (hit->kind == ObjectSnapKind::Quadrant
                || hit->kind == ObjectSnapKind::Nearest) {
                pick = PickCircleOrArcAt(*document_, hit->point, tolerance, visibleModelRect);
                if (!pick) {
                    pick = PickCircleOrArcByCenter(*document_, hit->point, centerTolerance);
                }
            }
        }
    }

    if (!pick || pick->radius <= Tolerance::Model) {
        return false;
    }

    dimRadiusValue_ = pick->radius;
    dimRadiusHasArcSpan_ = pick->hasArcSpan;
    dimRadiusArcStartRadians_ = pick->arcStartRadians;
    dimRadiusArcEndRadians_ = pick->arcEndRadians;
    inputPoints_.push_back(pick->center);
    dragCurrentPoint_ = pick->center;
    setFocus(Qt::MouseFocusReason);
    return true;
}

bool CadView::IsDimJoggedRadiusOptionKey(const int key) const
{
    return key == Qt::Key_M || key == Qt::Key_T || key == Qt::Key_A;
}

bool CadView::IsDimDiameterOptionKey(const int key) const
{
    return key == Qt::Key_M || key == Qt::Key_T || key == Qt::Key_A;
}

bool CadView::IsDimOrdinateOptionKey(const int key) const
{
    return key == Qt::Key_M || key == Qt::Key_T || key == Qt::Key_A
        || key == Qt::Key_X || key == Qt::Key_Y;
}

bool CadView::IsDimAlignedOptionKey(const int key) const
{
    return key == Qt::Key_M || key == Qt::Key_T || key == Qt::Key_A;
}

bool CadView::IsDimRadiusOptionKey(const int key) const
{
    return key == Qt::Key_M || key == Qt::Key_T || key == Qt::Key_A;
}

bool CadView::IsDimAngularOptionKey(const int key) const
{
    return key == Qt::Key_M || key == Qt::Key_T || key == Qt::Key_A || key == Qt::Key_Q;
}

bool CadView::IsDimOptionKey(const int key) const
{
    if (IsAwaitingDimLinearPlacement()) {
        return IsDimLinearOptionKey(key);
    }
    if (IsAwaitingDimAlignedPlacement()) {
        return IsDimAlignedOptionKey(key);
    }
    if (IsAwaitingDimRadiusPlacement()) {
        return IsDimRadiusOptionKey(key);
    }
    if (IsAwaitingDimJoggedRadiusPlacement() && inputPoints_.size() == 2) {
        return IsDimJoggedRadiusOptionKey(key);
    }
    if (IsAwaitingDimDiameterPlacement()) {
        return IsDimDiameterOptionKey(key);
    }
    if (IsAwaitingDimAngularPlacement()) {
        return IsDimAngularOptionKey(key);
    }
    if (IsAwaitingDimOrdinatePlacement()) {
        return IsDimOrdinateOptionKey(key);
    }
    return false;
}

void CadView::HandleDimAlignedOptionKey(const int key)
{
    if (key == Qt::Key_M) {
        PromptDimAlignedTextEdit(true);
    } else if (key == Qt::Key_T) {
        PromptDimAlignedTextEdit(false);
    } else if (key == Qt::Key_A) {
        PromptDimAlignedTextAngle();
    }
}

void CadView::HandleDimRadiusOptionKey(const int key)
{
    if (key == Qt::Key_M) {
        PromptDimRadiusTextEdit(true);
    } else if (key == Qt::Key_T) {
        PromptDimRadiusTextEdit(false);
    } else if (key == Qt::Key_A) {
        PromptDimRadiusTextAngle();
    }
}

void CadView::HandleDimJoggedRadiusOptionKey(const int key)
{
    if (key == Qt::Key_M) {
        PromptDimJoggedRadiusTextEdit(true);
    } else if (key == Qt::Key_T) {
        PromptDimJoggedRadiusTextEdit(false);
    } else if (key == Qt::Key_A) {
        PromptDimJoggedRadiusTextAngle();
    }
}

void CadView::HandleDimDiameterOptionKey(const int key)
{
    if (key == Qt::Key_M) {
        PromptDimDiameterTextEdit(true);
    } else if (key == Qt::Key_T) {
        PromptDimDiameterTextEdit(false);
    } else if (key == Qt::Key_A) {
        PromptDimDiameterTextAngle();
    }
}

void CadView::HandleDimOrdinateOptionKey(const int key)
{
    if (key == Qt::Key_M) {
        PromptDimOrdinateTextEdit(true);
    } else if (key == Qt::Key_T) {
        PromptDimOrdinateTextEdit(false);
    } else if (key == Qt::Key_A) {
        PromptDimOrdinateTextAngle();
    } else if (key == Qt::Key_X) {
        dimOrdinateOrientationOverride_ = LinearDimensionOrientation::Vertical;
        EmitStatus(QStringLiteral("坐标标注：已强制 X 坐标标注；指定引线位置"));
        UpdatePreview(dragCurrentPoint_);
    } else if (key == Qt::Key_Y) {
        dimOrdinateOrientationOverride_ = LinearDimensionOrientation::Horizontal;
        EmitStatus(QStringLiteral("坐标标注：已强制 Y 坐标标注；指定引线位置"));
        UpdatePreview(dragCurrentPoint_);
    }
}

void CadView::HandleDimAngularOptionKey(const int key)
{
    if (key == Qt::Key_M) {
        PromptDimAngularTextEdit(true);
    } else if (key == Qt::Key_T) {
        PromptDimAngularTextEdit(false);
    } else if (key == Qt::Key_A) {
        PromptDimAngularTextAngle();
    } else if (key == Qt::Key_Q) {
        CycleDimAngularQuadrantLock();
    }
}

void CadView::HandleDimOptionKey(const int key)
{
    if (IsAwaitingDimLinearPlacement()) {
        HandleDimLinearOptionKey(key);
    } else if (IsAwaitingDimAlignedPlacement()) {
        HandleDimAlignedOptionKey(key);
    } else if (IsAwaitingDimRadiusPlacement()) {
        HandleDimRadiusOptionKey(key);
    } else if (IsAwaitingDimJoggedRadiusPlacement() && inputPoints_.size() == 2) {
        HandleDimJoggedRadiusOptionKey(key);
    } else if (IsAwaitingDimDiameterPlacement()) {
        HandleDimDiameterOptionKey(key);
    } else if (IsAwaitingDimAngularPlacement()) {
        HandleDimAngularOptionKey(key);
    } else if (IsAwaitingDimOrdinatePlacement()) {
        HandleDimOrdinateOptionKey(key);
    }
}

bool CadView::IsDimLinearOptionKey(const int key) const
{
    return key == Qt::Key_M || key == Qt::Key_T || key == Qt::Key_A || key == Qt::Key_R;
}

void CadView::HandleDimLinearOptionKey(const int key)
{
    if (key == Qt::Key_M) {
        PromptDimLinearTextEdit(true);
    } else if (key == Qt::Key_T) {
        PromptDimLinearTextEdit(false);
    } else if (key == Qt::Key_A) {
        PromptDimLinearTextAngle();
    } else if (key == Qt::Key_R) {
        ToggleDimLinearOrientation();
    }
}

bool CadView::event(QEvent* event)
{
    if (event->type() == QEvent::ShortcutOverride) {
        const int key = static_cast<QKeyEvent*>(event)->key();
        if (IsAwaitingDimPlacement() && IsDimOptionKey(key)) {
            event->accept();
            return true;
        }
        if (key == Qt::Key_Escape && toolMode_ != ToolMode::Select) {
            event->accept();
            return true;
        }
        if (IsCommandOptionHudActive()
            && static_cast<QKeyEvent*>(event)->modifiers() == Qt::NoModifier) {
            if (key == Qt::Key_Down || key == Qt::Key_Up
                || (key >= Qt::Key_A && key <= Qt::Key_Z)
                || (IsArrayCommand() && ((key >= Qt::Key_0 && key <= Qt::Key_9)
                    || key == Qt::Key_Period || key == Qt::Key_Minus
                    || key == Qt::Key_Space || key == Qt::Key_Return || key == Qt::Key_Enter))) {
                event->accept();
                return true;
            }
        }
        if (IsContinuousLineDraw() && (IsDrawInProgress() || lineChainPoints_.size() >= 2)
            && static_cast<QKeyEvent*>(event)->modifiers() == Qt::NoModifier
            && (key == Qt::Key_U || key == Qt::Key_C)) {
            event->accept();
            return true;
        }
        // Dynamic polar / point input: claim Tab while HUD is active.
        if (((SupportsPolarTracking() && polarInput_ && polarInput_->isVisible())
                || (ShouldShowDynPointInput() && pointInput_ && pointInput_->isVisible()))
            && (key == Qt::Key_Tab || key == Qt::Key_Backtab)) {
            event->accept();
            return true;
        }
    }
    return QGraphicsView::event(event);
}

void CadView::keyPressEvent(QKeyEvent* event)
{
    // Handle LINE U/C first, before any other tool shortcuts/handlers.
    if (IsContinuousLineDraw()
        && event->modifiers() == Qt::NoModifier
        && (IsDrawInProgress() || lineChainPoints_.size() >= 2)) {
        if (event->key() == Qt::Key_U) {
            UndoLineSegment();
            event->accept();
            return;
        }
        if (event->key() == Qt::Key_C) {
            CloseLineChain();
            event->accept();
            return;
        }
    }

    if (IsSplineGripEditing()
        && (event->key() == Qt::Key_Control || event->key() == Qt::Key_Meta)
        && !event->isAutoRepeat()) {
        CycleSplineGripEditMode();
        event->accept();
        return;
    }

    if (IsCommandOptionHudActive()
        && event->modifiers() == Qt::NoModifier) {
        if (IsSplineGripEditing()
            && (event->key() == Qt::Key_Control || event->key() == Qt::Key_Meta)
            && !event->isAutoRepeat()) {
            CycleSplineGripEditMode();
            event->accept();
            return;
        }
        if (IsPolygonCommand()) {
            if (event->key() == Qt::Key_Down || event->key() == Qt::Key_Up) {
                EnsureOptionInput();
                UpdatePolygonHud();
                optionInput_->ShowOptionMenu();
                event->accept();
                return;
            }
            if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter)
                && polygonPhase_ == PolygonCommandPhase::Option) {
                ApplyPolygonKeyword(QString());
                event->accept();
                return;
            }
            QString text = event->text();
            if (text.isEmpty()
                && event->key() >= Qt::Key_A && event->key() <= Qt::Key_Z) {
                text = QChar(QLatin1Char(static_cast<char>('a' + (event->key() - Qt::Key_A))));
            }
            if (!text.isEmpty()) {
                BeginPolylineKeywordTyping(text);
                event->accept();
                return;
            }
        }
        if (IsFilletChamferCommand()) {
            if (event->key() == Qt::Key_Down || event->key() == Qt::Key_Up) {
                EnsureOptionInput();
                UpdateFilletChamferHud();
                optionInput_->ShowOptionMenu();
                event->accept();
                return;
            }
            if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
                if (IsFilletChamferNumericPhase()) {
                    if (optionInput_ && optionInput_->IsKeywordEditing()) {
                        optionInput_->ConfirmInput();
                    } else {
                        ApplyFilletChamferKeyword(QString());
                    }
                    event->accept();
                    return;
                }
            }
            QString text = event->text();
            if (text.isEmpty()
                && event->key() >= Qt::Key_A && event->key() <= Qt::Key_Z) {
                text = QChar(QLatin1Char(static_cast<char>('a' + (event->key() - Qt::Key_A))));
            }
            if (!text.isEmpty()) {
                BeginPolylineKeywordTyping(text);
                event->accept();
                return;
            }
        }
        if (IsArrayCommand()) {
            if (event->key() == Qt::Key_Down || event->key() == Qt::Key_Up) {
                EnsureOptionInput();
                UpdateArrayHud();
                optionInput_->ShowOptionMenu();
                event->accept();
                return;
            }
            if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter
                || event->key() == Qt::Key_Space) {
                if (arrayPhase_ == ArrayCommandPhase::Edit) {
                    CommitPolarArray();
                } else if (IsArrayNumericPhase()
                    || arrayPhase_ == ArrayCommandPhase::RotateItems
                    || arrayPhase_ == ArrayCommandPhase::BasePoint) {
                    if (optionInput_ && optionInput_->IsKeywordEditing()) {
                        optionInput_->ConfirmInput();
                    } else {
                        ApplyArrayKeyword(QString());
                    }
                } else {
                    EmitStatus(ArrayPromptText());
                    UpdateArrayHud();
                }
                event->accept();
                return;
            }
            QString text = event->text();
            if (text.isEmpty()
                && event->key() >= Qt::Key_A && event->key() <= Qt::Key_Z) {
                text = QChar(QLatin1Char(static_cast<char>('a' + (event->key() - Qt::Key_A))));
            }
            if (!text.isEmpty()) {
                const QChar ch = text.at(0);
                const bool numericPhase = IsArrayNumericPhase()
                    || arrayPhase_ == ArrayCommandPhase::RotateItems;
                const bool numberChar = ch.isDigit() || ch == QLatin1Char('.') || ch == QLatin1Char('-');
                if (ch.isLetter() || (numericPhase && numberChar)) {
                    BeginPolylineKeywordTyping(text);
                    event->accept();
                    return;
                }
            }
        }
        if (IsPolylineOptionEditing()) {
            if (event->key() == Qt::Key_Down || event->key() == Qt::Key_Up) {
                optionInput_->ShowOptionMenu();
                event->accept();
                return;
            }
            const QString text = event->text();
            if (!text.isEmpty()) {
                BeginPolylineKeywordTyping(text);
                event->accept();
                return;
            }
        }
        if (event->key() == Qt::Key_Down || event->key() == Qt::Key_Up) {
            EnsureOptionInput();
            if (IsSplineDraw()) {
                UpdateSplineOptionInput(lastCursorViewportPos_);
            } else {
                UpdatePolylineOptionInput(lastCursorViewportPos_);
            }
            optionInput_->ShowOptionMenu();
            event->accept();
            return;
        }
        const QString text = event->text();
        if (!text.isEmpty() && text.at(0).isLetter()) {
            BeginPolylineKeywordTyping(text);
            event->accept();
            return;
        }
    }

    // Absolute X/Y dyninput (first point / insert point).
    if (ShouldShowDynPointInput() && event->modifiers() == Qt::NoModifier
        && pointInput_ && pointInput_->isVisible()) {
        if (event->key() == Qt::Key_Tab || event->key() == Qt::Key_Backtab) {
            pointInput_->ToggleFocus();
            event->accept();
            return;
        }
        const QString text = event->text();
        if (!text.isEmpty()) {
            const QChar ch = text.at(0);
            if (ch.isDigit() || ch == QLatin1Char('.') || ch == QLatin1Char('-')) {
                EnsurePointInput();
                pointInput_->BeginTyping(text);
                event->accept();
                return;
            }
        }
    }

    // Dynamic polar input (LINE / polyline / arc / circle, etc.).
    if (SupportsPolarTracking() && event->modifiers() == Qt::NoModifier
        && polarInput_ && polarInput_->isVisible()) {
        if (event->key() == Qt::Key_Tab || event->key() == Qt::Key_Backtab) {
            polarInput_->ToggleFocus();
            event->accept();
            return;
        }
        // Typing a number focuses the length field (AutoCAD DYNINPUT).
        const QString text = event->text();
        if (!text.isEmpty()) {
            const QChar ch = text.at(0);
            if (ch.isDigit() || ch == QLatin1Char('.') || ch == QLatin1Char('-')) {
                EnsurePolarInput();
                polarInput_->BeginTyping(text);
                event->accept();
                return;
            }
        }
    }

    if (IsAwaitingDimPlacement() && IsDimOptionKey(event->key())) {
        HandleDimOptionKey(event->key());
        event->accept();
        return;
    }

    if (toolMode_ == ToolMode::Mirror && mirrorPhase_ == MirrorPhase::ConfirmEraseSource) {
        if (event->key() == Qt::Key_Y) {
            ConfirmMirror(true);
            event->accept();
            return;
        }
        if (event->key() == Qt::Key_N || event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
            ConfirmMirror(false);
            event->accept();
            return;
        }
    }

    if (toolMode_ == ToolMode::Mirror && mirrorPhase_ == MirrorPhase::SelectObjects) {
        if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
            FinishMirrorSelection();
            event->accept();
            return;
        }
    }

    if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter)
        && toolMode_ == ToolMode::Break
        && editHelperEntityId_ != 0
        && !inputPoints_.empty()
        && commandManager_) {
        const bool ok = commandManager_->BreakSelected(editHelperEntityId_, inputPoints_.front(), std::nullopt);
        EmitStatus(ok ? QStringLiteral("打断完成") : QStringLiteral("打断失败：点无效或在端点上"));
        editHelperEntityId_ = 0;
        ResetTransientState();
        event->accept();
        return;
    }

    if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter)
        && toolMode_ == ToolMode::DrawDimAngular
        && !dimAngularPlacementReady_
        && !dimAngularThreePointMode_
        && inputPoints_.empty()
        && dimAngularLine1Id_ == 0
        && !dimAngularAwaitCircleArm2_) {
        dimAngularThreePointMode_ = true;
        EmitStatus(QStringLiteral("三点角度标注：指定角的顶点"));
        event->accept();
        return;
    }

    if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter || event->key() == Qt::Key_Space)
        && IsDrawTool(toolMode_)) {
        if (IsPolylineOptionEditing()) {
            optionInput_->ConfirmInput();
            event->accept();
            return;
        }
        if (IsContinuousPolylineDraw() && inputPoints_.empty() && lastArcContinue_.valid) {
            inputPoints_.push_back(lastArcContinue_.start);
            EmitStatus(PolylineOptionPrompt());
            if (viewport()) {
                UpdatePreview(ApplySnap(MapToModel(viewport()->mapFromGlobal(QCursor::pos()))));
            }
            event->accept();
            return;
        }
        FinishDrawCommand();
        event->accept();
        return;
    }

    if (toolMode_ == ToolMode::Select && mirrorPhase_ == MirrorPhase::Inactive) {
        if (event->matches(QKeySequence::Copy)) {
            CopySelectedTextEntities();
            event->accept();
            return;
        }
        if (event->matches(QKeySequence::Paste)) {
            BeginPasteText();
            event->accept();
            return;
        }
    }

    if (event->key() == Qt::Key_Escape) {
        if (CancelActiveCommand()) {
            event->accept();
            return;
        }
        if (pasteTextPending_ || !textClipboard_.empty()) {
            EndTextPasteSession(true);
            RefreshViewport();
            event->accept();
            return;
        }
        if (IsPolylineOptionEditing()) {
            optionInput_->CancelKeywordEdit();
            event->accept();
            return;
        }
        if (toolMode_ == ToolMode::Mirror && mirrorPhase_ != MirrorPhase::Inactive) {
            ResetMirrorState();
            ClearPreview();
            EmitStatus(QStringLiteral("镜像命令已取消"));
            RefreshViewport();
            event->accept();
            return;
        }
        if (IsDrawTool(toolMode_)) {
            if (drawSegmentFinished_) {
                SetToolMode(ToolMode::Select);
                if (document_) {
                    document_->ClearSelection();
                }
            } else if (IsDrawInProgress()) {
                ResetTransientState();
                EmitStatus(QStringLiteral("已取消当前绘制"));
            }
            RefreshViewport();
            event->accept();
            return;
        }
        if (toolMode_ == ToolMode::DrawText || toolMode_ == ToolMode::DrawMText
            || toolMode_ == ToolMode::DrawDimLinear || toolMode_ == ToolMode::DrawDimAligned
            || toolMode_ == ToolMode::DrawDimRadius || toolMode_ == ToolMode::DrawDimJoggedRadius
            || toolMode_ == ToolMode::DrawDimDiameter || toolMode_ == ToolMode::DrawDimAngular
            || toolMode_ == ToolMode::DrawDimOrdinate) {
            ResetDimLinearOptions();
            ResetDimAlignedOptions();
            ResetDimRadiusOptions();
            ResetDimAngularOptions();
            ResetDimOrdinateOptions();
            ResetTransientState();
            SetToolMode(ToolMode::Select);
            RefreshViewport();
            event->accept();
            return;
        }
        ResetInteractionState();
        ResetTransientState();
        if (document_) {
            document_->ClearSelection();
        }
        RefreshViewport();
        event->accept();
        return;
    }

    if ((event->key() == Qt::Key_Control || event->key() == Qt::Key_Meta)
        && IsDrawTool(toolMode_) && IsDrawInProgress()) {
        if (IsArcIncludedAnglePick() || IsArcStartEndAnglePick()
            || IsArcStartEndRadiusPick() || IsArcChordLengthPick()
            || IsArcStartCenterEndEndPick()) {
            if (viewport()) {
                UpdatePreview(ApplySnap(MapToModel(
                    viewport()->mapFromGlobal(QCursor::pos()))));
                RefreshViewport();
            }
        }
    }

    QGraphicsView::keyPressEvent(event);
}

void CadView::keyReleaseEvent(QKeyEvent* event)
{
    if ((event->key() == Qt::Key_Control || event->key() == Qt::Key_Meta)
        && IsDrawTool(toolMode_) && IsDrawInProgress()) {
        if (IsArcIncludedAnglePick() || IsArcStartEndAnglePick()
            || IsArcStartEndRadiusPick() || IsArcChordLengthPick()
            || IsArcStartCenterEndEndPick()) {
            if (viewport()) {
                UpdatePreview(ApplySnap(MapToModel(
                    viewport()->mapFromGlobal(QCursor::pos()))));
                RefreshViewport();
            }
        }
    }
    QGraphicsView::keyReleaseEvent(event);
}

GeoPoint CadView::MapToModel(const QPoint& viewPoint) const
{
    const QPointF scenePoint = mapToScene(viewPoint);
    return GeoPoint(scenePoint.x(), -scenePoint.y());
}

QPointF CadView::ToScenePoint(const GeoPoint& point) const
{
    return QPointF(point.X(), -point.Y());
}

double CadView::ViewScale() const
{
    return std::max(std::abs(transform().m11()), Tolerance::MinZoom);
}

double CadView::ModelToleranceFromPixels(double pixels) const
{
    return Tolerance::PixelsToModel(pixels, ViewScale());
}

QRectF CadView::VisibleModelRect(double marginPixels) const
{
    const QRectF sceneVisible = mapToScene(viewport()->rect()).boundingRect();
    QRectF modelVisible(
        sceneVisible.left(),
        -sceneVisible.bottom(),
        sceneVisible.width(),
        sceneVisible.height());
    const double margin = ModelToleranceFromPixels(marginPixels);
    return modelVisible.normalized().adjusted(-margin, -margin, margin, margin);
}

void CadView::EnsureSceneCoversViewport()
{
    if (!cadScene_) {
        return;
    }
    const QRectF visibleScene = mapToScene(viewport()->rect()).boundingRect().adjusted(-500.0, -500.0, 500.0, 500.0);
    if (!cadScene_->sceneRect().contains(visibleScene)) {
        cadScene_->setSceneRect(cadScene_->sceneRect().united(visibleScene));
        cadScene_->update();
    }
}

QRectF CadView::ComputeExtentsRect() const
{
    if (!document_) {
        return QRectF();
    }
    bool hasBounds = false;
    double minX = 0.0, minY = 0.0, maxX = 0.0, maxY = 0.0;
    auto includePoint = [&](double x, double y) {
        if (!hasBounds) {
            minX = maxX = x;
            minY = maxY = y;
            hasBounds = true;
        } else {
            minX = std::min(minX, x);
            minY = std::min(minY, y);
            maxX = std::max(maxX, x);
            maxY = std::max(maxY, y);
        }
    };

    for (const std::unique_ptr<Entity>& entity : document_->Entities()) {
        const GeoShape shape = entity->Shaper();
        const GeoBounds bounds = shape.Bounds();
        if (bounds.valid) {
            includePoint(bounds.minX, bounds.minY);
            includePoint(bounds.maxX, bounds.maxY);
        } else {
            for (const GeoPoint& p : shape.Points()) {
                includePoint(p.X(), p.Y());
            }
        }
        for (const GripInfo& grip : entity->Grips()) {
            includePoint(grip.position.X(), grip.position.Y());
        }
    }

    if (!hasBounds) {
        return QRectF();
    }
    return QRectF(minX, minY, maxX - minX, maxY - minY).normalized();
}

void CadView::FitModelRect(const QRectF& modelRect, double marginFactor)
{
    if (modelRect.isNull() || !cadScene_) {
        return;
    }

    const QRectF sceneRect(modelRect.left(), -modelRect.bottom(),
                           modelRect.width(), modelRect.height());

    QRectF target = sceneRect;
    const double minExtent = Tolerance::PixelsToModel(2.0, ViewScale());
    if (target.width() < minExtent && target.height() < minExtent) {
        target = QRectF(target.center() - QPointF(minExtent, minExtent) / 2.0,
                        QSizeF(minExtent, minExtent));
    }

    const double margin = std::max(target.width(), target.height()) * marginFactor;
    target.adjust(-margin, -margin, margin, margin);

    cadScene_->setSceneRect(cadScene_->sceneRect().united(target));

    const QRectF currentScene = mapToScene(viewport()->rect()).boundingRect();
    if (currentScene.isNull() || currentScene.width() <= 0.0 || currentScene.height() <= 0.0) {
        return;
    }

    const double scaleX = currentScene.width() / target.width();
    const double scaleY = currentScene.height() / target.height();
    const double targetScale = std::clamp(std::min(scaleX, scaleY),
                                          Tolerance::MinZoom, Tolerance::MaxZoom);
    const double currentScale = ViewScale();
    if (Tolerance::IsEqual(currentScale, 0.0) || Tolerance::IsEqual(currentScale, targetScale)) {
        centerOn(target.center());
    } else {
        const double factor = targetScale / currentScale;
        setTransformationAnchor(QGraphicsView::NoAnchor);
        scale(factor, factor);
        centerOn(target.center());
        setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    }

    EnsureSceneCoversViewport();
    InvalidateGridBackground();
    UpdateCursorOverlay(lastCursorViewportPos_);
    RefreshDrawInteractionAfterViewChange(lastCursorViewportPos_);
}

void CadView::ZoomExtents()
{
    QRectF modelRect = ComputeExtentsRect();
    if (modelRect.isNull()) {
        ZoomAll();
        return;
    }
    FitModelRect(modelRect, 0.08);
}

void CadView::ZoomAll()
{
    if (!cadScene_) {
        return;
    }
    QRectF sceneRect = cadScene_->sceneRect();
    if (sceneRect.isNull()) {
        return;
    }
    QRectF modelRect(sceneRect.left(), -sceneRect.bottom(),
                     sceneRect.width(), sceneRect.height());
    FitModelRect(modelRect, 0.0);
}

void CadView::ZoomWindow(const GeoPoint& first, const GeoPoint& second)
{
    QRectF modelRect(QPointF(first.X(), first.Y()), QPointF(second.X(), second.Y()));
    modelRect = modelRect.normalized();
    if (modelRect.width() < Tolerance::Model && modelRect.height() < Tolerance::Model) {
        return;
    }
    FitModelRect(modelRect, 0.0);
}

void CadView::ZoomIn(double factor)
{
    if (factor <= 0.0) {
        factor = 2.0;
    }
    QPoint viewportPos = lastCursorViewportPos_;
    if (viewport() && !viewport()->rect().contains(viewportPos)) {
        viewportPos = viewport()->rect().center();
    }
    ZoomByFactorAtViewportPos(factor, viewportPos);
    EnsureSceneCoversViewport();
    InvalidateGridBackground();
    RestorePointerInteractionFromGlobalCursor();
}

void CadView::ZoomOut(double factor)
{
    if (factor <= 0.0) {
        factor = 2.0;
    }
    ZoomIn(1.0 / factor);
}

bool CadView::EntityIntersectsVisibleRect(const Entity& entity, const QRectF& visibleModelRect, double tolerance) const
{
    if (entity.Type() == EntityType::Ray || entity.Type() == EntityType::XLine) {
        const GeoBounds viewClip = ModelRectToBounds(visibleModelRect);
        return !entity.Shaper().ToSegments(viewClip).empty();
    }

    if (entity.Type() == EntityType::Text || entity.Type() == EntityType::MText) {
        TextLayoutOptions layoutOptions;
        layoutOptions.document = document_;
        layoutOptions.style = &entity.Style();
        const QRectF sceneBounds = TextLikeEntityBounds(entity, layoutOptions);
        if (!sceneBounds.isValid()) {
            return false;
        }
        const QRectF modelBounds(
            sceneBounds.left(),
            -sceneBounds.bottom(),
            sceneBounds.width(),
            sceneBounds.height());
        return modelBounds.normalized()
            .adjusted(-tolerance, -tolerance, tolerance, tolerance)
            .intersects(visibleModelRect);
    }

    if (entity.Type() == EntityType::Dimension) {
        const DimensionStyle dimStyle = ResolveDimensionStyleForEntity(
            document_,
            static_cast<const DimensionEntity&>(entity));
        const GeoBounds bounds = DimensionModelBounds(static_cast<const DimensionEntity&>(entity), dimStyle);
        if (!bounds.valid) {
            for (const GripInfo& grip : entity.Grips()) {
                const QRectF pointRect(grip.position.X(), grip.position.Y(), 1.0, 1.0);
                if (pointRect.normalized().adjusted(-tolerance, -tolerance, tolerance, tolerance).intersects(visibleModelRect)) {
                    return true;
                }
            }
            return false;
        }
        const QRectF entityRect(bounds.minX, bounds.minY, bounds.maxX - bounds.minX, bounds.maxY - bounds.minY);
        return entityRect.normalized().adjusted(-tolerance, -tolerance, tolerance, tolerance).intersects(visibleModelRect);
    }

    const GeoBounds bounds = entity.Shaper().Bounds();
    if (!bounds.valid) {
        for (const GripInfo& grip : entity.Grips()) {
            const QRectF pointRect(grip.position.X(), grip.position.Y(), 1.0, 1.0);
            if (pointRect.normalized().adjusted(-tolerance, -tolerance, tolerance, tolerance).intersects(visibleModelRect)) {
                return true;
            }
        }
        return true;
    }
    const QRectF entityRect(bounds.minX, bounds.minY, bounds.maxX - bounds.minX, bounds.maxY - bounds.minY);
    return entityRect.normalized().adjusted(-tolerance, -tolerance, tolerance, tolerance).intersects(visibleModelRect);
}

GeoPoint CadView::SnapToGrid(const GeoPoint& point) const
{
    return GeoPoint(
        std::round(point.X() / gridSize_) * gridSize_,
        std::round(point.Y() / gridSize_) * gridSize_);
}

bool CadView::IsGridSnapActive(bool forDrag) const
{
    return snapEnabled_ && objectSnapModes_.grid && !forDrag;
}

GeoPoint CadView::ApplySnap(const GeoPoint& point, bool forDrag) const
{
    if (const std::optional<GeoPoint> objectSnap = FindObjectSnapPoint(point)) {
        return *objectSnap;
    }

    if (IsGridSnapActive(forDrag)) {
        return SnapToGrid(point);
    }

    return point;
}

ObjectSnapModes CadView::EffectiveObjectSnapModes() const
{
    ObjectSnapModes modes = objectSnapModes_;
    // TTR：命令默认启用最近点，并同时使用「对象捕捉设置」中勾选的其它模式。
    if (toolMode_ == ToolMode::DrawCircle
        && (drawOptions_.method == DrawMethod::CircleTanTanRadius
            || drawOptions_.method == DrawMethod::CircleTanTanTan)) {
        modes.nearest = true;
        if (IsCircleTangentObjectPick()) {
            // 选择切线对象时，圆心/几何中心会落到对象内部，选不中切线。
            modes.center = false;
            modes.geometricCenter = false;
        }
    }
    return modes;
}

std::optional<GeoPoint> CadView::FindObjectSnapPoint(const GeoPoint& point) const
{
    if (const std::optional<ObjectSnapHit> hit = FindObjectSnapHit(point)) {
        return hit->point;
    }
    return std::nullopt;
}

std::optional<ObjectSnapHit> CadView::FindObjectSnapHit(const GeoPoint& point) const
{
    const ObjectSnapModes modes = EffectiveObjectSnapModes();
    if (!snapEnabled_ || !document_ || !modes.AnyEnabled()) {
        return std::nullopt;
    }

    const double tolerance = ModelToleranceFromPixels(
        std::max(snapAperturePixels_, snapTolerancePixels_));
    const QRectF visibleModelRect = VisibleModelRect(snapAperturePixels_);
    const GeoPoint* fromPoint = inputPoints_.empty() ? nullptr : &inputPoints_.back();
    const std::vector<GeoShape> transientShapes = BuildTransientSnapShapes();
    const std::vector<GeoShape>* extraShapes = transientShapes.empty() ? nullptr : &transientShapes;
    return FindBestObjectSnapPoint(
        *document_,
        point,
        modes,
        tolerance,
        visibleModelRect,
        fromPoint,
        extraShapes);
}

std::vector<GeoShape> CadView::BuildTransientSnapShapes() const
{
    std::vector<GeoShape> shapes;
    if (toolMode_ == ToolMode::DrawPolyline && inputPoints_.size() >= 1) {
        GeoShape polylineShape;
        DrawAlgorithms::AppendPolylineGeometry(
            polylineShape,
            inputPoints_,
            drawOptions_.polylineBulges,
            false);
        shapes.push_back(std::move(polylineShape));
    }
    return shapes;
}

void CadView::UpdateSnapMarker(const GeoPoint& point, bool visible, ObjectSnapKind kind)
{
    if (!scene() || !visible) {
        ClearSnapMarker();
        return;
    }

    // Snap markers are drawn in scene units sized from screen pixels so they stay
    // a constant on-screen size (AutoCAD Autosnap style).
    const double halfSize = ModelToleranceFromPixels(cursorSettings_.snapMarkerSizePixels * 0.5);
    const double xHalf = ModelToleranceFromPixels(cursorSettings_.snapMarkerSizePixels * 0.75);
    const QPointF center = ToScenePoint(point);
    QPainterPath markerPath;
    double penWidth = 1.6;

    switch (kind) {
    case ObjectSnapKind::Endpoint:
        // AutoCAD: hollow green square
        markerPath.addRect(center.x() - halfSize, center.y() - halfSize, halfSize * 2.0, halfSize * 2.0);
        break;
    case ObjectSnapKind::Midpoint:
        // AutoCAD: hollow yellow triangle
        markerPath.moveTo(center.x(), center.y() - halfSize);
        markerPath.lineTo(center.x() + halfSize, center.y() + halfSize);
        markerPath.lineTo(center.x() - halfSize, center.y() + halfSize);
        markerPath.closeSubpath();
        break;
    case ObjectSnapKind::Intersection:
    case ObjectSnapKind::ApparentIntersection:
    case ObjectSnapKind::Extension:
        // AutoCAD: X (extension uses the same glyph as intersection)
        penWidth = 2.0;
        markerPath.moveTo(center.x() - xHalf, center.y() - xHalf);
        markerPath.lineTo(center.x() + xHalf, center.y() + xHalf);
        markerPath.moveTo(center.x() - xHalf, center.y() + xHalf);
        markerPath.lineTo(center.x() + xHalf, center.y() - xHalf);
        break;
    case ObjectSnapKind::Center:
    case ObjectSnapKind::GeometricCenter: {
        penWidth = 1.8;
        const double crossHalf = halfSize * 1.8;
        const double circleRadius = halfSize * 0.95;
        markerPath.addEllipse(
            center.x() - circleRadius,
            center.y() - circleRadius,
            circleRadius * 2.0,
            circleRadius * 2.0);
        markerPath.moveTo(center.x() - crossHalf, center.y());
        markerPath.lineTo(center.x() + crossHalf, center.y());
        markerPath.moveTo(center.x(), center.y() - crossHalf);
        markerPath.lineTo(center.x(), center.y() + crossHalf);
        break;
    }
    case ObjectSnapKind::Nearest:
        // AutoCAD: hollow hourglass / diamond
        markerPath.moveTo(center.x(), center.y() - halfSize);
        markerPath.lineTo(center.x() + halfSize, center.y());
        markerPath.lineTo(center.x(), center.y() + halfSize);
        markerPath.lineTo(center.x() - halfSize, center.y());
        markerPath.closeSubpath();
        break;
    case ObjectSnapKind::Quadrant:
        markerPath.moveTo(center.x(), center.y() - halfSize);
        markerPath.lineTo(center.x() + halfSize, center.y());
        markerPath.lineTo(center.x(), center.y() + halfSize);
        markerPath.lineTo(center.x() - halfSize, center.y());
        markerPath.closeSubpath();
        break;
    case ObjectSnapKind::Node:
        markerPath.addEllipse(center, halfSize * 0.85, halfSize * 0.85);
        break;
    case ObjectSnapKind::Insertion: {
        const double arm = halfSize * 1.1;
        markerPath.moveTo(center.x() - arm, center.y());
        markerPath.lineTo(center.x() + arm, center.y());
        markerPath.moveTo(center.x(), center.y() - arm);
        markerPath.lineTo(center.x(), center.y() + arm);
        markerPath.addRect(center.x() - halfSize * 0.45, center.y() - halfSize * 0.45, halfSize * 0.9, halfSize * 0.9);
        break;
    }
    case ObjectSnapKind::Perpendicular: {
        const double arm = halfSize * 1.2;
        markerPath.moveTo(center.x() - arm, center.y());
        markerPath.lineTo(center.x() + arm, center.y());
        markerPath.moveTo(center.x(), center.y());
        markerPath.lineTo(center.x(), center.y() - arm);
        break;
    }
    case ObjectSnapKind::Tangent: {
        const double r = halfSize * 0.85;
        markerPath.addEllipse(center, r, r);
        markerPath.moveTo(center.x() - halfSize * 1.3, center.y() - r);
        markerPath.lineTo(center.x() + halfSize * 1.3, center.y() - r);
        break;
    }
    case ObjectSnapKind::Parallel: {
        const double arm = halfSize * 1.1;
        const double gap = halfSize * 0.35;
        markerPath.moveTo(center.x() - arm, center.y() - gap);
        markerPath.lineTo(center.x() + arm, center.y() - gap);
        markerPath.moveTo(center.x() - arm, center.y() + gap);
        markerPath.lineTo(center.x() + arm, center.y() + gap);
        break;
    }
    case ObjectSnapKind::Unknown:
    default: {
        // Grid / unclassified: small hollow cross — never a filled yellow blob.
        const double arm = halfSize * 0.9;
        markerPath.moveTo(center.x() - arm, center.y());
        markerPath.lineTo(center.x() + arm, center.y());
        markerPath.moveTo(center.x(), center.y() - arm);
        markerPath.lineTo(center.x(), center.y() + arm);
        break;
    }
    }

    const QColor markerColor = kind == ObjectSnapKind::Endpoint
        ? QColor(40, 220, 70)
        : QColor(255, 210, 64);
    QPen markerPen(markerColor);
    markerPen.setCosmetic(true);
    markerPen.setCapStyle(Qt::RoundCap);
    markerPen.setJoinStyle(Qt::RoundJoin);
    markerPen.setWidthF(penWidth);
    const QBrush markerBrush = Qt::NoBrush;

    QRectF oldBounds;
    if (snapMarkerItem_ && snapMarkerItem_->scene() == scene()) {
        oldBounds = snapMarkerItem_->mapRectToScene(snapMarkerItem_->boundingRect());
    }

    if (!snapMarkerItem_ || snapMarkerItem_->scene() != scene()) {
        ClearSnapMarker();
        snapMarkerItem_ = scene()->addPath(markerPath, markerPen, markerBrush);
        snapMarkerItem_->setZValue(1500.0);
        snapMarkerItem_->setCacheMode(QGraphicsItem::NoCache);
        snapMarkerItem_->setAcceptedMouseButtons(Qt::NoButton);
        snapMarkerItem_->setFlag(QGraphicsItem::ItemIsSelectable, false);
    } else {
        snapMarkerItem_->setPath(markerPath);
        snapMarkerItem_->setPen(markerPen);
        snapMarkerItem_->setBrush(markerBrush);
    }

    if (snapMarkerItem_) {
        const QRectF newBounds = snapMarkerItem_->mapRectToScene(snapMarkerItem_->boundingRect());
        QRectF dirty = oldBounds.united(newBounds);
        if (dirty.isValid()) {
            const double margin = ModelToleranceFromPixels(4.0);
            scene()->invalidate(dirty.adjusted(-margin, -margin, margin, margin), QGraphicsScene::AllLayers);
        }
        RepaintViewport();
    }
}

void CadView::ClearSnapMarker()
{
    if (!snapMarkerItem_) {
        return;
    }
    const QRectF bounds = snapMarkerItem_->mapRectToScene(snapMarkerItem_->boundingRect());
    if (snapMarkerItem_->scene()) {
        snapMarkerItem_->scene()->removeItem(snapMarkerItem_);
    }
    delete snapMarkerItem_;
    snapMarkerItem_ = nullptr;
    if (scene() && bounds.isValid()) {
        const double margin = ModelToleranceFromPixels(4.0);
        scene()->invalidate(bounds.adjusted(-margin, -margin, margin, margin), QGraphicsScene::AllLayers);
        RepaintViewport();
    }
}

GeoPoint CadView::ApplyOrtho(const GeoPoint& point) const
{
    if (toolMode_ == ToolMode::DrawDimLinear || toolMode_ == ToolMode::DrawDimAligned
        || toolMode_ == ToolMode::DrawDimRadius || toolMode_ == ToolMode::DrawDimJoggedRadius
        || toolMode_ == ToolMode::DrawDimDiameter || toolMode_ == ToolMode::DrawDimAngular) {
        return point;
    }

    if (!orthoEnabled_ || inputPoints_.empty()) {
        return point;
    }

    const GeoPoint& base = inputPoints_.back();
    const double dx = point.X() - base.X();
    const double dy = point.Y() - base.Y();
    if (std::abs(dx) >= std::abs(dy)) {
        return GeoPoint(point.X(), base.Y());
    }
    return GeoPoint(base.X(), point.Y());
}

GeoPoint CadView::ResolveInputPoint(const GeoPoint& rawPoint, bool forDrag) const
{
    // AutoCAD-like priority: object snap wins over ORTHO. Applying ortho to an
    // acquired osnap point pulls the rubber-band off the green marker and makes
    // high-zoom endpoint picks look "dead" until the user zooms out.
    if (const std::optional<ObjectSnapHit> objectSnap = FindObjectSnapHit(rawPoint)) {
        return objectSnap->point;
    }
    if (IsGridSnapActive(forDrag)) {
        return ApplyOrtho(SnapToGrid(rawPoint));
    }
    return ApplyOrtho(rawPoint);
}

EntityId CadView::HitTest(const GeoPoint& point) const
{
    if (!document_) {
        return 0;
    }

    const double tolerance = ModelToleranceFromPixels(snapTolerancePixels_);
    const QRectF visibleModelRect = VisibleModelRect(snapTolerancePixels_);
    double bestDistance = tolerance;
    EntityId bestId = 0;
    for (const auto& entity : document_->Entities()) {
        if (!document_->IsEntityVisible(*entity)) {
            continue;
        }
        if (!EntityIntersectsVisibleRect(*entity, visibleModelRect, tolerance)) {
            continue;
        }
        double distance = std::numeric_limits<double>::max();
        const GeoShape shape = entity->Shaper();
        GeoBounds clip = ModelRectToBounds(visibleModelRect);
        if (!shape.HasInfiniteLines()) {
            const GeoBounds bounds = shape.Bounds();
            clip = bounds.valid ? bounds : GeoBounds{
                point.X() - tolerance * 10.0,
                point.Y() - tolerance * 10.0,
                point.X() + tolerance * 10.0,
                point.Y() + tolerance * 10.0,
                true};
        }
        for (const GeoLine& segment : shape.ToSegments(clip)) {
            distance = std::min(distance, DistancePointToSegment(point, segment.Start(), segment.End()));
        }
        for (const GeoCircle& circle : shape.Circles()) {
            distance = std::min(distance, DistancePointToCircleCurve(point, circle));
        }
        for (const GeoArc& arc : shape.Arcs()) {
            distance = std::min(distance, DistancePointToArcCurve(point, arc));
        }
        for (const GeoPoint& shapePoint : shape.Points()) {
            distance = std::min(distance, Distance2D(point, shapePoint));
        }
        if (entity->Type() == EntityType::Region) {
            const auto& region = static_cast<const RegionEntity&>(*entity).Region();
            if (region.Contains(point)) {
                distance = 0.0;
            }
        }
        if (entity->Type() == EntityType::Text || entity->Type() == EntityType::MText) {
            TextLayoutOptions layoutOptions;
            layoutOptions.document = document_;
            layoutOptions.style = &entity->Style();
            distance = std::min(
                distance,
                TextLikeEntityDistanceToModelPoint(*entity, point, layoutOptions));
        } else if (entity->Type() == EntityType::Dimension) {
            const DimensionStyle dimStyle = ResolveDimensionStyleForEntity(
                document_,
                static_cast<const DimensionEntity&>(*entity));
            distance = std::min(
                distance,
                DimensionDistanceToModelPoint(
                    static_cast<const DimensionEntity&>(*entity),
                    point,
                    dimStyle,
                    tolerance));
        } else if (entity->Type() == EntityType::BlockReference || entity->Type() == EntityType::Group) {
            for (const GripInfo& grip : entity->Grips()) {
                distance = std::min(distance, Distance2D(point, grip.position));
            }
        }

        if (distance < bestDistance) {
            bestDistance = distance;
            bestId = entity->Id();
        }
    }
    return bestId;
}

EntityId CadView::HitTestTextAtPoint(const GeoPoint& point) const
{
    if (!document_) {
        return 0;
    }

    TextLayoutOptions layoutOptions;
    layoutOptions.document = document_;
    const double hitTolerance = ModelToleranceFromPixels(4.0);

    for (auto it = document_->Entities().rbegin(); it != document_->Entities().rend(); ++it) {
        const Entity* entity = it->get();
        if (!entity) {
            continue;
        }
        if (entity->Type() != EntityType::Text && entity->Type() != EntityType::MText) {
            continue;
        }
        layoutOptions.style = &entity->Style();
        if (TextLikeEntityContainsModelPoint(*entity, point, layoutOptions, hitTolerance)) {
            return entity->Id();
        }
    }
    return 0;
}

EntityId CadView::ResolveTextEditHit(const GeoPoint& rawPoint) const
{
    if (!document_) {
        return 0;
    }

    if (const EntityId textHit = HitTestTextAtPoint(rawPoint)) {
        return textHit;
    }

    TextLayoutOptions layoutOptions;
    layoutOptions.document = document_;
    const double hitTolerance = ModelToleranceFromPixels(4.0);

    auto isTextEntity = [](const Entity& entity) {
        return entity.Type() == EntityType::Text || entity.Type() == EntityType::MText;
    };

    for (EntityId selectedId : document_->SelectedEntityIds()) {
        const Entity* selected = document_->FindEntity(selectedId);
        if (!selected || !isTextEntity(*selected)) {
            continue;
        }
        layoutOptions.style = &selected->Style();
        if (TextLikeEntityContainsModelPoint(*selected, rawPoint, layoutOptions, hitTolerance)) {
            return selectedId;
        }
    }

    const EntityId hit = HitTest(rawPoint);
    if (hit != 0) {
        const Entity* entity = document_->FindEntity(hit);
        if (entity && isTextEntity(*entity)) {
            return hit;
        }
    }

    const std::vector<EntityId>& selectedIds = document_->SelectedEntityIds();
    if (selectedIds.size() == 1) {
        const Entity* selected = document_->FindEntity(selectedIds.front());
        if (selected && isTextEntity(*selected)) {
            layoutOptions.style = &selected->Style();
            const QRectF sceneBounds = TextLikeEntityBounds(*selected, layoutOptions);
            const QPointF scenePoint(rawPoint.X(), -rawPoint.Y());
            if (sceneBounds.isValid() && sceneBounds.contains(scenePoint)) {
                return selectedIds.front();
            }
        }
    }

    return 0;
}

bool CadView::IsSelected(EntityId id) const
{
    if (!document_ || id == 0) {
        return false;
    }
    const Entity* entity = document_->FindEntity(id);
    return entity && entity->IsSelected();
}

bool CadView::EntityFullyContainedInRect(const Entity& entity, const QRectF& modelRect, double tolerance) const
{
    if (entity.Type() == EntityType::Ray || entity.Type() == EntityType::XLine) {
        return false;
    }

    const GeoShape shape = entity.Shaper();
    const QRectF rect = modelRect.normalized();
    const GeoBounds clip = ModelRectToGeoBounds(rect);
    const std::vector<GeoLine> segments = shape.ToSegments(clip);

    auto pointInside = [&](const GeoPoint& point) {
        return PointInsideModelRect(point, rect, tolerance);
    };

    bool hasGeometry = false;

    for (const GeoCircle& circle : shape.Circles()) {
        hasGeometry = true;
        if (!CircleFullyInsideModelRect(circle, rect, tolerance)) {
            return false;
        }
    }

    for (const GeoLine& segment : shape.Segments()) {
        hasGeometry = true;
        if (!SegmentFullyInsideModelRect(segment.Start(), segment.End(), rect, tolerance)) {
            return false;
        }
    }

    for (const GeoPoint& point : shape.Points()) {
        hasGeometry = true;
        if (!pointInside(point)) {
            return false;
        }
    }

    for (const auto& polyline : shape.Polylines()) {
        for (const GeoPoint& point : polyline) {
            hasGeometry = true;
            if (!pointInside(point)) {
                return false;
            }
        }
    }

    for (const GeoLine& segment : segments) {
        hasGeometry = true;
        if (!SegmentFullyInsideModelRect(segment.Start(), segment.End(), rect, tolerance)) {
            return false;
        }
    }

    if (entity.Type() == EntityType::Region) {
        const auto& region = static_cast<const RegionEntity&>(entity).Region();
        if (region.Outer().size() >= 3) {
            hasGeometry = true;
            for (const GeoPoint& point : region.Outer()) {
                if (!pointInside(point)) {
                    return false;
                }
            }
        }
    }

    if (entity.Type() == EntityType::Text || entity.Type() == EntityType::MText) {
        TextLayoutOptions layoutOptions;
        layoutOptions.document = document_;
        layoutOptions.style = &entity.Style();
        return TextLikeEntityFullyInsideModelRect(entity, rect, tolerance, layoutOptions);
    }

    if (entity.Type() == EntityType::Dimension) {
        const DimensionStyle dimStyle = ResolveDimensionStyleForEntity(
            document_,
            static_cast<const DimensionEntity&>(entity));
        return DimensionFullyInsideModelRect(
            static_cast<const DimensionEntity&>(entity),
            rect,
            tolerance,
            dimStyle);
    }

    if (!hasGeometry) {
        const std::vector<GripInfo> grips = entity.Grips();
        if (grips.empty()) {
            return false;
        }
        for (const GripInfo& grip : grips) {
            if (!pointInside(grip.position)) {
                return false;
            }
        }
        return true;
    }

    return true;
}

bool CadView::EntityBoundsIntersectsRect(const Entity& entity, const GeoShape& shape, const QRectF& modelRect) const
{
    if (entity.Type() == EntityType::Ray || entity.Type() == EntityType::XLine) {
        return true;
    }

    const GeoBounds bounds = shape.Bounds();
    if (!bounds.valid) {
        return !entity.Grips().empty();
    }

    const QRectF entityRect(bounds.minX, bounds.minY, bounds.maxX - bounds.minX, bounds.maxY - bounds.minY);
    return entityRect.normalized().intersects(modelRect.normalized());
}

bool CadView::EntityCrossesRect(const Entity& entity, const QRectF& modelRect, double tolerance) const
{
    const GeoShape shape = entity.Shaper();
    const QRectF rect = modelRect.normalized();
    const GeoBounds clip = ModelRectToGeoBounds(rect);

    auto segmentCrosses = [&](const GeoPoint& start, const GeoPoint& end) {
        return SegmentIntersectsModelRect(start, end, rect, tolerance);
    };

    if (entity.Type() == EntityType::Ray || entity.Type() == EntityType::XLine) {
        for (const GeoLine& segment : shape.ToSegments(clip)) {
            if (segmentCrosses(segment.Start(), segment.End())) {
                return true;
            }
        }
        return false;
    }

    for (const GeoPoint& point : shape.Points()) {
        if (PointInsideModelRect(point, rect, tolerance)) {
            return true;
        }
    }

    for (const GeoLine& segment : shape.Segments()) {
        if (segmentCrosses(segment.Start(), segment.End())) {
            return true;
        }
    }

    for (std::size_t polylineIndex = 0; polylineIndex < shape.Polylines().size(); ++polylineIndex) {
        const auto& polyline = shape.Polylines()[polylineIndex];
        for (std::size_t i = 1; i < polyline.size(); ++i) {
            if (segmentCrosses(polyline[i - 1], polyline[i])) {
                return true;
            }
        }
        if (polylineIndex < shape.PolylineClosed().size()
            && shape.PolylineClosed()[polylineIndex]
            && polyline.size() >= 2
            && segmentCrosses(polyline.back(), polyline.front())) {
            return true;
        }
    }

    for (const GeoLine& segment : shape.ToSegments(clip)) {
        if (segmentCrosses(segment.Start(), segment.End())) {
            return true;
        }
    }

    for (const GeoCircle& circle : shape.Circles()) {
        if (CircleCrossesModelRect(circle, rect, tolerance)) {
            return true;
        }
    }

    if (entity.Type() == EntityType::Region) {
        const auto& region = static_cast<const RegionEntity&>(entity).Region();
        const auto& outer = region.Outer();
        if (outer.size() >= 3) {
            for (std::size_t i = 0; i < outer.size(); ++i) {
                const GeoPoint& start = outer[i];
                const GeoPoint& end = outer[(i + 1) % outer.size()];
                if (segmentCrosses(start, end)) {
                    return true;
                }
            }
            const GeoPoint center((rect.left() + rect.right()) * 0.5, (rect.top() + rect.bottom()) * 0.5);
            if (region.Contains(center)) {
                return true;
            }
        }
    }

    if (entity.Type() == EntityType::Text || entity.Type() == EntityType::MText) {
        TextLayoutOptions layoutOptions;
        layoutOptions.document = document_;
        layoutOptions.style = &entity.Style();
        return TextLikeEntityCrossesModelRect(entity, rect, tolerance, layoutOptions);
    }

    if (entity.Type() == EntityType::Dimension) {
        const DimensionStyle dimStyle = ResolveDimensionStyleForEntity(
            document_,
            static_cast<const DimensionEntity&>(entity));
        return DimensionCrossesModelRect(
            static_cast<const DimensionEntity&>(entity),
            rect,
            tolerance,
            dimStyle);
    }

    return false;
}

void CadView::ApplyBoxSelection(const QRectF& modelRect, bool windowMode, bool addToSelection)
{
    if (!document_) {
        return;
    }

    const double tolerance = Tolerance::Model;
    std::vector<EntityId> hits;
    for (const auto& entity : document_->Entities()) {
        const bool matched = windowMode
            ? EntityFullyContainedInRect(*entity, modelRect, tolerance)
            : EntityCrossesRect(*entity, modelRect, tolerance);
        if (matched) {
            hits.push_back(entity->Id());
        }
    }

    if (!addToSelection) {
        document_->ClearSelection();
    }
    for (EntityId id : hits) {
        if (Entity* entity = document_->FindEntity(id)) {
            entity->SetSelected(true);
        }
    }
    document_->SyncSelectionView();

    EmitStatus(windowMode
        ? QStringLiteral("窗口选择：选中 %1 个对象").arg(hits.size())
        : QStringLiteral("窗交选择：选中 %1 个对象").arg(hits.size()));
}

void CadView::UpdateBoxSelectPreview(const QPoint& currentViewPoint)
{
    if (!scene()) {
        return;
    }

    const bool windowMode = currentViewPoint.x() >= boxSelectStartView_.x();
    const QPointF sceneStart = mapToScene(boxSelectStartView_);
    const QPointF sceneEnd = mapToScene(currentViewPoint);
    QRectF sceneRect(sceneStart, sceneEnd);
    sceneRect = sceneRect.normalized();

    if (!boxSelectItem_) {
        boxSelectItem_ = scene()->addRect(sceneRect, QPen(), QBrush(Qt::NoBrush));
        boxSelectItem_->setZValue(2000.0);
    }
    boxSelectItem_->setRect(sceneRect);

    QPen pen(windowMode ? QColor(58, 142, 255) : QColor(58, 196, 58));
    pen.setStyle(windowMode ? Qt::SolidLine : Qt::DashLine);
    pen.setCosmetic(true);
    pen.setWidth(1);
    boxSelectItem_->setPen(pen);
    boxSelectItem_->setBrush(Qt::NoBrush);
}

void CadView::ClearBoxSelectPreview()
{
    if (!boxSelectItem_) {
        return;
    }
    const QRectF bounds = boxSelectItem_->sceneBoundingRect();
    if (boxSelectItem_->scene()) {
        boxSelectItem_->scene()->removeItem(boxSelectItem_);
    }
    delete boxSelectItem_;
    boxSelectItem_ = nullptr;
    if (scene() && bounds.isValid()) {
        scene()->invalidate(bounds.adjusted(-4.0, -4.0, 4.0, 4.0), QGraphicsScene::AllLayers);
        RepaintViewport();
    }
}

void CadView::FinalizeBoxSelection(const QPoint& endViewPoint, Qt::KeyboardModifiers modifiers)
{
    ClearBoxSelectPreview();
    boxSelecting_ = false;

    if (!document_) {
        return;
    }

    const QPoint delta = endViewPoint - boxSelectStartView_;
    const double pixelDrag = std::hypot(static_cast<double>(delta.x()), static_cast<double>(delta.y()));
    if (pixelDrag < 4.0) {
        if (!modifiers.testFlag(Qt::ShiftModifier)) {
            document_->ClearSelection();
        }
        return;
    }

    const bool windowMode = endViewPoint.x() >= boxSelectStartView_.x();
    const GeoPoint startModel = MapToModel(boxSelectStartView_);
    const GeoPoint endModel = MapToModel(endViewPoint);
    const QRectF modelRect = NormalizedModelRect(startModel, endModel);
    if (modelRect.width() <= Tolerance::Model && modelRect.height() <= Tolerance::Model) {
        if (!modifiers.testFlag(Qt::ShiftModifier)) {
            document_->ClearSelection();
        }
        return;
    }

    ApplyBoxSelection(modelRect, windowMode, modifiers.testFlag(Qt::ShiftModifier));
}

void CadView::HandleSelectPress(const GeoPoint& rawPoint, Qt::KeyboardModifiers modifiers)
{
    if (!document_ || !commandManager_) {
        return;
    }

    const GeoPoint snapPoint = ApplyOrtho(ApplySnap(rawPoint));

    if (splineGripAwaitingClick_ && splineGripEditing_) {
        CommitSplineGripEdit(snapPoint);
        return;
    }

    if (cadScene_) {
        const double gripTolerance = ModelToleranceFromPixels(Tolerance::GripTolerancePixels);
        if (GripItem* grip = cadScene_->HitGrip(ToScenePoint(snapPoint), gripTolerance)) {
            if (grip->Kind() == GripKind::MethodToggle) {
                if (Entity* entity = document_->FindEntity(grip->EntityIdValue())) {
                    if (entity->Type() == EntityType::Spline) {
                        static_cast<SplineEntity*>(entity)->Spline().ToggleDisplayMode();
                        cadScene_->RebuildGrips();
                        cadScene_->InvalidateItemRegions();
                    }
                }
                return;
            }

            HideSplineGripHoverMenu();
            if (document_) {
                if (Entity* splineEntity = document_->FindEntity(grip->EntityIdValue())) {
                    if (splineEntity->Type() == EntityType::Spline
                        && (grip->Kind() == GripKind::Vertex
                            || grip->Kind() == GripKind::ControlVertex)) {
                        BeginSplineGripEdit(
                            grip->EntityIdValue(),
                            grip->GripIndex(),
                            grip->Kind(),
                            SplineGripEditMode::Stretch,
                            true);
                        return;
                    }
                }
            }
            draggingGrip_ = true;
            gripEntityId_ = grip->EntityIdValue();
            gripIndex_ = grip->GripIndex();
            gripDragGroup_.clear();
            splineGripEditing_ = false;
            splineGripAwaitingClick_ = false;
            splineGripEditMode_ = SplineGripEditMode::Stretch;
            splineGripKind_ = grip->Kind();

            // Use the entity's true grip coordinates as the weld anchor (not the
            // click/snap point). This is critical when two line endpoints share a
            // corner but the cursor is slightly off the vertex.
            GeoPoint anchor = snapPoint;
            if (Entity* entity = document_->FindEntity(gripEntityId_)) {
                for (const GripInfo& info : entity->Grips()) {
                    if (info.index == gripIndex_) {
                        anchor = info.position;
                        break;
                    }
                }
            }
            gripStartPoint_ = anchor;

            // Collect every vertex grip that sits on this corner so they move as one
            // (AutoCAD closed-polyline / STRETCH-at-vertex behavior).
            // Control vertices stay isolated — they are not corner welds.
            if (grip->Kind() == GripKind::Vertex) {
                constexpr double kWeldTol = 1.0e-4;
                const auto nearlySame = [](const GeoPoint& a, const GeoPoint& b) {
                    return std::abs(a.X() - b.X()) <= kWeldTol && std::abs(a.Y() - b.Y()) <= kWeldTol;
                };
                for (const auto& entity : document_->Entities()) {
                    if (!entity) {
                        continue;
                    }
                    if (entity->Id() != gripEntityId_ && !entity->IsSelected()
                        && document_->SelectedEntityIds().size() > 1) {
                        continue;
                    }
                    for (const GripInfo& info : entity->Grips()) {
                        if (info.kind != GripKind::Vertex) {
                            continue;
                        }
                        if (!nearlySame(info.position, anchor)) {
                            continue;
                        }
                        gripDragGroup_.emplace_back(entity->Id(), info.index);
                    }
                }
            }
            if (gripDragGroup_.empty()) {
                gripDragGroup_.emplace_back(gripEntityId_, gripIndex_);
            }
            return;
        }
    }

    EntityId hit = HitTestTextAtPoint(rawPoint);
    if (hit == 0) {
        hit = HitTest(rawPoint);
    }

    if (hit == 0) {
        HideSplineGripHoverMenu();
        if (cadScene_) {
            cadScene_->ClearHotGrips();
        }
        boxSelecting_ = true;
        dragStartPoint_ = snapPoint;
        dragCurrentPoint_ = snapPoint;
        return;
    }

    if (modifiers.testFlag(Qt::ShiftModifier)) {
        document_->ToggleSelection(hit);
    } else if (!IsSelected(hit)) {
        document_->SelectOnly(hit);
    }

    if (!document_->SelectedEntityIds().empty()) {
        draggingSelection_ = true;
        dragMoved_ = false;
        dragStartPoint_ = snapPoint;
        dragCurrentPoint_ = snapPoint;
    }

    if (cadScene_) {
        cadScene_->InvalidateItemRegions();
    }
}

void CadView::HandleLeftPress(const GeoPoint& point, Qt::KeyboardModifiers modifiers)
{
    if (!document_ || !commandManager_) {
        return;
    }
    if (IsPolylineOptionEditing()) {
        EmitStatus(QStringLiteral("请先确认或取消当前选项"));
        return;
    }

    if (toolMode_ == ToolMode::InsertBlock) {
        if (!pendingBlockInsert_) {
            EmitStatus(QStringLiteral("插入块：无待插入参数"));
            SetToolMode(ToolMode::Select);
            return;
        }
        const PendingBlockInsert params = *pendingBlockInsert_;
        const EntityId id = commandManager_->InsertBlock(
            params.blockName,
            point,
            params.scale,
            params.rotationRadians,
            params.explodeOnInsert,
            params.attributes);
        if (params.explodeOnInsert) {
            EmitStatus(QStringLiteral("已插入并分解块「%1」").arg(QString::fromStdString(params.blockName)));
        } else if (id != 0) {
            EmitStatus(QStringLiteral("已插入块「%1」").arg(QString::fromStdString(params.blockName)));
        } else {
            EmitStatus(QStringLiteral("插入块失败"));
        }
        pendingBlockInsert_.reset();
        SetToolMode(ToolMode::Select);
        return;
    }

    if (toolMode_ == ToolMode::Move) {
        if (document_->SelectedEntityIds().empty()) {
            const EntityId hit = HitTest(point);
            if (hit != 0) {
                document_->SelectOnly(hit);
            }
        }
        inputPoints_.push_back(point);
        if (inputPoints_.size() == 2) {
            const double dx = inputPoints_[1].X() - inputPoints_[0].X();
            const double dy = inputPoints_[1].Y() - inputPoints_[0].Y();
            commandManager_->MoveSelected(dx, dy);
            ResetTransientState();
        }
        return;
    }

    if (toolMode_ == ToolMode::Mirror) {
        HandleMirrorPress(point, modifiers);
        return;
    }

    if (toolMode_ == ToolMode::Rotate) {
        if (inputPoints_.empty() && document_->SelectedEntityIds().empty()) {
            const EntityId hit = HitTest(point);
            if (hit != 0) {
                document_->SelectOnly(hit);
            } else {
                EmitStatus(QStringLiteral("旋转：请先选中图元"));
                return;
            }
        }
        inputPoints_.push_back(point);
        if (inputPoints_.size() == 1) {
            EmitStatus(QStringLiteral("旋转：指定角度点"));
        } else if (inputPoints_.size() == 2) {
            const double angle = std::atan2(inputPoints_[1].Y() - inputPoints_[0].Y(), inputPoints_[1].X() - inputPoints_[0].X());
            commandManager_->RotateSelected(inputPoints_[0], angle);
            EmitStatus(QStringLiteral("旋转完成"));
            ResetTransientState();
        }
        return;
    }

    if (toolMode_ == ToolMode::Scale) {
        if (inputPoints_.empty() && document_->SelectedEntityIds().empty()) {
            const EntityId hit = HitTest(point);
            if (hit != 0) {
                document_->SelectOnly(hit);
            } else {
                EmitStatus(QStringLiteral("缩放：请先选中图元，或点击要缩放的对象"));
                return;
            }
        } else if (document_->SelectedEntityIds().empty()) {
            EmitStatus(QStringLiteral("缩放：请先选中图元"));
            return;
        }
        inputPoints_.push_back(point);
        if (inputPoints_.size() == 1) {
            EmitStatus(QStringLiteral("缩放：指定参考长度第二点"));
        } else if (inputPoints_.size() == 2) {
            EmitStatus(QStringLiteral("缩放：指定目标长度点"));
        } else if (inputPoints_.size() == 3) {
            const double refDist = Distance2D(inputPoints_[0], inputPoints_[1]);
            const double targetDist = Distance2D(inputPoints_[0], inputPoints_[2]);
            if (refDist <= Tolerance::Model) {
                EmitStatus(QStringLiteral("缩放失败：参考长度为零"));
                ResetTransientState();
                return;
            }
            const double factor = std::max(0.001, targetDist / refDist);
            commandManager_->ScaleSelected(inputPoints_[0], factor);
            EmitStatus(QStringLiteral("缩放完成"));
            ResetTransientState();
        }
        return;
    }

    if (toolMode_ == ToolMode::Trim || toolMode_ == ToolMode::Extend) {
        const EntityId hit = HitTest(point);
        if (editHelperEntityId_ == 0) {
            if (hit == 0) {
                EmitStatus(toolMode_ == ToolMode::Trim
                    ? QStringLiteral("修剪：请点击要修剪的直线")
                    : QStringLiteral("延伸：请点击要延伸的直线"));
                return;
            }
            const Entity* entity = document_->FindEntity(hit);
            if (!entity || entity->Type() != EntityType::Line) {
                EmitStatus(QStringLiteral("修剪/延伸仅支持直线对象"));
                return;
            }
            editHelperEntityId_ = hit;
            document_->SelectOnly(hit);
            EmitStatus(toolMode_ == ToolMode::Trim
                ? QStringLiteral("修剪：请点击剪切边界")
                : QStringLiteral("延伸：请点击延伸边界"));
        } else if (hit != 0 && hit != editHelperEntityId_) {
            const bool ok = toolMode_ == ToolMode::Trim
                ? commandManager_->TrimSelected(hit, point)
                : commandManager_->ExtendSelected(hit, point);
            EmitStatus(ok
                ? (toolMode_ == ToolMode::Trim ? QStringLiteral("修剪完成") : QStringLiteral("延伸完成"))
                : (toolMode_ == ToolMode::Trim
                    ? QStringLiteral("修剪失败：边界无效或无法修剪")
                    : QStringLiteral("延伸失败：边界无效或无法延伸")));
            editHelperEntityId_ = 0;
        } else {
            EmitStatus(QStringLiteral("请选择与目标不同的边界对象"));
        }
        return;
    }

    if (toolMode_ == ToolMode::Break) {
        const EntityId hit = HitTest(point);
        auto isBreakable = [](EntityType type) {
            return type == EntityType::Line
                || type == EntityType::Polyline
                || type == EntityType::Arc
                || type == EntityType::Circle
                || type == EntityType::Spline;
        };

        if (editHelperEntityId_ == 0) {
            if (hit == 0) {
                EmitStatus(QStringLiteral("打断：请点击直线、多段线、圆弧、圆或样条曲线"));
                return;
            }
            const Entity* entity = document_->FindEntity(hit);
            if (!entity || !isBreakable(entity->Type())) {
                EmitStatus(QStringLiteral("打断：仅支持直线、多段线、圆弧、圆和样条曲线"));
                return;
            }
            editHelperEntityId_ = hit;
            document_->SelectOnly(hit);
            inputPoints_.push_back(point);
            EmitStatus(QStringLiteral("打断：指定第二个打断点；Enter/右键在一点处打断"));
            return;
        }

        inputPoints_.push_back(point);
        std::optional<GeoPoint> secondPoint;
        if (inputPoints_.size() >= 2 && Distance2D(inputPoints_[0], inputPoints_[1]) > Tolerance::Model) {
            secondPoint = inputPoints_[1];
        }
        const bool ok = commandManager_->BreakSelected(editHelperEntityId_, inputPoints_.front(), secondPoint);
        EmitStatus(ok
            ? (secondPoint ? QStringLiteral("打断完成：已删除两点间部分") : QStringLiteral("打断完成：对象已在一点处断开"))
            : QStringLiteral("打断失败：点无效或在端点上"));
        editHelperEntityId_ = 0;
        ResetTransientState();
        return;
    }

    if (toolMode_ == ToolMode::Fillet || toolMode_ == ToolMode::Chamfer) {
        HandleFilletChamferPress(point, modifiers);
        return;
    }

    if (toolMode_ == ToolMode::ArrayPolar) {
        if (arrayPhase_ == ArrayCommandPhase::BasePoint) {
            polarArrayOptions_.basePoint = point;
            ReturnToArrayPrompt();
            return;
        }
        if (arrayPhase_ != ArrayCommandPhase::Center) {
            EmitStatus(ArrayPromptText());
            UpdateArrayHud();
            return;
        }
        arrayCenter_ = point;
        ReturnToArrayPrompt();
        return;
    }

    if (toolMode_ == ToolMode::ArrayPath) {
        const EntityId pathId = HitTest(point);
        const Entity* path = pathId == 0 ? nullptr : document_->FindEntity(pathId);
        if (!path || !EditKernel::IsSupportedArrayPath(*path)) {
            EmitStatus(QStringLiteral("路径阵列：请选择有效的有限曲线作为路径"));
            return;
        }
        if (std::find(arraySourceIds_.begin(), arraySourceIds_.end(), pathId) != arraySourceIds_.end()) {
            EmitStatus(QStringLiteral("路径阵列：路径不能同时是被阵列的源对象"));
            return;
        }
        const bool ok = commandManager_->CreatePathArray(
            arraySourceIds_, pathId, pathArrayOptions_);
        EmitStatus(ok
            ? QStringLiteral("路径阵列完成")
            : QStringLiteral("路径阵列失败：路径长度或阵列参数无效"));
        if (ok) {
            SetToolMode(ToolMode::Select);
        }
        return;
    }

    if (toolMode_ == ToolMode::Offset) {
        if (inputPoints_.empty() && document_->SelectedEntityIds().empty()) {
            const EntityId hit = HitTest(point);
            if (hit != 0) {
                document_->SelectOnly(hit);
            } else {
                EmitStatus(QStringLiteral("偏移：请先选中可偏移的对象（圆、矩形、面域等）"));
                return;
            }
        }
        inputPoints_.push_back(point);
        if (inputPoints_.size() == 1) {
            EmitStatus(QStringLiteral("偏移：指定距离终点"));
        } else if (inputPoints_.size() == 2) {
            const bool ok = commandManager_->OffsetSelected(inputPoints_[0], inputPoints_[1]);
            EmitStatus(ok
                ? QStringLiteral("偏移完成")
                : QStringLiteral("偏移失败：对象无法转为闭合区域或距离为零"));
            ResetTransientState();
        }
        return;
    }

    if (toolMode_ == ToolMode::DrawText) {
        HandleTextToolPress(point);
        return;
    }

    if (toolMode_ == ToolMode::DrawMText) {
        HandleMTextToolPress(point);
        return;
    }

    if (toolMode_ == ToolMode::DrawDimLinear) {
        HandleDimLinearPress(point);
        return;
    }

    if (toolMode_ == ToolMode::DrawDimAligned) {
        HandleDimAlignedPress(point);
        return;
    }

    if (toolMode_ == ToolMode::DrawDimRadius) {
        HandleDimRadiusPress(point);
        return;
    }

    if (toolMode_ == ToolMode::DrawDimJoggedRadius) {
        HandleDimJoggedRadiusPress(point);
        return;
    }

    if (toolMode_ == ToolMode::DrawDimDiameter) {
        HandleDimDiameterPress(point);
        return;
    }

    if (toolMode_ == ToolMode::DrawDimAngular) {
        HandleDimAngularPress(point);
        return;
    }

    if (toolMode_ == ToolMode::DrawDimOrdinate) {
        HandleDimOrdinatePress(point);
        return;
    }

    if (IsDrawTool(toolMode_)) {
        if (toolMode_ == ToolMode::DrawSolidFill || toolMode_ == ToolMode::DrawHatchFill) {
            if (document_->SelectedEntityIds().empty()) {
                const EntityId hit = HitTest(point);
                if (hit != 0) {
                    document_->SelectOnly(hit);
                } else {
                    EmitStatus(QStringLiteral("填充：请先选中闭合图元，或点击要填充的对象"));
                    return;
                }
            }
            DrawToolResult drawResult = DrawToolEngine::ProcessInput(
                toolMode_, drawOptions_, inputPoints_, point, document_);
            if (drawResult.entity) {
                commandManager_->AddEntity(std::move(drawResult.entity));
                EmitStatus(toolMode_ == ToolMode::DrawSolidFill
                    ? QStringLiteral("实体填充完成")
                    : QStringLiteral("图案填充完成"));
                ResetTransientState();
            } else if (!drawResult.prompt.empty()) {
                EmitStatus(QString::fromStdString(drawResult.prompt));
            }
            return;
        }

        drawSegmentFinished_ = false;
        if (toolMode_ == ToolMode::DrawArc
            && drawOptions_.method == DrawMethod::ArcContinue
            && !lastArcContinue_.valid) {
            EmitStatus(QStringLiteral("连续：没有可衔接的直线、多段线或圆弧"));
            return;
        }
        if (IsCircleTangentObjectPick()) {
            const EntityId hit = HitTest(point);
            auto isTtrSupport = [](EntityType type) {
                return type == EntityType::Line
                    || type == EntityType::Ray
                    || type == EntityType::XLine
                    || type == EntityType::Circle
                    || type == EntityType::Arc
                    || type == EntityType::Polyline
                    || type == EntityType::Rectangle
                    || type == EntityType::Polygon;
            };
            if (hit == 0) {
                if (inputPoints_.empty()) {
                    EmitStatus(QStringLiteral("指定圆的第一条切线"));
                } else if (inputPoints_.size() == 1) {
                    EmitStatus(QStringLiteral("指定圆的第二条切线"));
                } else {
                    EmitStatus(QStringLiteral("指定圆的第三条切线"));
                }
                return;
            }
            const Entity* hitEntity = document_->FindEntity(hit);
            if (!hitEntity || !isTtrSupport(hitEntity->Type())) {
                EmitStatus(QStringLiteral("请选择直线、射线、构造线、圆、圆弧、多段线、矩形或多边形"));
                return;
            }
        }
        // 必须在 push_back 之前同步 DYN 显式夹角/弦长：IsArc*Pick 依赖 size==2（第三点拾取中）。
        // 若先 push 再 Sync，size==3 时 IsArcIncludedAnglePick 为 false，键入的 -90° 会丢失，
        // 随后按端点走 CCW 量角 → 误成 270° 逆时针弧。
        SyncArcContinueOptions();
        SyncSeaIncludedAngleOptions();
        SyncSclChordLengthOptions();
        if (toolMode_ == ToolMode::DrawArc
            && (drawOptions_.method == DrawMethod::ArcStartEndAngle
                || drawOptions_.method == DrawMethod::ArcStartCenterAngle
                || drawOptions_.method == DrawMethod::ArcCenterStartAngle
                || drawOptions_.method == DrawMethod::ArcCenterStartChord
                || drawOptions_.method == DrawMethod::ArcStartEndRadius
                || drawOptions_.method == DrawMethod::ArcStartCenterChord
                || drawOptions_.method == DrawMethod::ArcStartCenterEnd)
            && inputPoints_.size() == 2) {
            drawOptions_.clockwise =
                (QGuiApplication::keyboardModifiers() & Qt::ControlModifier) != 0;
        }
        if (IsContinuousPolylineDraw() && drawOptions_.polylineArcMode) {
            drawOptions_.clockwise =
                (QGuiApplication::keyboardModifiers() & Qt::ControlModifier) != 0;
        }
        drawOptions_.pickTolerance = ModelToleranceFromPixels(snapTolerancePixels_);
        if (toolMode_ == ToolMode::DrawPolygon && polygonPhase_ == PolygonCommandPhase::Option) {
            if (drawOptions_.method == DrawMethod::PolygonCenterRadius) {
                drawOptions_.method = DrawMethod::PolygonInscribed;
            }
            polygonPhase_ = PolygonCommandPhase::Radius;
        }
        inputPoints_.push_back(point);
        DrawToolResult drawResult = DrawToolEngine::ProcessInput(
            toolMode_, drawOptions_, inputPoints_, point, document_);
        if (toolMode_ == ToolMode::DrawPolygon
            && !drawResult.entity
            && inputPoints_.size() == 1
            && drawOptions_.method != DrawMethod::PolygonEdge
            && polygonPhase_ == PolygonCommandPhase::Center) {
            polygonPhase_ = PolygonCommandPhase::Option;
            EmitStatus(PolygonPromptText());
            UpdatePolygonHud();
        } else if (!drawResult.prompt.empty()) {
            EmitStatus(QString::fromStdString(drawResult.prompt));
        }
        if (drawResult.entity && !DrawToolEngine::IsContinuousDrawTool(toolMode_)) {
            const EntityId newEntityId = drawResult.entity->Id();
            NoteArcContinuation(*drawResult.entity);
            commandManager_->AddEntity(std::move(drawResult.entity));
            if (drawResult.continueFromLastPoint
                && toolMode_ == ToolMode::DrawArc
                && drawOptions_.method == DrawMethod::ArcContinue) {
                inputPoints_.clear();
                SyncArcContinueOptions();
                if (!drawResult.nextPrompt.empty()) {
                    EmitStatus(QString::fromStdString(drawResult.nextPrompt));
                }
                UpdatePreview(point);
            } else if (drawResult.continueFromLastPoint && !inputPoints_.empty()) {
                // AutoCAD LINE: commit segment, then continue from the endpoint.
                if (lineChainPoints_.empty()) {
                    lineChainPoints_.push_back(inputPoints_.front());
                }
                lineChainPoints_.push_back(inputPoints_.back());
                lineChainEntityIds_.push_back(newEntityId);
                const GeoPoint nextStart = inputPoints_.back();
                inputPoints_.clear();
                inputPoints_.push_back(nextStart);
                if (!drawResult.nextPrompt.empty()) {
                    EmitStatus(QString::fromStdString(drawResult.nextPrompt));
                }
                UpdatePreview(point);
            } else if (drawResult.keepBasePoint && inputPoints_.size() >= 2) {
                const GeoPoint base = inputPoints_.front();
                inputPoints_.clear();
                inputPoints_.push_back(base);
                if (!drawResult.nextPrompt.empty()) {
                    EmitStatus(QString::fromStdString(drawResult.nextPrompt));
                }
                UpdatePreview(point);
            } else {
                ResetTransientState();
                if (toolMode_ == ToolMode::DrawPolygon) {
                    EmitStatus(PolygonPromptText());
                    UpdatePolygonHud();
                }
            }
        } else if (IsDrawTool(toolMode_) && !inputPoints_.empty()) {
            // Continuous PLINE: reject coincident vertex; otherwise store bulge for the new segment.
            if (IsContinuousPolylineDraw() && inputPoints_.size() >= 2) {
                if (Distance2D(inputPoints_.back(), inputPoints_[inputPoints_.size() - 2]) <= Tolerance::Model) {
                    inputPoints_.pop_back();
                    if (!drawResult.prompt.empty()) {
                        EmitStatus(QString::fromStdString(drawResult.prompt));
                    }
                } else {
                    CommitLastPolylineSegment();
                    drawOptions_.polylineLengthMode = false;
                }
                UpdatePreview(point);
            } else if (IsSplineDraw() && inputPoints_.size() >= 2) {
                if (Distance2D(inputPoints_.back(), inputPoints_[inputPoints_.size() - 2]) <= Tolerance::Model) {
                    inputPoints_.pop_back();
                    if (!drawResult.prompt.empty()) {
                        EmitStatus(QString::fromStdString(drawResult.prompt));
                    }
                }
                UpdatePreview(point);
            } else if (IsContinuousLineDraw() && !drawResult.entity && inputPoints_.size() >= 2) {
                inputPoints_.pop_back();
                if (!drawResult.prompt.empty()) {
                    EmitStatus(QString::fromStdString(drawResult.prompt));
                }
                UpdatePreview(point);
            } else if (toolMode_ == ToolMode::DrawArc
                && (drawOptions_.method == DrawMethod::ArcStartCenterAngle
                    || drawOptions_.method == DrawMethod::ArcCenterStartAngle
                    || drawOptions_.method == DrawMethod::ArcStartCenterEnd)
                && !drawResult.entity
                && inputPoints_.size() == 3) {
                // Invalid included angle (≈0): drop the angle pick and keep waiting.
                inputPoints_.pop_back();
                EmitStatus(drawResult.prompt.empty()
                    ? (drawOptions_.method == DrawMethod::ArcStartCenterEnd
                        ? QStringLiteral("指定圆弧的端点(按住 Ctrl 键以切换方向)")
                        : QStringLiteral("指定夹角(按住 Ctrl 键以切换方向)"))
                    : QString::fromStdString(drawResult.prompt));
                UpdatePreview(point);
            } else if (toolMode_ == ToolMode::DrawArc
                && (drawOptions_.method == DrawMethod::ArcStartCenterChord
                    || drawOptions_.method == DrawMethod::ArcCenterStartChord)
                && !drawResult.entity
                && inputPoints_.size() == 3) {
                inputPoints_.pop_back();
                EmitStatus(drawResult.prompt.empty()
                    ? QStringLiteral("指定弦长(按住 Ctrl 键以切换方向)")
                    : QString::fromStdString(drawResult.prompt));
                UpdatePreview(point);
            } else if (toolMode_ == ToolMode::DrawArc
                && drawOptions_.method == DrawMethod::ArcStartEndAngle
                && !drawResult.entity
                && inputPoints_.size() == 3) {
                inputPoints_.pop_back();
                EmitStatus(drawResult.prompt.empty()
                    ? QStringLiteral("指定夹角(按住 Ctrl 键以切换方向)")
                    : QString::fromStdString(drawResult.prompt));
                UpdatePreview(point);
            } else if (toolMode_ == ToolMode::DrawArc
                && drawOptions_.method == DrawMethod::ArcContinue
                && !drawResult.entity
                && lastArcContinue_.valid
                && !inputPoints_.empty()) {
                inputPoints_.pop_back();
                EmitStatus(drawResult.prompt.empty()
                    ? QStringLiteral("指定圆弧的端点")
                    : QString::fromStdString(drawResult.prompt));
                UpdatePreview(point);
            } else if (toolMode_ == ToolMode::DrawArc
                && drawOptions_.method == DrawMethod::ArcStartEndRadius
                && !drawResult.entity
                && inputPoints_.size() == 3) {
                inputPoints_.pop_back();
                EmitStatus(drawResult.prompt.empty()
                    ? QStringLiteral("指定圆弧的半径")
                    : QString::fromStdString(drawResult.prompt));
                UpdatePreview(point);
            } else if (toolMode_ == ToolMode::DrawCircle
                && (drawOptions_.method == DrawMethod::CircleTanTanRadius
                    || drawOptions_.method == DrawMethod::CircleTanTanTan)
                && !drawResult.entity
                && inputPoints_.size() == 3) {
                inputPoints_.pop_back();
                EmitStatus(drawResult.prompt.empty()
                    ? QStringLiteral("圆不存在")
                    : QString::fromStdString(drawResult.prompt));
                UpdatePreview(point);
            } else if (!drawResult.entity
                && drawResult.prompt.empty()
                && !DrawToolEngine::IsContinuousDrawTool(toolMode_)
                && !IsContinuousLineDraw()
                && inputPoints_.size() >= 3) {
                // Enough points were collected but geometry could not be built (e.g.
                // coincident/collinear picks after a bad view transform). Drop the
                // stuck chain so the next click starts fresh instead of appearing dead.
                EmitStatus(QStringLiteral("无法根据当前点生成对象，请重新指定"));
                ResetTransientState();
            } else {
                // First point of a continuous LINE chain.
                if (IsContinuousLineDraw()
                    && lineChainPoints_.empty()
                    && inputPoints_.size() == 1) {
                    lineChainPoints_.push_back(inputPoints_.front());
                }
                UpdatePreview(point);
            }
        }
        return;
    }
}

void CadView::HandleLeftRelease(const GeoPoint& point)
{
    if (splineGripEditing_) {
        draggingGrip_ = false;
        const bool moved = Distance2D(gripStartPoint_, point) > Tolerance::Model;
        if (moved || splineGripEditMode_ == SplineGripEditMode::RemovePoint) {
            CommitSplineGripEdit(point);
        } else {
            splineGripAwaitingClick_ = true;
            if (cadScene_) {
                cadScene_->SetHotGrip(gripEntityId_, gripIndex_);
            }
            const GeoPoint editPoint = ResolveDynamicLineEndpoint(gripStartPoint_, point);
            UpdateSplineGripEditPreview(editPoint);
            if (viewport()) {
                UpdateSplineGripEditHud(lastCursorViewportPos_, editPoint);
            }
        }
        return;
    }

    if (draggingGrip_) {
        draggingGrip_ = false;
        if (Distance2D(gripStartPoint_, point) > Tolerance::Model && commandManager_ && document_) {
            // Weld every coincident corner grip to the exact same target point.
            std::vector<EntityId> changed;
            changed.reserve(gripDragGroup_.size());
            for (const auto& target : gripDragGroup_) {
                Entity* entity = document_->FindEntity(target.first);
                if (!entity) {
                    continue;
                }
                if (entity->Type() == EntityType::BlockReference
                    && target.second >= BlockReferenceEntity::kDynamicGripBase) {
                    commandManager_->ApplyGripEdit(target.first, target.second, point);
                    changed.push_back(target.first);
                    continue;
                }
                entity->SetGrip(target.second, point);
                changed.push_back(target.first);
            }
            if (changed.empty()) {
                commandManager_->ApplyGripEdit(gripEntityId_, gripIndex_, point);
            } else {
                // Unique ids then notify.
                std::sort(changed.begin(), changed.end());
                changed.erase(std::unique(changed.begin(), changed.end()), changed.end());
                document_->NotifyEntitiesUpdated(changed);
            }
        }
        gripDragGroup_.clear();
        if (cadScene_) {
            cadScene_->RebuildGrips();
            cadScene_->InvalidateItemRegions();
        }
        return;
    }

    if (!draggingSelection_) {
        return;
    }

    dragCurrentPoint_ = point;
    draggingSelection_ = false;

    if (dragMoved_ && commandManager_ && cadScene_) {
        const double dx = dragCurrentPoint_.X() - dragStartPoint_.X();
        const double dy = dragCurrentPoint_.Y() - dragStartPoint_.Y();
        cadScene_->ClearSelectionDragOffset();
        commandManager_->MoveSelected(dx, dy);
    } else if (cadScene_) {
        cadScene_->ClearSelectionDragOffset();
    }

    dragMoved_ = false;
    InvalidateGridBackground();
}

void CadView::UpdatePreview(const GeoPoint& point)
{
    if (IsFilletChamferCommand()) {
        UpdateFilletChamferPreview(point);
        if (optionInput_ && optionInput_->ContainsGlobalPos(QCursor::pos())) {
            return;
        }
        if (optionInput_ && optionInput_->IsKeywordEditing()) {
            optionInput_->MoveToViewPosition(lastCursorViewportPos_);
        } else {
            UpdateFilletChamferHud();
        }
        return;
    }
    if (toolMode_ == ToolMode::ArrayPath || toolMode_ == ToolMode::ArrayPolar) {
        if (toolMode_ == ToolMode::ArrayPolar
            && arrayPhase_ == ArrayCommandPhase::BasePoint
            && !arrayCenter_) {
            ClearPreview();
        } else {
            UpdateArrayPreview(point);
        }
        if (toolMode_ == ToolMode::ArrayPolar) {
            if (!ShouldShowDynPointInput()) {
                HidePointInput();
            }
            if (optionInput_ && optionInput_->ContainsGlobalPos(QCursor::pos())) {
                return;
            }
            if (optionInput_ && optionInput_->IsKeywordEditing()) {
                optionInput_->MoveToViewPosition(lastCursorViewportPos_);
            } else {
                UpdateArrayHud();
            }
        }
        return;
    }
    if (IsPolylineOptionEditing()) {
        return;
    }
    if (!scene()) {
        ClearPreview();
        return;
    }

    if (toolMode_ == ToolMode::Select && inputPoints_.empty() && !pasteTextPending_) {
        return;
    }

    if (toolMode_ == ToolMode::DrawDimLinear || toolMode_ == ToolMode::DrawDimAligned
        || toolMode_ == ToolMode::DrawDimRadius || toolMode_ == ToolMode::DrawDimJoggedRadius
        || toolMode_ == ToolMode::DrawDimDiameter || toolMode_ == ToolMode::DrawDimAngular
        || toolMode_ == ToolMode::DrawDimOrdinate) {
        dragCurrentPoint_ = point;
    }

    if (toolMode_ == ToolMode::Move && !inputPoints_.empty()) {
        QPainterPath path(ToScenePoint(inputPoints_.front()));
        path.lineTo(ToScenePoint(point));
        SetPreviewPath(path);
        return;
    }

    if ((toolMode_ == ToolMode::Scale || toolMode_ == ToolMode::Offset || toolMode_ == ToolMode::Break) && !inputPoints_.empty()) {
        QPainterPath path(ToScenePoint(inputPoints_.front()));
        path.lineTo(ToScenePoint(point));
        SetPreviewPath(path);
        return;
    }

    if (toolMode_ == ToolMode::Mirror && mirrorPhase_ == MirrorPhase::PickAxisSecond) {
        UpdateMirrorPreview(point);
        return;
    }

    if (pasteTextPending_) {
        if (pointInput_) {
            UpdatePasteTextPreview(pointInput_->ModelPoint());
        } else {
            UpdatePasteTextPreview(point);
        }
        return;
    }

    if (toolMode_ == ToolMode::DrawMText
        && drawOptions_.method == DrawMethod::MTextCornerWidth
        && !inputPoints_.empty()) {
        SetPreviewPath(ModelRectToScenePath(inputPoints_.front(), point));
        return;
    }

    if (toolMode_ == ToolMode::DrawDimAligned && !inputPoints_.empty()) {
        if (inputPoints_.size() == 1) {
            ClearPreviewDimLabel();
            QPainterPath path;
            path.moveTo(ToScenePoint(inputPoints_.front()));
            path.lineTo(ToScenePoint(point));
            SetPreviewPath(path);
            return;
        }

        DimensionStyle dimStyle = ActiveDimensionStyle();
        const AlignedDimensionLayout layout = BuildAlignedDimensionLayout(
            inputPoints_[0],
            inputPoints_[1],
            point,
            dimStyle,
            dimAlignedOverrideText_,
            dimAlignedTextRotationRadians_);
        SetPreviewPath(AlignedDimensionScenePath(layout, dimStyle));
        SetPreviewAlignedDimLabel(layout, dimStyle);
        EmitStatus(QStringLiteral("对齐标注：指定尺寸线位置，测量值 %1，选项 [多行文字(M)/文字(T)/角度(A)]")
            .arg(layout.label));
        return;
    }

    if (toolMode_ == ToolMode::DrawDimLinear && !inputPoints_.empty()) {
        if (inputPoints_.size() == 1) {
            ClearPreviewDimLabel();
            QPainterPath path;
            path.moveTo(ToScenePoint(inputPoints_.front()));
            path.lineTo(ToScenePoint(point));
            SetPreviewPath(path);
            return;
        }

        const DimensionStyle dimStyle = ActiveDimensionStyle();
        const LinearDimensionLayout layout = BuildLinearDimensionLayout(
            inputPoints_[0],
            inputPoints_[1],
            point,
            dimStyle,
            dimLinearOrientationOverride_,
            dimLinearOverrideText_,
            dimLinearTextRotationRadians_);
        SetPreviewPath(LinearDimensionScenePath(layout, dimStyle));
        SetPreviewDimLabel(layout, dimStyle);
        EmitStatus(QStringLiteral("线性标注：指定尺寸线位置，测量值 %1，选项 [多行文字(M)/文字(T)/角度(A)/旋转(R)]")
            .arg(layout.label));
        return;
    }

    if (toolMode_ == ToolMode::DrawDimRadius && inputPoints_.size() == 1) {
        const DimensionStyle dimStyle = ActiveDimensionStyle();
        const GeoPoint center = inputPoints_.front();
        const GeoPoint attachment = ComputeRadiusAttachmentPoint(
            center,
            dimRadiusValue_,
            point,
            dimRadiusHasArcSpan_,
            dimRadiusArcStartRadians_,
            dimRadiusArcEndRadians_);
        const RadiusDimensionLayout layout = BuildRadiusDimensionLayout(
            center,
            attachment,
            point,
            dimStyle,
            dimRadiusOverrideText_,
            dimRadiusTextRotationRadians_);
        SetPreviewPath(RadiusDimensionScenePath(layout, dimStyle));
        SetPreviewRadiusDimLabel(layout, dimStyle);
        EmitStatus(QStringLiteral("半径标注：指定尺寸线位置，测量值 %1，选项 [多行文字(M)/文字(T)/角度(A)]")
            .arg(layout.label));
        return;
    }

    if (toolMode_ == ToolMode::DrawDimDiameter && inputPoints_.size() == 1) {
        const DimensionStyle dimStyle = ActiveDimensionStyle();
        const GeoPoint center = inputPoints_.front();
        const GeoPoint attachment = ComputeRadiusAttachmentPoint(
            center,
            dimRadiusValue_,
            point,
            dimRadiusHasArcSpan_,
            dimRadiusArcStartRadians_,
            dimRadiusArcEndRadians_);
        const DiameterDimensionLayout layout = BuildDiameterDimensionLayout(
            center,
            attachment,
            point,
            dimStyle,
            dimRadiusOverrideText_,
            dimRadiusTextRotationRadians_);
        SetPreviewPath(DiameterDimensionScenePath(layout, dimStyle));
        SetPreviewDiameterDimLabel(layout, dimStyle);
        EmitStatus(QStringLiteral("直径标注：指定尺寸线位置，测量值 %1，选项 [多行文字(M)/文字(T)/角度(A)]")
            .arg(layout.label));
        return;
    }

    if (toolMode_ == ToolMode::DrawDimAngular && dimAngularPlacementReady_) {
        const DimensionStyle dimStyle = ActiveDimensionStyle();
        GeoPoint placement = point;
        if (dimAngularQuadrantLock_ >= 0) {
            const double quadrantMid = (static_cast<double>(dimAngularQuadrantLock_) + 0.5)
                * 3.14159265358979323846 * 0.5;
            const double radius = std::max(
                std::hypot(point.X() - dimAngularVertex_.X(), point.Y() - dimAngularVertex_.Y()),
                dimStyle.extensionOffset + dimStyle.textHeight);
            placement = GeoPoint(
                dimAngularVertex_.X() + std::cos(quadrantMid) * radius,
                dimAngularVertex_.Y() + std::sin(quadrantMid) * radius);
        }
        const AngularDimensionLayout layout = BuildAngularDimensionLayout(
            dimAngularVertex_,
            dimAngularArm1_,
            dimAngularArm2_,
            placement,
            dimStyle,
            dimAngularOverrideText_,
            dimAngularTextRotationRadians_,
            dimAngularHasArcSpan_,
            dimAngularArcStartRadians_,
            dimAngularArcEndRadians_,
            dimAngularSourceArcRadius_);
        SetPreviewPath(AngularDimensionScenePath(layout, dimStyle));
        SetPreviewAngularDimLabel(layout, dimStyle);
        EmitStatus(QStringLiteral("角度标注：指定标注弧位置，测量值 %1，选项 [多行文字(M)/文字(T)/角度(A)/象限(Q)]")
            .arg(layout.label));
        return;
    }

    if (toolMode_ == ToolMode::DrawDimOrdinate && !inputPoints_.empty()) {
        const DimensionStyle dimStyle = ActiveDimensionStyle();
        const GeoPoint ucsOrigin(0.0, 0.0);
        const OrdinateDimensionLayout layout = BuildOrdinateDimensionLayout(
            inputPoints_[0],
            point,
            ucsOrigin,
            dimStyle,
            dimOrdinateOrientationOverride_,
            dimOrdinateOverrideText_,
            dimOrdinateTextRotationRadians_);
        SetPreviewPath(OrdinateDimensionScenePath(layout, dimStyle));
        SetPreviewOrdinateDimLabel(layout, dimStyle);
        const QString axisText = layout.orientation == LinearDimensionOrientation::Vertical
            ? QStringLiteral("X")
            : QStringLiteral("Y");
        EmitStatus(QStringLiteral("坐标标注：指定引线位置，%1 = %2，选项 [多行文字(M)/文字(T)/角度(A)/X/Y]")
            .arg(axisText, layout.label));
        return;
    }

    if (toolMode_ == ToolMode::DrawDimJoggedRadius && !inputPoints_.empty()) {
        const DimensionStyle dimStyle = ActiveDimensionStyle();
        const GeoPoint trueCenter = inputPoints_.front();

        if (inputPoints_.size() == 1) {
            QPainterPath path;
            const GeoPoint snapped = ApplySnap(point);
            path.moveTo(ToScenePoint(snapped));
            const GeoPoint attachment = ComputeRadiusAttachmentPoint(
                trueCenter,
                dimRadiusValue_,
                snapped,
                dimRadiusHasArcSpan_,
                dimRadiusArcStartRadians_,
                dimRadiusArcEndRadians_);
            path.lineTo(ToScenePoint(attachment));
            SetPreviewPath(path);
            ClearPreviewDimLabel();
            EmitStatus(QStringLiteral("折弯半径：指定圆心覆盖点"));
            return;
        }

        const GeoPoint centerOverride = inputPoints_[1];
        if (inputPoints_.size() == 2) {
            const GeoPoint attachment = ComputeRadiusAttachmentPoint(
                trueCenter,
                dimRadiusValue_,
                point,
                dimRadiusHasArcSpan_,
                dimRadiusArcStartRadians_,
                dimRadiusArcEndRadians_);
            const double ux = (attachment.X() - trueCenter.X()) / dimRadiusValue_;
            const double uy = (attachment.Y() - trueCenter.Y()) / dimRadiusValue_;
            const double overrideParam = (centerOverride.X() - trueCenter.X()) * ux
                + (centerOverride.Y() - trueCenter.Y()) * uy;
            const double textParam = (point.X() - trueCenter.X()) * ux + (point.Y() - trueCenter.Y()) * uy;
            const double jogParam = overrideParam + (textParam - overrideParam) * 0.55;
            const GeoPoint defaultJog(
                trueCenter.X() + ux * jogParam,
                trueCenter.Y() + uy * jogParam);
            const JoggedRadiusDimensionLayout layout = BuildJoggedRadiusDimensionLayout(
                trueCenter,
                centerOverride,
                point,
                defaultJog,
                dimStyle,
                dimRadiusOverrideText_,
                dimRadiusTextRotationRadians_,
                dimRadiusHasArcSpan_,
                dimRadiusArcStartRadians_,
                dimRadiusArcEndRadians_,
                dimRadiusValue_);
            SetPreviewPath(JoggedRadiusDimensionScenePath(layout, dimStyle));
            SetPreviewJoggedRadiusDimLabel(layout, dimStyle);
            EmitStatus(QStringLiteral("折弯半径：指定尺寸线位置，测量值 %1，选项 [多行文字(M)/文字(T)/角度(A)]")
                .arg(layout.label));
            return;
        }

        const GeoPoint dimLineLocation = inputPoints_[2];
        const JoggedRadiusDimensionLayout layout = BuildJoggedRadiusDimensionLayout(
            trueCenter,
            centerOverride,
            dimLineLocation,
            point,
            dimStyle,
            dimRadiusOverrideText_,
            dimRadiusTextRotationRadians_,
            dimRadiusHasArcSpan_,
            dimRadiusArcStartRadians_,
            dimRadiusArcEndRadians_,
            dimRadiusValue_);
        SetPreviewPath(JoggedRadiusDimensionScenePath(layout, dimStyle));
        SetPreviewJoggedRadiusDimLabel(layout, dimStyle);
        EmitStatus(QStringLiteral("折弯半径：指定折弯位置，测量值 %1").arg(layout.label));
        return;
    }

    const bool arcContinuePreview = IsArcContinuePick();
    if (!IsDrawTool(toolMode_) || (inputPoints_.empty() && !arcContinuePreview)) {
        ClearPreview();
        return;
    }

    SyncArcContinueOptions();
    SyncSeaIncludedAngleOptions();
    SyncSclChordLengthOptions();
    drawOptions_.pickTolerance = ModelToleranceFromPixels(snapTolerancePixels_);
    if (IsArcStartEndAnglePick() || IsArcIncludedAnglePick() || IsArcStartEndRadiusPick()
        || IsArcChordLengthPick()
        || IsArcStartCenterEndEndPick()
        || (IsContinuousPolylineDraw() && drawOptions_.polylineArcMode)) {
        drawOptions_.clockwise =
            (QGuiApplication::keyboardModifiers() & Qt::ControlModifier) != 0;
    }
    GeoPoint previewPoint = point;
    GeoPoint polarBase;
    if (TryGetPolarBasePoint(&polarBase)) {
        bool trackingSnapped = false;
        previewPoint = ResolveDynamicLineEndpoint(polarBase, point, &trackingSnapped);
        const double dx = previewPoint.X() - polarBase.X();
        const double dy = previewPoint.Y() - polarBase.Y();
        const double angle = (std::hypot(dx, dy) > Tolerance::Model) ? std::atan2(dy, dx) : 0.0;
        // Re-check polar on the final rubber-band angle (covers object-snap overrides).
        if (!trackingSnapped) {
            ApplyPolarTracking(angle, &trackingSnapped);
        }
        UpdatePolarGuidePreview(polarBase, previewPoint, angle, trackingSnapped, &point);
    } else {
        ClearPolarGuidePreview();
    }

    const DrawToolResult drawResult = DrawToolEngine::ProcessInput(
        toolMode_, drawOptions_, inputPoints_, previewPoint, document_);

    const GeoBounds clipBounds = ModelRectToBounds(VisibleModelRect(snapTolerancePixels_));
    const GeoBounds* clipPtr = drawResult.preview.HasInfiniteLines() ? &clipBounds : nullptr;
    const CurveDisplayOptions display = MakeCurveDisplayOptions();
    SetPreviewPath(GeoShapePainter::BuildPath(drawResult.preview, clipPtr, &display));
    if (toolMode_ == ToolMode::DrawPolygon) {
        if (optionInput_ && optionInput_->IsKeywordEditing()) {
            optionInput_->MoveToViewPosition(lastCursorViewportPos_);
        } else {
            UpdatePolygonHud();
        }
    }
}

void CadView::AcquirePreviewItem()
{
    if (!scene()) {
        return;
    }
    if (!previewItem_ || previewItem_->scene() != scene()) {
        ClearPreview();
        auto* item = new AntialiasedPathItem;
        scene()->addItem(item);
        previewItem_ = item;
        previewItem_->setZValue(1000.0);
        previewItem_->setCacheMode(QGraphicsItem::NoCache);
        previewItem_->setAcceptedMouseButtons(Qt::NoButton);
        previewItem_->setFlag(QGraphicsItem::ItemIsSelectable, false);
        ApplyPreviewPen();
    }
}

void CadView::ApplyPreviewPen()
{
    if (!previewItem_) {
        return;
    }

    // AutoCAD-like dynamic LINE / PLINE preview: light rubber-band over dark background.
    if (IsSplineGripEditing()
        || ((IsContinuousLineDraw() || IsContinuousPolylineDraw() || IsSplineDraw())
            && !inputPoints_.empty())) {
        QPen previewPen(QColor(230, 230, 230), IsHardwareAccelerationActive() ? 2.0 : 1.6);
        previewPen.setCosmetic(true);
        previewPen.setStyle(Qt::SolidLine);
        previewPen.setCapStyle(Qt::RoundCap);
        previewPen.setJoinStyle(Qt::RoundJoin);
        previewItem_->setPen(previewPen);
        return;
    }

    // AutoCAD SER / SCL / CSE / CSA·SCA 夹角：实线圆弧预览。
    if (IsArcStartEndRadiusPick() || IsArcCenterStartEndEndPick() || IsArcChordLengthPick()
        || IsArcIncludedAnglePick() || IsArcStartCenterEndEndPick()) {
        QPen previewPen(QColor(230, 230, 230), IsHardwareAccelerationActive() ? 2.0 : 1.6);
        previewPen.setCosmetic(true);
        previewPen.setStyle(Qt::SolidLine);
        previewPen.setCapStyle(Qt::RoundCap);
        previewPen.setJoinStyle(Qt::RoundJoin);
        previewItem_->setPen(previewPen);
        return;
    }

    // AutoCAD FILLET / CHAMFER rubber-band: solid cyan result overlay.
    if (IsFilletChamferCommand()) {
        QPen previewPen(QColor(64, 180, 255), IsHardwareAccelerationActive() ? 2.0 : 1.6);
        previewPen.setCosmetic(true);
        previewPen.setStyle(Qt::SolidLine);
        previewPen.setCapStyle(Qt::RoundCap);
        previewPen.setJoinStyle(Qt::RoundJoin);
        previewItem_->setPen(previewPen);
        return;
    }

    QPen previewPen(QColor(64, 180, 255), IsHardwareAccelerationActive() ? 2.0 : 1.6);
    previewPen.setCosmetic(true);
    previewPen.setCapStyle(Qt::RoundCap);
    previewPen.setJoinStyle(Qt::RoundJoin);

    if (document_) {
        EntityStyle style = document_->CurrentEntityStyle();
        std::string lineTypeName = style.lineTypeName;
        if (style.lineTypeSource == PropertySource::ByLayer) {
            if (const Layer* layer = document_->FindLayer(document_->CurrentLayerName())) {
                lineTypeName = layer->LineTypeName();
            }
        } else if (style.lineTypeSource == PropertySource::ByBlock) {
            lineTypeName = "Continuous";
        }
        const double patternScale = std::max(
            0.0001,
            document_->GlobalLinetypeScale() * std::max(0.0001, style.lineTypeScale));
        if (const LineTypeDefinition* lineType = document_->FindLineType(lineTypeName)) {
            LineTypeLibrary::ApplyPatternToPen(
                previewPen, *lineType, patternScale, PixelsPerModelUnit());
        } else {
            previewPen.setStyle(Qt::DashLine);
        }
    } else {
        previewPen.setStyle(Qt::DashLine);
    }

    previewItem_->setPen(previewPen);
}

void CadView::SetPreviewPath(const QPainterPath& path)
{
    if (!scene()) {
        ClearPreview();
        return;
    }

    QRectF frameDirty;
    if (previewItem_ && previewItem_->scene() == scene()) {
        frameDirty = previewItem_->mapRectToScene(previewItem_->boundingRect());
    }

    AcquirePreviewItem();
    if (!previewItem_) {
        return;
    }

    ApplyPreviewPen();
    previewItem_->setPath(path);

    const QRectF newBounds = previewItem_->mapRectToScene(previewItem_->boundingRect());
    frameDirty = frameDirty.isValid() ? frameDirty.united(newBounds) : newBounds;
    const double margin = ModelToleranceFromPixels(8.0);
    if (frameDirty.isValid()) {
        previewDirtyRect_ = previewDirtyRect_.isValid()
            ? previewDirtyRect_.united(frameDirty)
            : frameDirty;
        const QRectF invalidRect = frameDirty.adjusted(-margin, -margin, margin, margin);
        scene()->invalidate(invalidRect, QGraphicsScene::AllLayers);
        ScheduleViewportRepaint(invalidRect);
        return;
    }
    ScheduleViewportRepaint();
}

void CadView::ClearPreviewDimLabel()
{
    if (!previewDimLabelItem_) {
        return;
    }

    const QRectF bounds = previewDimLabelItem_->mapRectToScene(previewDimLabelItem_->boundingRect());
    if (bounds.isValid()) {
        previewDirtyRect_ = previewDirtyRect_.isValid() ? previewDirtyRect_.united(bounds) : bounds;
    }
    if (previewDimLabelItem_->scene()) {
        previewDimLabelItem_->scene()->removeItem(previewDimLabelItem_);
    }
    delete previewDimLabelItem_;
    previewDimLabelItem_ = nullptr;
}

void CadView::SetPreviewDimLabel(const LinearDimensionLayout& layout, const DimensionStyle& style)
{
    if (!scene()) {
        ClearPreviewDimLabel();
        return;
    }

    QRectF frameDirty;
    if (previewDimLabelItem_ && previewDimLabelItem_->scene() == scene()) {
        frameDirty = previewDimLabelItem_->mapRectToScene(previewDimLabelItem_->boundingRect());
    }

    if (!previewDimLabelItem_ || previewDimLabelItem_->scene() != scene()) {
        ClearPreviewDimLabel();
        previewDimLabelItem_ = scene()->addSimpleText(QString());
        previewDimLabelItem_->setZValue(1001.0);
        previewDimLabelItem_->setCacheMode(QGraphicsItem::NoCache);
    }

    QFont font = previewDimLabelItem_->font();
    font.setPixelSize(qMax(1, qRound(std::max(1.0, style.textHeight))));
    previewDimLabelItem_->setFont(font);
    previewDimLabelItem_->setBrush(QColor(64, 180, 255));
    previewDimLabelItem_->setText(layout.label);

    const DimensionTextPreview textPreview = LinearDimensionTextPreview(layout, style);
    SetPreviewMeasureLabelPosition(
        previewDimLabelItem_,
        textPreview.center,
        textPreview.rotationDegrees);

    const QRectF newBounds = previewDimLabelItem_->mapRectToScene(previewDimLabelItem_->boundingRect());
    frameDirty = frameDirty.isValid() ? frameDirty.united(newBounds) : newBounds;
    const double margin = ModelToleranceFromPixels(8.0);
    if (frameDirty.isValid()) {
        previewDirtyRect_ = previewDirtyRect_.isValid()
            ? previewDirtyRect_.united(frameDirty)
            : frameDirty;
        const QRectF invalidRect = frameDirty.adjusted(-margin, -margin, margin, margin);
        scene()->invalidate(invalidRect, QGraphicsScene::AllLayers);
        ScheduleViewportRepaint(invalidRect);
        return;
    }
    ScheduleViewportRepaint();
}

void CadView::SetPreviewAlignedDimLabel(const AlignedDimensionLayout& layout, const DimensionStyle& style)
{
    if (!scene()) {
        ClearPreviewDimLabel();
        return;
    }

    QRectF frameDirty;
    if (previewDimLabelItem_ && previewDimLabelItem_->scene() == scene()) {
        frameDirty = previewDimLabelItem_->mapRectToScene(previewDimLabelItem_->boundingRect());
    }

    if (!previewDimLabelItem_ || previewDimLabelItem_->scene() != scene()) {
        ClearPreviewDimLabel();
        previewDimLabelItem_ = scene()->addSimpleText(QString());
        previewDimLabelItem_->setZValue(1001.0);
        previewDimLabelItem_->setCacheMode(QGraphicsItem::NoCache);
    }

    QFont font = previewDimLabelItem_->font();
    font.setPixelSize(qMax(1, qRound(std::max(1.0, style.textHeight))));
    previewDimLabelItem_->setFont(font);
    previewDimLabelItem_->setBrush(QColor(64, 180, 255));
    previewDimLabelItem_->setText(layout.label);

    const DimensionTextPreview textPreview = AlignedDimensionTextPreview(layout, style);
    SetPreviewMeasureLabelPosition(
        previewDimLabelItem_,
        textPreview.center,
        textPreview.rotationDegrees);

    const QRectF newBounds = previewDimLabelItem_->mapRectToScene(previewDimLabelItem_->boundingRect());
    frameDirty = frameDirty.isValid() ? frameDirty.united(newBounds) : newBounds;
    const double margin = ModelToleranceFromPixels(8.0);
    if (frameDirty.isValid()) {
        previewDirtyRect_ = previewDirtyRect_.isValid()
            ? previewDirtyRect_.united(frameDirty)
            : frameDirty;
        const QRectF invalidRect = frameDirty.adjusted(-margin, -margin, margin, margin);
        scene()->invalidate(invalidRect, QGraphicsScene::AllLayers);
        ScheduleViewportRepaint(invalidRect);
        return;
    }
    ScheduleViewportRepaint();
}

void CadView::SetPreviewRadiusDimLabel(const RadiusDimensionLayout& layout, const DimensionStyle& style)
{
    if (!scene()) {
        ClearPreviewDimLabel();
        return;
    }

    QRectF frameDirty;
    if (previewDimLabelItem_ && previewDimLabelItem_->scene() == scene()) {
        frameDirty = previewDimLabelItem_->mapRectToScene(previewDimLabelItem_->boundingRect());
    }

    if (!previewDimLabelItem_ || previewDimLabelItem_->scene() != scene()) {
        ClearPreviewDimLabel();
        previewDimLabelItem_ = scene()->addSimpleText(QString());
        previewDimLabelItem_->setZValue(1001.0);
        previewDimLabelItem_->setCacheMode(QGraphicsItem::NoCache);
    }

    QFont font = previewDimLabelItem_->font();
    font.setPixelSize(qMax(1, qRound(std::max(1.0, style.textHeight))));
    previewDimLabelItem_->setFont(font);
    previewDimLabelItem_->setBrush(QColor(64, 180, 255));
    previewDimLabelItem_->setText(layout.label);

    const DimensionTextPreview textPreview = RadiusDimensionTextPreview(layout, style);
    SetPreviewMeasureLabelPosition(
        previewDimLabelItem_,
        textPreview.center,
        textPreview.rotationDegrees);

    const QRectF newBounds = previewDimLabelItem_->mapRectToScene(previewDimLabelItem_->boundingRect());
    frameDirty = frameDirty.isValid() ? frameDirty.united(newBounds) : newBounds;
    const double margin = ModelToleranceFromPixels(8.0);
    if (frameDirty.isValid()) {
        previewDirtyRect_ = previewDirtyRect_.isValid()
            ? previewDirtyRect_.united(frameDirty)
            : frameDirty;
        const QRectF invalidRect = frameDirty.adjusted(-margin, -margin, margin, margin);
        scene()->invalidate(invalidRect, QGraphicsScene::AllLayers);
        ScheduleViewportRepaint(invalidRect);
        return;
    }
    ScheduleViewportRepaint();
}

void CadView::SetPreviewJoggedRadiusDimLabel(const JoggedRadiusDimensionLayout& layout, const DimensionStyle& style)
{
    if (!scene()) {
        ClearPreviewDimLabel();
        return;
    }

    QRectF frameDirty;
    if (previewDimLabelItem_ && previewDimLabelItem_->scene() == scene()) {
        frameDirty = previewDimLabelItem_->mapRectToScene(previewDimLabelItem_->boundingRect());
    }

    if (!previewDimLabelItem_ || previewDimLabelItem_->scene() != scene()) {
        ClearPreviewDimLabel();
        previewDimLabelItem_ = scene()->addSimpleText(QString());
        previewDimLabelItem_->setZValue(1001.0);
        previewDimLabelItem_->setCacheMode(QGraphicsItem::NoCache);
    }

    QFont font = previewDimLabelItem_->font();
    font.setPixelSize(qMax(1, qRound(std::max(1.0, style.textHeight))));
    previewDimLabelItem_->setFont(font);
    previewDimLabelItem_->setBrush(QColor(64, 180, 255));
    previewDimLabelItem_->setText(layout.label);

    const DimensionTextPreview textPreview = JoggedRadiusDimensionTextPreview(layout, style);
    SetPreviewMeasureLabelPosition(
        previewDimLabelItem_,
        textPreview.center,
        textPreview.rotationDegrees);

    const QRectF newBounds = previewDimLabelItem_->mapRectToScene(previewDimLabelItem_->boundingRect());
    frameDirty = frameDirty.isValid() ? frameDirty.united(newBounds) : newBounds;
    const double margin = ModelToleranceFromPixels(8.0);
    if (frameDirty.isValid()) {
        previewDirtyRect_ = previewDirtyRect_.isValid()
            ? previewDirtyRect_.united(frameDirty)
            : frameDirty;
        const QRectF invalidRect = frameDirty.adjusted(-margin, -margin, margin, margin);
        scene()->invalidate(invalidRect, QGraphicsScene::AllLayers);
        ScheduleViewportRepaint(invalidRect);
        return;
    }
    ScheduleViewportRepaint();
}

void CadView::SetPreviewDiameterDimLabel(const DiameterDimensionLayout& layout, const DimensionStyle& style)
{
    if (!scene()) {
        ClearPreviewDimLabel();
        return;
    }

    QRectF frameDirty;
    if (previewDimLabelItem_ && previewDimLabelItem_->scene() == scene()) {
        frameDirty = previewDimLabelItem_->mapRectToScene(previewDimLabelItem_->boundingRect());
    }

    if (!previewDimLabelItem_ || previewDimLabelItem_->scene() != scene()) {
        ClearPreviewDimLabel();
        previewDimLabelItem_ = scene()->addSimpleText(QString());
        previewDimLabelItem_->setZValue(1001.0);
        previewDimLabelItem_->setCacheMode(QGraphicsItem::NoCache);
    }

    QFont font = previewDimLabelItem_->font();
    font.setPixelSize(qMax(1, qRound(std::max(1.0, style.textHeight))));
    previewDimLabelItem_->setFont(font);
    previewDimLabelItem_->setBrush(QColor(64, 180, 255));
    previewDimLabelItem_->setText(layout.label);

    const DimensionTextPreview textPreview = DiameterDimensionTextPreview(layout, style);
    SetPreviewMeasureLabelPosition(
        previewDimLabelItem_,
        textPreview.center,
        textPreview.rotationDegrees);

    const QRectF newBounds = previewDimLabelItem_->mapRectToScene(previewDimLabelItem_->boundingRect());
    frameDirty = frameDirty.isValid() ? frameDirty.united(newBounds) : newBounds;
    const double margin = ModelToleranceFromPixels(8.0);
    if (frameDirty.isValid()) {
        previewDirtyRect_ = previewDirtyRect_.isValid()
            ? previewDirtyRect_.united(frameDirty)
            : frameDirty;
        const QRectF invalidRect = frameDirty.adjusted(-margin, -margin, margin, margin);
        scene()->invalidate(invalidRect, QGraphicsScene::AllLayers);
        ScheduleViewportRepaint(invalidRect);
        return;
    }
    ScheduleViewportRepaint();
}

void CadView::ClearPreview()
{
    RestoreFilletPreviewSources();
    const bool hadPreviewOverlay = previewItem_ != nullptr
        || previewDimLabelItem_ != nullptr
        || dimSnapEndpointItem_ != nullptr
        || dimSnapGuideItem_ != nullptr;

    ClearPreviewDimLabel();
    ClearDimensionSnapPreview();

    if (previewItem_) {
        const QRectF bounds = previewItem_->mapRectToScene(previewItem_->boundingRect());
        if (bounds.isValid()) {
            previewDirtyRect_ = previewDirtyRect_.isValid() ? previewDirtyRect_.united(bounds) : bounds;
        }
        if (previewItem_->scene()) {
            previewItem_->scene()->removeItem(previewItem_);
        }
        delete previewItem_;
        previewItem_ = nullptr;
    }

    if (scene() && previewDirtyRect_.isValid()) {
        const double margin = ModelToleranceFromPixels(16.0);
        scene()->invalidate(
            previewDirtyRect_.adjusted(-margin, -margin, margin, margin),
            QGraphicsScene::AllLayers);
        previewDirtyRect_ = QRectF();
    }

    // 预览清除后刷新整个视口，避免 drawForeground 十字线留下残影。
    if (hadPreviewOverlay) {
        RefreshViewport();
    }
}

void CadView::ResetTransientState()
{
    inputPoints_.clear();
    ClearLineChain();
    ResetPolylineDrawOptions();
    HideSplineGripHoverMenu();
    HidePolarInput();
    HideOptionInput();
    polygonPhase_ = (toolMode_ == ToolMode::DrawPolygon && drawOptions_.method == DrawMethod::PolygonEdge)
        ? PolygonCommandPhase::Edge
        : PolygonCommandPhase::Center;
    if (!pasteTextPending_) {
        HidePointInput();
    }
    ClearPolarGuidePreview();
    ResetDimLinearOptions();
    ResetDimAlignedOptions();
    ResetDimRadiusOptions();
    ResetDimAngularOptions();
    ResetDimOrdinateOptions();
    ClearPreview();
    ClearDimensionSnapPreview();
    ClearSnapMarker();
    ClearBoxSelectPreview();
    boxSelecting_ = false;
    RefreshViewport();
}

void CadView::CancelDrag()
{
    draggingSelection_ = false;
    dragMoved_ = false;
    boxSelecting_ = false;
    ClearBoxSelectPreview();
    if (cadScene_) {
        cadScene_->ClearSelectionDragOffset();
    }
}

void CadView::CancelGripDrag()
{
    CancelSplineGripEdit();
    draggingGrip_ = false;
    gripEntityId_ = 0;
    gripIndex_ = 0;
    gripDragGroup_.clear();
}

bool CadView::IsSplineFitGripEdit() const
{
    return splineGripEditing_ && splineGripKind_ != GripKind::ControlVertex;
}

QVector<CadView::SplineGripEditMode> CadView::CurrentSplineGripEditModes() const
{
    QVector<SplineGripEditMode> modes;
    modes.push_back(SplineGripEditMode::Stretch);
    modes.push_back(SplineGripEditMode::AddPoint);
    if (document_) {
        if (const Entity* entity = document_->FindEntity(gripEntityId_)) {
            if (entity->Type() == EntityType::Spline) {
                const auto& spline = static_cast<const SplineEntity*>(entity)->Spline();
                const std::size_t count = IsSplineFitGripEdit()
                    ? spline.FitPoints().size()
                    : spline.ControlPoints().size();
                if (count > 2) {
                    modes.push_back(SplineGripEditMode::RemovePoint);
                }
            }
        }
    }
    if (IsSplineFitGripEdit()) {
        const bool endPoint = gripIndex_ == 0;
        bool lastPoint = false;
        if (document_) {
            if (const Entity* entity = document_->FindEntity(gripEntityId_)) {
                if (entity->Type() == EntityType::Spline) {
                    const auto& pts = static_cast<const SplineEntity*>(entity)->Spline().FitPoints();
                    lastPoint = !pts.empty() && gripIndex_ == static_cast<int>(pts.size()) - 1;
                }
            }
        }
        if (endPoint || lastPoint) {
            modes.push_back(SplineGripEditMode::Tangent);
        }
    }
    return modes;
}

QString CadView::SplineGripEditPrompt() const
{
    switch (splineGripEditMode_) {
    case SplineGripEditMode::AddPoint:
        return QStringLiteral("指定新拟合点或");
    case SplineGripEditMode::RemovePoint:
        return QStringLiteral("确认删除拟合点或");
    case SplineGripEditMode::Tangent:
        return QStringLiteral("指定切向或");
    case SplineGripEditMode::Stretch:
    default:
        return QStringLiteral("指定拉伸点或");
    }
}

QVector<QString> CadView::SplineGripEditModeLabels() const
{
    const bool fit = IsSplineFitGripEdit();
    QVector<QString> labels;
    for (SplineGripEditMode mode : CurrentSplineGripEditModes()) {
        switch (mode) {
        case SplineGripEditMode::Stretch:
            labels.push_back(fit ? QStringLiteral("拉伸拟合点") : QStringLiteral("拉伸控制点"));
            break;
        case SplineGripEditMode::AddPoint:
            labels.push_back(fit ? QStringLiteral("添加拟合点") : QStringLiteral("添加控制点"));
            break;
        case SplineGripEditMode::RemovePoint:
            labels.push_back(fit ? QStringLiteral("删除拟合点") : QStringLiteral("删除控制点"));
            break;
        case SplineGripEditMode::Tangent:
            labels.push_back(QStringLiteral("相切方向"));
            break;
        }
    }
    return labels;
}

void CadView::CycleSplineGripEditMode()
{
    const QVector<SplineGripEditMode> modes = CurrentSplineGripEditModes();
    if (modes.size() < 2) {
        return;
    }
    int index = modes.indexOf(splineGripEditMode_);
    if (index < 0) {
        index = 0;
    }
    SetSplineGripEditMode(modes[(index + 1) % modes.size()]);
}

void CadView::SetSplineGripEditMode(SplineGripEditMode mode)
{
    const QVector<SplineGripEditMode> modes = CurrentSplineGripEditModes();
    if (!modes.contains(mode)) {
        mode = SplineGripEditMode::Stretch;
    }
    splineGripEditMode_ = mode;
    if (cadScene_ && mode != SplineGripEditMode::Stretch) {
        cadScene_->UpdateGripPosition(gripEntityId_, gripIndex_, gripStartPoint_);
    }
    if (viewport()) {
        const GeoPoint cursor = ApplySnap(MapToModel(lastCursorViewportPos_));
        GeoPoint polarBase = gripStartPoint_;
        const GeoPoint editPoint = ResolveDynamicLineEndpoint(polarBase, cursor);
        UpdateSplineGripEditPreview(editPoint);
        UpdateSplineGripEditHud(lastCursorViewportPos_, editPoint);
    }
}

void CadView::ApplySplineGripEditKeyword(const QString& keyword)
{
    const QString k = keyword.trimmed().toUpper();
    if (k == QLatin1String("S") || k.contains(QStringLiteral("拉伸"))) {
        SetSplineGripEditMode(SplineGripEditMode::Stretch);
    } else if (k == QLatin1String("A") || k.contains(QStringLiteral("添加"))) {
        SetSplineGripEditMode(SplineGripEditMode::AddPoint);
    } else if (k == QLatin1String("R") || k.contains(QStringLiteral("删除"))) {
        SetSplineGripEditMode(SplineGripEditMode::RemovePoint);
    } else if (k == QLatin1String("T") || k.contains(QStringLiteral("相切"))) {
        SetSplineGripEditMode(SplineGripEditMode::Tangent);
    }
}

void CadView::UpdateSplineGripEditHud(const QPoint& viewPos, const GeoPoint& cursorPoint)
{
    if (!IsSplineGripEditing()) {
        return;
    }
    EnsureOptionInput();
    QVector<DynCommandOption> options;
    const QVector<SplineGripEditMode> modes = CurrentSplineGripEditModes();
    const QVector<QString> labels = SplineGripEditModeLabels();
    for (int i = 0; i < modes.size(); ++i) {
        QString key = QStringLiteral("S");
        if (modes[i] == SplineGripEditMode::AddPoint) {
            key = QStringLiteral("A");
        } else if (modes[i] == SplineGripEditMode::RemovePoint) {
            key = QStringLiteral("R");
        } else if (modes[i] == SplineGripEditMode::Tangent) {
            key = QStringLiteral("T");
        }
        options.push_back({key, labels.value(i), true});
    }
    optionInput_->SetOptions(options);
    optionInput_->SetCycleList(
        SplineGripEditPrompt(),
        QStringLiteral("按 Ctrl 键在以下选项之间循环切换:"),
        labels,
        std::max(0, static_cast<int>(modes.indexOf(splineGripEditMode_))));
    optionInput_->MoveToViewPosition(viewPos);
    optionInput_->show();
    optionInput_->raise();

    if (splineGripEditMode_ == SplineGripEditMode::RemovePoint) {
        HidePolarInput();
        ClearPolarGuidePreview();
        return;
    }

    EnsurePolarInput();
    const GeoPoint end = ResolveDynamicLineEndpoint(gripStartPoint_, cursorPoint);
    const double dx = end.X() - gripStartPoint_.X();
    const double dy = end.Y() - gripStartPoint_.Y();
    const double length = std::hypot(dx, dy);
    const double angleRad = (length > Tolerance::Model) ? std::atan2(dy, dx) : 0.0;
    const double angleDeg = DrawAlgorithms::ToPositiveAngleDegrees(angleRad * kRadToDeg);
    polarInput_->SetTabHintVisible(false);
    polarInput_->SetPolarAnglePrefix(true);
    polarInput_->SetLengthVisible(true);
    polarInput_->SetAngleVisible(true);
    polarInput_->SetLengthAngle(length, angleDeg);
    if (optionInput_->isVisible()) {
        polarInput_->MoveToHorizontalRow(QPoint(
            optionInput_->x() + optionInput_->width() + 3,
            optionInput_->y()));
    } else {
        polarInput_->MoveToHorizontalRow(viewPos + QPoint(18, 28));
    }
    bool trackingSnapped = false;
    const double tracked = ApplyPolarTracking(angleRad, &trackingSnapped);
    UpdatePolarGuidePreview(gripStartPoint_, end, tracked, trackingSnapped, &cursorPoint);
    const double trackedDeg = DrawAlgorithms::ToPositiveAngleDegrees(tracked * kRadToDeg);
    const bool orthoPolar = trackingSnapped
        && (std::abs(std::fmod(trackedDeg, 90.0)) <= 0.51
            || std::abs(std::fmod(trackedDeg, 90.0) - 90.0) <= 0.51);
    if (orthoPolar && length > Tolerance::Model) {
        optionInput_->hide();
        polarInput_->hide();
        ShowDynPrompt(
            QStringLiteral("极轴: %1 < %2°")
                .arg(length, 0, 'f', 4)
                .arg(qRound(trackedDeg)),
            viewPos);
    } else {
        HideDynPrompt();
        optionInput_->show();
        optionInput_->raise();
        polarInput_->show();
        polarInput_->raise();
    }
}

void CadView::UpdateSplineGripEditPreview(const GeoPoint& cursorPoint)
{
    if (!document_ || !IsSplineGripEditing()) {
        return;
    }
    const Entity* entity = document_->FindEntity(gripEntityId_);
    if (!entity || entity->Type() != EntityType::Spline) {
        ClearPreview();
        return;
    }
    const GeoSpline preview = MakeSplineGripPreview(
        static_cast<const SplineEntity*>(entity)->Spline(), cursorPoint);
    GeoShape shape;
    shape.AddSpline(preview);
    if (preview.DisplayMode() == SplineMode::ControlPoints && preview.ControlPoints().size() >= 2) {
        shape.AddPolyline(preview.ControlPoints(), false);
    }
    SetPreviewPath(GeoShapePainter::BuildPath(shape));
}

GeoSpline CadView::MakeSplineGripPreview(const GeoSpline& source, const GeoPoint& cursorPoint) const
{
    GeoSpline preview = source;
    const bool fit = source.DisplayMode() == SplineMode::FitPoints;
    const GeoPoint editPoint = ResolveDynamicLineEndpoint(gripStartPoint_, cursorPoint);
    switch (splineGripEditMode_) {
    case SplineGripEditMode::AddPoint: {
        const std::size_t index = SplineInsertIndex(source, editPoint);
        if (fit) {
            preview.InsertFitPoint(index, editPoint);
        } else {
            preview.InsertControlPoint(index, editPoint);
        }
        break;
    }
    case SplineGripEditMode::RemovePoint:
        if (gripIndex_ >= 0) {
            if (fit) {
                preview.RemoveFitPoint(static_cast<std::size_t>(gripIndex_));
            } else {
                preview.RemoveControlPoint(static_cast<std::size_t>(gripIndex_));
            }
        }
        break;
    case SplineGripEditMode::Tangent:
        if (fit && gripIndex_ >= 0) {
            const auto& points = source.FitPoints();
            if (gripIndex_ == 0 && !points.empty()) {
                preview.SetStartTangent(GeoPoint(
                    editPoint.X() - points.front().X(),
                    editPoint.Y() - points.front().Y()));
            } else if (!points.empty() && gripIndex_ == static_cast<int>(points.size()) - 1) {
                preview.SetEndTangent(GeoPoint(
                    editPoint.X() - points.back().X(),
                    editPoint.Y() - points.back().Y()));
            }
        }
        break;
    case SplineGripEditMode::Stretch:
    default:
        if (gripIndex_ >= 0) {
            if (fit) {
                preview.SetFitPoint(static_cast<std::size_t>(gripIndex_), editPoint);
            } else {
                preview.SetControlPoint(static_cast<std::size_t>(gripIndex_), editPoint);
            }
        }
        break;
    }
    return preview;
}

std::size_t CadView::SplineInsertIndex(const GeoSpline& spline, const GeoPoint& point) const
{
    const bool fit = spline.DisplayMode() == SplineMode::FitPoints;
    const auto& points = fit ? spline.FitPoints() : spline.ControlPoints();
    if (points.empty()) {
        return 0;
    }

    // AutoCAD grip "Add Fit/Control Point": insert immediately after the
    // grip that started the command, at the specified location. Do not
    // project onto the existing curve — that would only add a point on
    // the original path instead of reshaping through the new pick.
    if (gripIndex_ >= 0) {
        const std::size_t after = static_cast<std::size_t>(gripIndex_) + 1;
        return std::min(after, points.size());
    }

    if (points.size() < 2) {
        return points.size();
    }
    if (!fit) {
        std::size_t best = 1;
        double bestDistance = std::numeric_limits<double>::max();
        for (std::size_t i = 1; i < points.size(); ++i) {
            const double distance = DistancePointToSegment(point, points[i - 1], points[i]);
            if (distance < bestDistance) {
                bestDistance = distance;
                best = i;
            }
        }
        return best;
    }

    const GeoPoint* startTangent = spline.StartTangent() ? &*spline.StartTangent() : nullptr;
    const GeoPoint* endTangent = spline.EndTangent() ? &*spline.EndTangent() : nullptr;
    const auto beziers = DrawAlgorithms::SplineCubicBeziers(
        spline.FitPoints(), SplineMode::FitPoints, spline.Degree(), spline.Knots(), startTangent, endTangent);
    if (beziers.empty()) {
        return points.size();
    }
    std::size_t bestSpan = 1;
    double bestDistance = std::numeric_limits<double>::max();
    for (std::size_t i = 0; i < beziers.size(); ++i) {
        const auto& bezier = beziers[i];
        for (int sample = 0; sample <= 16; ++sample) {
            const double u = static_cast<double>(sample) / 16.0;
            const double ou = 1.0 - u;
            const GeoPoint onCurve(
                ou * ou * ou * bezier.p0.X() + 3.0 * ou * ou * u * bezier.p1.X()
                    + 3.0 * ou * u * u * bezier.p2.X() + u * u * u * bezier.p3.X(),
                ou * ou * ou * bezier.p0.Y() + 3.0 * ou * ou * u * bezier.p1.Y()
                    + 3.0 * ou * u * u * bezier.p2.Y() + u * u * u * bezier.p3.Y());
            const double distance = Distance2D(point, onCurve);
            if (distance < bestDistance) {
                bestDistance = distance;
                bestSpan = i + 1;
            }
        }
    }
    return bestSpan;
}

bool CadView::CommitSplineGripEdit(const GeoPoint& point)
{
    if (!document_ || !splineGripEditing_) {
        return false;
    }
    Entity* entity = document_->FindEntity(gripEntityId_);
    if (!entity || entity->Type() != EntityType::Spline) {
        CancelSplineGripEdit();
        return false;
    }
    auto& spline = static_cast<SplineEntity*>(entity)->Spline();
    const bool fit = spline.DisplayMode() == SplineMode::FitPoints;
    const GeoPoint editPoint = ResolveDynamicLineEndpoint(gripStartPoint_, point);
    switch (splineGripEditMode_) {
    case SplineGripEditMode::AddPoint: {
        const std::size_t index = SplineInsertIndex(spline, editPoint);
        if (fit) {
            spline.InsertFitPoint(index, editPoint);
        } else {
            spline.InsertControlPoint(index, editPoint);
        }
        break;
    }
    case SplineGripEditMode::RemovePoint:
        if (gripIndex_ >= 0) {
            if (fit) {
                spline.RemoveFitPoint(static_cast<std::size_t>(gripIndex_));
            } else {
                spline.RemoveControlPoint(static_cast<std::size_t>(gripIndex_));
            }
        }
        break;
    case SplineGripEditMode::Tangent:
        if (fit && gripIndex_ >= 0) {
            const auto& points = spline.FitPoints();
            if (gripIndex_ == 0 && !points.empty()) {
                spline.SetStartTangent(GeoPoint(
                    editPoint.X() - points.front().X(),
                    editPoint.Y() - points.front().Y()));
            } else if (!points.empty() && gripIndex_ == static_cast<int>(points.size()) - 1) {
                spline.SetEndTangent(GeoPoint(
                    editPoint.X() - points.back().X(),
                    editPoint.Y() - points.back().Y()));
            }
        }
        break;
    case SplineGripEditMode::Stretch:
    default:
        entity->SetGrip(gripIndex_, editPoint);
        break;
    }

    const EntityId id = gripEntityId_;
    splineGripEditing_ = false;
    splineGripAwaitingClick_ = false;
    draggingGrip_ = false;
    gripDragGroup_.clear();
    document_->NotifyEntitiesUpdated({id});
    if (cadScene_) {
        cadScene_->ClearHotGrips();
        cadScene_->RebuildGrips();
        cadScene_->InvalidateItemRegions();
    }
    ClearPreview();
    HideOptionInput();
    HidePolarInput();
    ClearPolarGuidePreview();
    EmitStatus(QStringLiteral("样条已更新"));
    return true;
}

void CadView::EnsureSplineGripHoverMenu()
{
    if (gripHoverMenu_) {
        return;
    }
    gripHoverMenu_ = new GripHoverMenuWidget(viewport());
    gripHoverMenu_->hide();
    gripHoverMenu_->onOptionChosen = [this](const QString& keyword) {
        if (gripHoverHideTimer_) {
            gripHoverHideTimer_->stop();
        }
        const EntityId entityId = hoverGripEntityId_;
        const int gripIndex = hoverGripIndex_;
        HideSplineGripHoverMenu();
        if (!document_ || entityId == 0) {
            return;
        }
        const Entity* entity = document_->FindEntity(entityId);
        if (!entity || entity->Type() != EntityType::Spline) {
            return;
        }
        GripKind kind = GripKind::Vertex;
        for (const GripInfo& info : entity->Grips()) {
            if (info.index == gripIndex) {
                kind = info.kind;
                break;
            }
        }
        SplineGripEditMode mode = SplineGripEditMode::Stretch;
        const QString k = keyword.trimmed().toUpper();
        if (k == QLatin1String("A")) {
            mode = SplineGripEditMode::AddPoint;
        } else if (k == QLatin1String("R")) {
            mode = SplineGripEditMode::RemovePoint;
        } else if (k == QLatin1String("T")) {
            mode = SplineGripEditMode::Tangent;
        }
        BeginSplineGripEdit(entityId, gripIndex, kind, mode, false);
        if (mode == SplineGripEditMode::RemovePoint) {
            CommitSplineGripEdit(gripStartPoint_);
        }
        setFocus(Qt::OtherFocusReason);
    };
    gripHoverMenu_->onEntered = [this]() {
        if (gripHoverHideTimer_) {
            gripHoverHideTimer_->stop();
        }
    };
    gripHoverMenu_->onLeft = [this]() {
        if (!gripHoverHideTimer_) {
            gripHoverHideTimer_ = new QTimer(this);
            gripHoverHideTimer_->setSingleShot(true);
            connect(gripHoverHideTimer_, &QTimer::timeout, this, [this]() {
                HideSplineGripHoverMenu();
                if (!splineGripEditing_ && cadScene_) {
                    cadScene_->ClearHotGrips();
                }
            });
        }
        gripHoverHideTimer_->start(220);
    };
}

void CadView::HideSplineGripHoverMenu()
{
    if (gripHoverHideTimer_) {
        gripHoverHideTimer_->stop();
    }
    if (gripHoverMenu_) {
        gripHoverMenu_->hide();
    }
    hoverGripEntityId_ = 0;
    hoverGripIndex_ = -1;
}

QVector<DynCommandOption> CadView::SplineGripHoverOptions(
    const Entity& entity, int gripIndex, GripKind kind) const
{
    QVector<DynCommandOption> options;
    if (entity.Type() != EntityType::Spline) {
        return options;
    }
    const auto& spline = static_cast<const SplineEntity&>(entity).Spline();
    const bool fit = kind != GripKind::ControlVertex;
    options.push_back({
        QStringLiteral("S"),
        fit ? QStringLiteral("拉伸拟合点(S)") : QStringLiteral("拉伸控制点(S)"),
        true});
    options.push_back({
        QStringLiteral("A"),
        fit ? QStringLiteral("添加拟合点(A)") : QStringLiteral("添加控制点(A)"),
        true});
    const std::size_t count = fit ? spline.FitPoints().size() : spline.ControlPoints().size();
    options.push_back({
        QStringLiteral("R"),
        fit ? QStringLiteral("删除拟合点(R)") : QStringLiteral("删除控制点(R)"),
        count > 2});
    if (fit) {
        const auto& points = spline.FitPoints();
        const bool endPoint = gripIndex == 0
            || (!points.empty() && gripIndex == static_cast<int>(points.size()) - 1);
        options.push_back({QStringLiteral("T"), QStringLiteral("相切方向(T)"), endPoint});
    }
    return options;
}

void CadView::UpdateSplineGripHover(const QPoint& viewPos, const GeoPoint& rawPoint)
{
    if (!cadScene_ || !document_ || toolMode_ != ToolMode::Select || splineGripEditing_) {
        return;
    }
    const double gripTolerance = ModelToleranceFromPixels(Tolerance::GripTolerancePixels);
    GripItem* grip = cadScene_->HitGrip(ToScenePoint(rawPoint), gripTolerance);
    if (grip
        && (grip->Kind() == GripKind::Vertex || grip->Kind() == GripKind::ControlVertex)) {
        const Entity* entity = document_->FindEntity(grip->EntityIdValue());
        if (entity && entity->Type() == EntityType::Spline && entity->IsSelected()) {
            if (gripHoverHideTimer_) {
                gripHoverHideTimer_->stop();
            }
            cadScene_->SetHotGrip(grip->EntityIdValue(), grip->GripIndex());
            EnsureSplineGripHoverMenu();
            if (hoverGripEntityId_ != grip->EntityIdValue()
                || hoverGripIndex_ != grip->GripIndex()
                || !gripHoverMenu_->isVisible()) {
                hoverGripEntityId_ = grip->EntityIdValue();
                hoverGripIndex_ = grip->GripIndex();
                gripHoverMenu_->SetItems(
                    SplineGripHoverOptions(*entity, grip->GripIndex(), grip->Kind()));
                gripHoverMenu_->MoveNearCursor(viewPos);
            }
            gripHoverMenu_->show();
            gripHoverMenu_->raise();
            return;
        }
    }

    if (gripHoverMenu_ && gripHoverMenu_->isVisible()
        && gripHoverMenu_->ContainsGlobalPos(QCursor::pos())) {
        return;
    }
    if (gripHoverMenu_ && gripHoverMenu_->isVisible()) {
        if (!gripHoverHideTimer_) {
            gripHoverHideTimer_ = new QTimer(this);
            gripHoverHideTimer_->setSingleShot(true);
            connect(gripHoverHideTimer_, &QTimer::timeout, this, [this]() {
                HideSplineGripHoverMenu();
                if (!splineGripEditing_ && cadScene_) {
                    cadScene_->ClearHotGrips();
                }
            });
        }
        if (!gripHoverHideTimer_->isActive()) {
            gripHoverHideTimer_->start(220);
        }
        return;
    }
    HideSplineGripHoverMenu();
    cadScene_->ClearHotGrips();
}

void CadView::BeginSplineGripEdit(
    EntityId entityId,
    int gripIndex,
    GripKind kind,
    SplineGripEditMode mode,
    bool startDrag)
{
    if (!document_ || !cadScene_) {
        return;
    }
    HideSplineGripHoverMenu();
    Entity* entity = document_->FindEntity(entityId);
    if (!entity || entity->Type() != EntityType::Spline) {
        return;
    }

    draggingGrip_ = startDrag;
    gripEntityId_ = entityId;
    gripIndex_ = gripIndex;
    gripDragGroup_.clear();
    splineGripEditing_ = true;
    splineGripAwaitingClick_ = !startDrag;
    splineGripEditMode_ = mode;
    splineGripKind_ = kind;

    GeoPoint anchor;
    bool found = false;
    for (const GripInfo& info : entity->Grips()) {
        if (info.index == gripIndex) {
            anchor = info.position;
            found = true;
            break;
        }
    }
    if (!found) {
        splineGripEditing_ = false;
        draggingGrip_ = false;
        return;
    }
    gripStartPoint_ = anchor;
    if (kind == GripKind::Vertex) {
        gripDragGroup_.emplace_back(entityId, gripIndex);
    } else {
        gripDragGroup_.emplace_back(entityId, gripIndex);
    }
    cadScene_->SetHotGrip(entityId, gripIndex);
    UpdateSplineGripEditPreview(anchor);
    if (viewport()) {
        UpdateSplineGripEditHud(lastCursorViewportPos_, anchor);
    }
    EmitStatus(SplineGripEditPrompt());
}

void CadView::CancelSplineGripEdit()
{
    HideSplineGripHoverMenu();
    const bool hadSession = splineGripEditing_ || splineGripAwaitingClick_;
    splineGripEditing_ = false;
    splineGripAwaitingClick_ = false;
    splineGripEditMode_ = SplineGripEditMode::Stretch;
    splineGripKind_ = GripKind::Vertex;
    if (!hadSession) {
        return;
    }
    draggingGrip_ = false;
    gripDragGroup_.clear();
    if (cadScene_) {
        cadScene_->ClearHotGrips();
        cadScene_->RebuildGrips();
        cadScene_->InvalidateItemRegions();
    }
    ClearPreview();
    HideOptionInput();
    HidePolarInput();
    ClearPolarGuidePreview();
}

void CadView::EmitStatus(const QString& text)
{
    if (statusCallback_) {
        statusCallback_(text);
    }
}

bool CadView::IsDrawTool(ToolMode mode) const
{
    switch (mode) {
    case ToolMode::DrawPoint:
    case ToolMode::DrawLine:
    case ToolMode::DrawRay:
    case ToolMode::DrawXLine:
    case ToolMode::DrawRectangle:
    case ToolMode::DrawPolygon:
    case ToolMode::DrawCircle:
    case ToolMode::DrawArc:
    case ToolMode::DrawEllipse:
    case ToolMode::DrawParabola:
    case ToolMode::DrawHyperbola:
    case ToolMode::DrawSpline:
    case ToolMode::DrawPolyline:
    case ToolMode::DrawSolidFill:
    case ToolMode::DrawHatchFill:
        return true;
    default:
        return false;
    }
}

void CadView::HandleTextToolPress(const GeoPoint& point)
{
    if (!document_ || !commandManager_) {
        return;
    }

    SingleLineTextEditorDialog dialog(*document_, this);
    SingleLineTextOptions options;
    options.height = lastTextHeight_;
    if (dialog.exec() != QDialog::Accepted) {
        EmitStatus(QStringLiteral("单行文字：已取消"));
        return;
    }

    options = dialog.Options();
    if (options.text.empty()) {
        EmitStatus(QStringLiteral("单行文字：内容不能为空"));
        return;
    }

    lastTextHeight_ = options.height;
    auto entity = std::make_unique<TextEntity>(
        IdGenerator::Next(),
        point,
        options.text,
        options.height,
        qDegreesToRadians(options.rotationDegrees),
        options.horizontalAlign,
        options.verticalAlign,
        options.widthFactor,
        qDegreesToRadians(options.obliqueDegrees));
    entity->Style() = options.style;
    entity->Style().textStyle = options.textStyle;
    commandManager_->AddEntity(std::move(entity));
    EmitStatus(QStringLiteral("单行文字已创建；继续指定插入点，Esc 返回选择"));
}

void CadView::HandleMTextToolPress(const GeoPoint& point)
{
    if (!document_ || !commandManager_) {
        return;
    }

    GeoPoint insertionPoint = point;
    double referenceWidth = 0.0;
    MTextAttachment cornerWidthAttachment = MTextAttachment::TopLeft;
    bool useCornerWidthAttachment = false;

    if (drawOptions_.method == DrawMethod::MTextCornerWidth) {
        if (inputPoints_.empty()) {
            inputPoints_.push_back(point);
            EmitStatus(QStringLiteral("多行文字：指定对角点以定义文本宽度"));
            return;
        }
        const MTextCornerPlacement placement = ResolveMTextCornerPlacement(inputPoints_.front(), point);
        insertionPoint = placement.insertionPoint;
        referenceWidth = placement.referenceWidth;
        cornerWidthAttachment = placement.attachment;
        useCornerWidthAttachment = true;
        ResetTransientState();
    }

    MTextEditorDialog dialog(*document_, this);
    MTextEditorOptions options;
    options.height = lastTextHeight_;
    options.referenceWidth = referenceWidth;
    if (dialog.exec() != QDialog::Accepted) {
        EmitStatus(QStringLiteral("多行文字：已取消"));
        return;
    }

    options = dialog.Options();
    if (options.content.empty()) {
        EmitStatus(QStringLiteral("多行文字：内容不能为空"));
        return;
    }

    lastTextHeight_ = options.height;
    if (options.referenceWidth <= 0.0 && referenceWidth > 0.0) {
        options.referenceWidth = referenceWidth;
    }
    if (useCornerWidthAttachment) {
        options.attachment = cornerWidthAttachment;
    }

    auto entity = std::make_unique<MTextEntity>(
        IdGenerator::Next(),
        insertionPoint,
        options.content,
        options.height,
        qDegreesToRadians(options.rotationDegrees),
        options.referenceWidth,
        options.lineSpacingFactor,
        options.attachment,
        options.paragraphAlign,
        options.backgroundMask);
    entity->Style() = options.style;
    entity->Style().textStyle = options.textStyle;
    commandManager_->AddEntity(std::move(entity));
    EmitStatus(QStringLiteral("多行文字已创建；继续指定插入点，Esc 返回选择"));
}

void CadView::ResetDimLinearOptions()
{
    dimLinearOrientationOverride_.reset();
    dimLinearOverrideText_.clear();
    dimLinearTextRotationRadians_ = 0.0;
}

void CadView::PromptDimLinearTextEdit(const bool multiline)
{
    if (!document_ || inputPoints_.size() < 2) {
        return;
    }

    DimensionStyle dimStyle;
    if (!document_->DimensionStyles().empty()) {
        dimStyle = document_->DimensionStyles().front();
    }
    const LinearDimensionLayout layout = BuildLinearDimensionLayout(
        inputPoints_[0],
        inputPoints_[1],
        inputPoints_[1],
        dimStyle,
        dimLinearOrientationOverride_,
        dimLinearOverrideText_,
        dimLinearTextRotationRadians_);

    if (multiline) {
        MTextEditorDialog dialog(*document_, this);
        MTextEditorOptions options;
        options.content = dimLinearOverrideText_.empty()
            ? layout.label.toUtf8().constData()
            : dimLinearOverrideText_;
        dialog.SetOptions(options);
        if (dialog.exec() != QDialog::Accepted) {
            return;
        }
        dimLinearOverrideText_ = dialog.Options().content;
    } else {
        bool ok = false;
        const QString current = dimLinearOverrideText_.empty()
            ? layout.label
            : QString::fromUtf8(dimLinearOverrideText_.c_str());
        const QString text = QInputDialog::getText(
            this,
            QStringLiteral("标注文字"),
            QStringLiteral("文字(T)："),
            QLineEdit::Normal,
            current,
            &ok);
        if (!ok) {
            return;
        }
        dimLinearOverrideText_ = text.toUtf8().constData();
    }

    EmitStatus(QStringLiteral("线性标注：已设置标注文字；指定尺寸线位置"));
    UpdatePreview(dragCurrentPoint_);
}

void CadView::PromptDimLinearTextAngle()
{
    if (inputPoints_.size() < 2) {
        return;
    }

    bool ok = false;
    const double currentDegrees = qRadiansToDegrees(dimLinearTextRotationRadians_);
    const double angle = QInputDialog::getDouble(
        this,
        QStringLiteral("标注文字角度"),
        QStringLiteral("角度(A)（度）："),
        currentDegrees,
        -360.0,
        360.0,
        1,
        &ok);
    if (!ok) {
        return;
    }

    dimLinearTextRotationRadians_ = qDegreesToRadians(angle);
    EmitStatus(QStringLiteral("线性标注：已设置文字角度；指定尺寸线位置"));
    UpdatePreview(dragCurrentPoint_);
}

void CadView::ToggleDimLinearOrientation()
{
    if (inputPoints_.size() < 2) {
        return;
    }

    if (!dimLinearOrientationOverride_.has_value()) {
        DimensionStyle dimStyle;
        if (document_ && !document_->DimensionStyles().empty()) {
            dimStyle = document_->DimensionStyles().front();
        }
        dimLinearOrientationOverride_ = ResolveLinearOrientation(
            inputPoints_[0],
            inputPoints_[1],
            dragCurrentPoint_,
            std::nullopt);
    }

    dimLinearOrientationOverride_ = dimLinearOrientationOverride_ == LinearDimensionOrientation::Horizontal
        ? LinearDimensionOrientation::Vertical
        : LinearDimensionOrientation::Horizontal;

    const QString orientationText = dimLinearOrientationOverride_ == LinearDimensionOrientation::Horizontal
        ? QStringLiteral("水平")
        : QStringLiteral("垂直");
    EmitStatus(QStringLiteral("线性标注：已切换为%1标注；指定尺寸线位置").arg(orientationText));
    UpdatePreview(dragCurrentPoint_);
}

void CadView::ResetDimOrdinateOptions()
{
    dimOrdinateOrientationOverride_.reset();
    dimOrdinateOverrideText_.clear();
    dimOrdinateTextRotationRadians_ = 0.0;
}

void CadView::PromptDimOrdinateTextEdit(const bool multiline)
{
    if (!document_ || inputPoints_.size() != 1) {
        return;
    }

    const DimensionStyle dimStyle = ActiveDimensionStyle();
    const GeoPoint ucsOrigin(0.0, 0.0);
    const OrdinateDimensionLayout layout = BuildOrdinateDimensionLayout(
        inputPoints_[0],
        dragCurrentPoint_,
        ucsOrigin,
        dimStyle,
        dimOrdinateOrientationOverride_,
        dimOrdinateOverrideText_,
        dimOrdinateTextRotationRadians_);

    if (multiline) {
        MTextEditorDialog dialog(*document_, this);
        MTextEditorOptions options;
        options.content = dimOrdinateOverrideText_.empty()
            ? layout.label.toUtf8().constData()
            : dimOrdinateOverrideText_;
        dialog.SetOptions(options);
        if (dialog.exec() != QDialog::Accepted) {
            return;
        }
        dimOrdinateOverrideText_ = dialog.Options().content;
    } else {
        bool ok = false;
        const QString current = dimOrdinateOverrideText_.empty()
            ? layout.label
            : QString::fromUtf8(dimOrdinateOverrideText_.c_str());
        const QString text = QInputDialog::getText(
            this,
            QStringLiteral("标注文字"),
            QStringLiteral("文字(T)："),
            QLineEdit::Normal,
            current,
            &ok);
        if (!ok) {
            return;
        }
        dimOrdinateOverrideText_ = text.toUtf8().constData();
    }

    EmitStatus(QStringLiteral("坐标标注：已设置标注文字；指定引线位置"));
    UpdatePreview(dragCurrentPoint_);
}

void CadView::PromptDimOrdinateTextAngle()
{
    if (inputPoints_.size() != 1) {
        return;
    }

    bool ok = false;
    const double currentDegrees = qRadiansToDegrees(dimOrdinateTextRotationRadians_);
    const double angle = QInputDialog::getDouble(
        this,
        QStringLiteral("标注文字角度"),
        QStringLiteral("角度(A)（度）："),
        currentDegrees,
        -360.0,
        360.0,
        1,
        &ok);
    if (!ok) {
        return;
    }

    dimOrdinateTextRotationRadians_ = qDegreesToRadians(angle);
    EmitStatus(QStringLiteral("坐标标注：已设置文字角度；指定引线位置"));
    UpdatePreview(dragCurrentPoint_);
}

void CadView::ToggleDimOrdinateOrientation()
{
    if (inputPoints_.size() != 1) {
        return;
    }

    if (!dimOrdinateOrientationOverride_.has_value()) {
        dimOrdinateOrientationOverride_ = ResolveOrdinateOrientation(
            inputPoints_[0],
            dragCurrentPoint_,
            std::nullopt);
    }

    dimOrdinateOrientationOverride_ = dimOrdinateOrientationOverride_ == LinearDimensionOrientation::Horizontal
        ? LinearDimensionOrientation::Vertical
        : LinearDimensionOrientation::Horizontal;

    const QString axisText = dimOrdinateOrientationOverride_ == LinearDimensionOrientation::Vertical
        ? QStringLiteral("X")
        : QStringLiteral("Y");
    EmitStatus(QStringLiteral("坐标标注：已切换为 %1 坐标标注；指定引线位置").arg(axisText));
    UpdatePreview(dragCurrentPoint_);
}

void CadView::ResetDimAlignedOptions()
{
    dimAlignedOverrideText_.clear();
    dimAlignedTextRotationRadians_ = 0.0;
}

void CadView::ClearDimensionSnapPreview()
{
    auto clearItem = [&](QGraphicsPathItem*& item) {
        if (!item) {
            return;
        }
        const QRectF bounds = item->mapRectToScene(item->boundingRect());
        if (bounds.isValid()) {
            previewDirtyRect_ = previewDirtyRect_.isValid() ? previewDirtyRect_.united(bounds) : bounds;
        }
        if (item->scene()) {
            item->scene()->removeItem(item);
        }
        delete item;
        item = nullptr;
    };

    clearItem(dimSnapEndpointItem_);
    clearItem(dimSnapGuideItem_);
}

void CadView::SetDimensionSnapGuidePaths(const QPainterPath& endpointPath, const QPainterPath& guidePath)
{
    if (!scene()) {
        ClearDimensionSnapPreview();
        return;
    }

    auto ensureItem = [&](QGraphicsPathItem*& item, const QPainterPath& path, const QPen& pen, double zValue) {
        QRectF frameDirty;
        if (item && item->scene() == scene()) {
            frameDirty = item->mapRectToScene(item->boundingRect());
        }

        if (path.isEmpty()) {
            if (item) {
                if (item->scene()) {
                    item->scene()->removeItem(item);
                }
                delete item;
                item = nullptr;
            }
            return;
        }

        if (!item || item->scene() != scene()) {
            if (item) {
                delete item;
            }
            item = scene()->addPath(path, pen);
            item->setZValue(zValue);
            item->setCacheMode(QGraphicsItem::NoCache);
        } else {
            item->setPen(pen);
            item->setPath(path);
        }

        const QRectF newBounds = item->mapRectToScene(item->boundingRect());
        frameDirty = frameDirty.isValid() ? frameDirty.united(newBounds) : newBounds;
        if (frameDirty.isValid()) {
            previewDirtyRect_ = previewDirtyRect_.isValid()
                ? previewDirtyRect_.united(frameDirty)
                : frameDirty;
            const double margin = ModelToleranceFromPixels(8.0);
            scene()->invalidate(frameDirty.adjusted(-margin, -margin, margin, margin), QGraphicsScene::AllLayers);
        }
    };

    QPen endpointPen(QColor(96, 220, 128));
    endpointPen.setCosmetic(true);
    endpointPen.setWidthF(0.0);

    QPen guidePen(QColor(255, 220, 96, 190));
    guidePen.setCosmetic(true);
    guidePen.setStyle(Qt::DashLine);
    guidePen.setWidthF(0.0);

    ensureItem(dimSnapEndpointItem_, endpointPath, endpointPen, 1498.0);
    ensureItem(dimSnapGuideItem_, guidePath, guidePen, 1497.0);
    ScheduleViewportRepaint(previewDirtyRect_);
}

bool CadView::IsDimensionDefPointPickActive() const
{
    // Linear/aligned use AutoCAD-like def-point helpers (endpoint squares + extension).
    // Ordinate uses full object snap (CEN/INT/END/MID/...), same as drawing commands.
    return (toolMode_ == ToolMode::DrawDimLinear || toolMode_ == ToolMode::DrawDimAligned)
        && inputPoints_.size() < 2;
}

std::optional<GeoPoint> CadView::DimensionSnapPointAt(const GeoPoint& point) const
{
    if (!document_ || !IsDimensionDefPointPickActive()) {
        return std::nullopt;
    }

    const double pickTolerance = ModelToleranceFromPixels(snapTolerancePixels_ * 3.0);
    const QRectF visibleModelRect = VisibleModelRect(snapTolerancePixels_);
    const std::optional<DimensionSnapHint> hint = ComputeDimensionSnapHint(
        *document_,
        point,
        pickTolerance,
        visibleModelRect,
        [this](const Entity& entity, const QRectF& rect, const double tolerance) {
            return EntityIntersectsVisibleRect(entity, rect, tolerance);
        });
    if (!hint) {
        return std::nullopt;
    }
    return hint->snapPoint;
}

void CadView::UpdateDimensionSnapPreview(const GeoPoint& rawPoint)
{
    if (!scene() || !document_ || !IsDimensionDefPointPickActive()) {
        ClearDimensionSnapPreview();
        return;
    }

    const double pickTolerance = ModelToleranceFromPixels(snapTolerancePixels_ * 3.0);
    const QRectF visibleModelRect = VisibleModelRect(snapTolerancePixels_);
    const std::optional<DimensionSnapHint> hint = ComputeDimensionSnapHint(
        *document_,
        rawPoint,
        pickTolerance,
        visibleModelRect,
        [this](const Entity& entity, const QRectF& rect, const double tolerance) {
            return EntityIntersectsVisibleRect(entity, rect, tolerance);
        });

    if (!hint) {
        ClearDimensionSnapPreview();
        return;
    }

    const double markerHalf = ModelToleranceFromPixels(4.0);
    const double activeHalf = ModelToleranceFromPixels(5.5);
    const double guideExtent = std::max(visibleModelRect.width(), visibleModelRect.height()) * 1.5;

    const GeoPoint axis(
        hint->endpointB.X() - hint->endpointA.X(),
        hint->endpointB.Y() - hint->endpointA.Y());
    const double axisLength = std::hypot(axis.X(), axis.Y());
    const GeoPoint axisMid(
        (hint->endpointA.X() + hint->endpointB.X()) * 0.5,
        (hint->endpointA.Y() + hint->endpointB.Y()) * 0.5);

    const auto toScene = [this](const GeoPoint& modelPoint) {
        return ToScenePoint(modelPoint);
    };

    QPainterPath endpointPath;
    AppendEndpointSquare(
        endpointPath,
        toScene(hint->endpointA),
        hint->snapIsEndpointA ? activeHalf : markerHalf);
    AppendEndpointSquare(
        endpointPath,
        toScene(hint->endpointB),
        hint->snapIsEndpointA ? markerHalf : activeHalf);

    QPainterPath guidePath;
    if (axisLength > Tolerance::Model) {
        AppendBidirectionalModelLine(guidePath, axisMid, axis, guideExtent, toScene);
    }

    SetDimensionSnapGuidePaths(endpointPath, guidePath);
}

GeoPoint CadView::ResolveDimensionDefPoint(const GeoPoint& point) const
{
    if (const std::optional<ObjectSnapHit> objectSnap = FindObjectSnapHit(point)) {
        return objectSnap->point;
    }
    if (const std::optional<GeoPoint> snapPoint = DimensionSnapPointAt(point)) {
        return *snapPoint;
    }
    return point;
}

void CadView::PromptDimAlignedTextEdit(const bool multiline)
{
    if (!document_ || inputPoints_.size() < 2) {
        return;
    }

    DimensionStyle dimStyle;
    if (!document_->DimensionStyles().empty()) {
        dimStyle = document_->DimensionStyles().front();
    }
    const AlignedDimensionLayout layout = BuildAlignedDimensionLayout(
        inputPoints_[0],
        inputPoints_[1],
        inputPoints_[1],
        dimStyle,
        dimAlignedOverrideText_,
        dimAlignedTextRotationRadians_);

    if (multiline) {
        MTextEditorDialog dialog(*document_, this);
        MTextEditorOptions options;
        options.content = dimAlignedOverrideText_.empty()
            ? layout.label.toUtf8().constData()
            : dimAlignedOverrideText_;
        dialog.SetOptions(options);
        if (dialog.exec() != QDialog::Accepted) {
            return;
        }
        dimAlignedOverrideText_ = dialog.Options().content;
    } else {
        bool ok = false;
        const QString current = dimAlignedOverrideText_.empty()
            ? layout.label
            : QString::fromUtf8(dimAlignedOverrideText_.c_str());
        const QString text = QInputDialog::getText(
            this,
            QStringLiteral("标注文字"),
            QStringLiteral("文字(T)："),
            QLineEdit::Normal,
            current,
            &ok);
        if (!ok) {
            return;
        }
        dimAlignedOverrideText_ = text.toUtf8().constData();
    }

    EmitStatus(QStringLiteral("对齐标注：已设置标注文字；指定尺寸线位置"));
    UpdatePreview(dragCurrentPoint_);
}

void CadView::PromptDimAlignedTextAngle()
{
    if (inputPoints_.size() < 2) {
        return;
    }

    bool ok = false;
    const double currentDegrees = qRadiansToDegrees(dimAlignedTextRotationRadians_);
    const double angle = QInputDialog::getDouble(
        this,
        QStringLiteral("标注文字角度"),
        QStringLiteral("角度(A)（度）："),
        currentDegrees,
        -360.0,
        360.0,
        1,
        &ok);
    if (!ok) {
        return;
    }

    dimAlignedTextRotationRadians_ = qDegreesToRadians(angle);
    EmitStatus(QStringLiteral("对齐标注：已设置文字角度；指定尺寸线位置"));
    UpdatePreview(dragCurrentPoint_);
}

void CadView::HandleDimAlignedPress(const GeoPoint& point)
{
    if (!document_ || !commandManager_) {
        return;
    }

    if (inputPoints_.empty()) {
        inputPoints_.push_back(ResolveDimensionDefPoint(point));
        EmitStatus(QStringLiteral("对齐标注：指定第二条尺寸界线原点"));
        return;
    }

    if (inputPoints_.size() == 1) {
        const GeoPoint second = ResolveDimensionDefPoint(point);
        if (Distance2D(inputPoints_.front(), second) <= Tolerance::Model) {
            EmitStatus(QStringLiteral("对齐标注：两点不能重合"));
            return;
        }
        inputPoints_.push_back(second);
        dragCurrentPoint_ = second;
        setFocus(Qt::MouseFocusReason);
        EmitStatus(QStringLiteral("对齐标注：指定尺寸线位置或 [多行文字(M)/文字(T)/角度(A)]"));
        return;
    }

    const GeoPoint& first = inputPoints_[0];
    const GeoPoint& second = inputPoints_[1];
    auto entity = std::make_unique<DimensionEntity>(
        IdGenerator::Next(),
        first,
        second,
        point,
        dimAlignedOverrideText_,
        DimensionKind::Aligned);
    entity->SetTextRotationRadians(dimAlignedTextRotationRadians_);
    ApplyDimensionStyleName(*entity);
    commandManager_->AddEntity(std::move(entity));

    ResetDimAlignedOptions();
    inputPoints_.clear();
    ClearPreview();
    EmitStatus(QStringLiteral("对齐标注已创建；继续指定第一条尺寸界线原点"));
}

void CadView::HandleDimLinearPress(const GeoPoint& point)
{
    if (!document_ || !commandManager_) {
        return;
    }

    if (inputPoints_.empty()) {
        inputPoints_.push_back(ResolveDimensionDefPoint(point));
        EmitStatus(QStringLiteral("线性标注：指定第二条尺寸界线原点"));
        return;
    }

    if (inputPoints_.size() == 1) {
        const GeoPoint second = ResolveDimensionDefPoint(point);
        if (Distance2D(inputPoints_.front(), second) <= Tolerance::Model) {
            EmitStatus(QStringLiteral("线性标注：两点不能重合"));
            return;
        }
        inputPoints_.push_back(second);
        dragCurrentPoint_ = second;
        setFocus(Qt::MouseFocusReason);
        EmitStatus(QStringLiteral("线性标注：指定尺寸线位置或 [多行文字(M)/文字(T)/角度(A)/旋转(R)]"));
        return;
    }

    const GeoPoint& first = inputPoints_[0];
    const GeoPoint& second = inputPoints_[1];
    const LinearDimensionOrientation orientation = ResolveLinearOrientation(
        first,
        second,
        point,
        dimLinearOrientationOverride_);

    auto entity = std::make_unique<DimensionEntity>(
        IdGenerator::Next(),
        first,
        second,
        point,
        dimLinearOverrideText_,
        DimensionKind::Linear);
    entity->SetLinearOrientation(orientation);
    entity->SetTextRotationRadians(dimLinearTextRotationRadians_);
    ApplyDimensionStyleName(*entity);
    commandManager_->AddEntity(std::move(entity));

    ResetDimLinearOptions();
    inputPoints_.clear();
    ClearPreview();
    EmitStatus(QStringLiteral("线性标注已创建；继续指定第一条尺寸界线原点"));
}

void CadView::HandleDimOrdinatePress(const GeoPoint& point)
{
    if (!document_ || !commandManager_) {
        return;
    }

    if (inputPoints_.empty()) {
        // `point` is already osnap-resolved (CEN/INT/END/MID/QUA/...).
        inputPoints_.push_back(point);
        dragCurrentPoint_ = inputPoints_.front();
        setFocus(Qt::MouseFocusReason);
        EmitStatus(QStringLiteral("坐标标注：指定引线位置或 [多行文字(M)/文字(T)/角度(A)/X/Y]"));
        return;
    }

    const GeoPoint& feature = inputPoints_.front();
    const GeoPoint ucsOrigin(0.0, 0.0);
    const LinearDimensionOrientation orientation = ResolveOrdinateOrientation(
        feature,
        point,
        dimOrdinateOrientationOverride_);

    auto entity = std::make_unique<DimensionEntity>(
        IdGenerator::Next(),
        feature,
        ucsOrigin,
        point,
        dimOrdinateOverrideText_,
        DimensionKind::Ordinate);
    entity->SetLinearOrientation(orientation);
    entity->SetTextRotationRadians(dimOrdinateTextRotationRadians_);
    ApplyDimensionStyleName(*entity);
    commandManager_->AddEntity(std::move(entity));

    ResetDimOrdinateOptions();
    inputPoints_.clear();
    ClearPreview();
    EmitStatus(QStringLiteral("坐标标注已创建；继续指定特征点"));
}

void CadView::ResetDimRadiusOptions()
{
    dimRadiusValue_ = 0.0;
    dimRadiusHasArcSpan_ = false;
    dimRadiusArcStartRadians_ = 0.0;
    dimRadiusArcEndRadians_ = 0.0;
    dimRadiusOverrideText_.clear();
    dimRadiusTextRotationRadians_ = 0.0;
}

void CadView::PromptDimRadiusTextEdit(const bool multiline)
{
    if (!document_ || dimRadiusValue_ <= Tolerance::Model
        || (toolMode_ != ToolMode::DrawDimRadius && toolMode_ != ToolMode::DrawDimJoggedRadius
            && toolMode_ != ToolMode::DrawDimDiameter)) {
        return;
    }
    if (toolMode_ == ToolMode::DrawDimJoggedRadius && inputPoints_.size() != 2) {
        return;
    }
    if (toolMode_ != ToolMode::DrawDimJoggedRadius && inputPoints_.size() != 1) {
        return;
    }

    const DimensionStyle dimStyle = ActiveDimensionStyle();
    const GeoPoint center = inputPoints_.front();
    const GeoPoint dimCursor = dragCurrentPoint_;
    const GeoPoint attachment = ComputeRadiusAttachmentPoint(
        center,
        dimRadiusValue_,
        dimCursor,
        dimRadiusHasArcSpan_,
        dimRadiusArcStartRadians_,
        dimRadiusArcEndRadians_);

    QString defaultLabel;
    if (toolMode_ == ToolMode::DrawDimJoggedRadius) {
        const GeoPoint centerOverride = inputPoints_[1];
        const double ux = (attachment.X() - center.X()) / dimRadiusValue_;
        const double uy = (attachment.Y() - center.Y()) / dimRadiusValue_;
        const double overrideParam = (centerOverride.X() - center.X()) * ux
            + (centerOverride.Y() - center.Y()) * uy;
        const double textParam = (dimCursor.X() - center.X()) * ux + (dimCursor.Y() - center.Y()) * uy;
        const double jogParam = overrideParam + (textParam - overrideParam) * 0.55;
        const GeoPoint defaultJog(
            center.X() + ux * jogParam,
            center.Y() + uy * jogParam);
        defaultLabel = BuildJoggedRadiusDimensionLayout(
            center,
            centerOverride,
            dimCursor,
            defaultJog,
            dimStyle,
            dimRadiusOverrideText_,
            dimRadiusTextRotationRadians_,
            dimRadiusHasArcSpan_,
            dimRadiusArcStartRadians_,
            dimRadiusArcEndRadians_,
            dimRadiusValue_).label;
    } else if (toolMode_ == ToolMode::DrawDimDiameter) {
        defaultLabel = BuildDiameterDimensionLayout(
            center,
            attachment,
            dragCurrentPoint_,
            dimStyle,
            dimRadiusOverrideText_,
            dimRadiusTextRotationRadians_).label;
    } else {
        defaultLabel = BuildRadiusDimensionLayout(
            center,
            attachment,
            dragCurrentPoint_,
            dimStyle,
            dimRadiusOverrideText_,
            dimRadiusTextRotationRadians_).label;
    }

    if (multiline) {
        MTextEditorDialog dialog(*document_, this);
        MTextEditorOptions options;
        options.content = dimRadiusOverrideText_.empty()
            ? defaultLabel.toUtf8().constData()
            : dimRadiusOverrideText_;
        dialog.SetOptions(options);
        if (dialog.exec() != QDialog::Accepted) {
            return;
        }
        dimRadiusOverrideText_ = dialog.Options().content;
    } else {
        bool ok = false;
        const QString current = dimRadiusOverrideText_.empty()
            ? defaultLabel
            : QString::fromUtf8(dimRadiusOverrideText_.c_str());
        const QString text = QInputDialog::getText(
            this,
            QStringLiteral("标注文字"),
            QStringLiteral("文字(T)："),
            QLineEdit::Normal,
            current,
            &ok);
        if (!ok) {
            return;
        }
        dimRadiusOverrideText_ = text.toUtf8().constData();
    }

    EmitStatus(QStringLiteral("半径标注：已设置标注文字；指定尺寸线位置"));
    UpdatePreview(dragCurrentPoint_);
}

void CadView::PromptDimRadiusTextAngle()
{
    if (toolMode_ == ToolMode::DrawDimJoggedRadius) {
        if (inputPoints_.size() != 2) {
            return;
        }
    } else if (inputPoints_.size() != 1) {
        return;
    }

    bool ok = false;
    const double currentDegrees = qRadiansToDegrees(dimRadiusTextRotationRadians_);
    const double angle = QInputDialog::getDouble(
        this,
        QStringLiteral("标注文字角度"),
        QStringLiteral("角度(A)（度）："),
        currentDegrees,
        -360.0,
        360.0,
        1,
        &ok);
    if (!ok) {
        return;
    }

    dimRadiusTextRotationRadians_ = qDegreesToRadians(angle);
    EmitStatus(QStringLiteral("半径标注：已设置文字角度；指定尺寸线位置"));
    UpdatePreview(dragCurrentPoint_);
}

void CadView::HandleDimRadiusPress(const GeoPoint& point)
{
    if (!document_ || !commandManager_) {
        return;
    }

    if (inputPoints_.empty()) {
        if (!TryBeginArcDimensionPick(point)) {
            EmitStatus(QStringLiteral("半径标注：请选择圆或圆弧"));
            return;
        }
        EmitStatus(QStringLiteral("半径标注：指定尺寸线位置或 [多行文字(M)/文字(T)/角度(A)]"));
        return;
    }

    const GeoPoint center = inputPoints_.front();
    const GeoPoint attachment = ComputeRadiusAttachmentPoint(
        center,
        dimRadiusValue_,
        point,
        dimRadiusHasArcSpan_,
        dimRadiusArcStartRadians_,
        dimRadiusArcEndRadians_);

    auto entity = std::make_unique<DimensionEntity>(
        IdGenerator::Next(),
        center,
        attachment,
        point,
        dimRadiusOverrideText_,
        DimensionKind::Radius);
    entity->SetRadiusValue(dimRadiusValue_);
    if (dimRadiusHasArcSpan_) {
        entity->SetArcSpan(dimRadiusArcStartRadians_, dimRadiusArcEndRadians_);
    }
    entity->SetTextRotationRadians(dimRadiusTextRotationRadians_);
    ApplyDimensionStyleName(*entity);
    commandManager_->AddEntity(std::move(entity));

    ResetDimRadiusOptions();
    inputPoints_.clear();
    ClearPreview();
    EmitStatus(QStringLiteral("半径标注已创建；继续选择圆或圆弧"));
}

void CadView::HandleDimJoggedRadiusPress(const GeoPoint& point)
{
    if (!document_ || !commandManager_) {
        return;
    }

    if (inputPoints_.empty()) {
        if (!TryBeginArcDimensionPick(point)) {
            EmitStatus(QStringLiteral("折弯半径：请选择圆或圆弧"));
            return;
        }
        EmitStatus(QStringLiteral("折弯半径：指定圆心覆盖点"));
        return;
    }

    if (inputPoints_.size() == 1) {
        inputPoints_.push_back(ApplySnap(point));
        dragCurrentPoint_ = inputPoints_.back();
        EmitStatus(QStringLiteral("折弯半径：指定尺寸线位置或 [多行文字(M)/文字(T)/角度(A)]"));
        return;
    }

    if (inputPoints_.size() == 2) {
        inputPoints_.push_back(point);
        dragCurrentPoint_ = point;
        EmitStatus(QStringLiteral("折弯半径：指定折弯位置"));
        return;
    }

    const GeoPoint trueCenter = inputPoints_.front();
    const GeoPoint centerOverride = inputPoints_[1];
    const GeoPoint dimLineLocation = inputPoints_[2];
    const GeoPoint jogLocation = point;
    const GeoPoint attachment = ComputeRadiusAttachmentPoint(
        trueCenter,
        dimRadiusValue_,
        dimLineLocation,
        dimRadiusHasArcSpan_,
        dimRadiusArcStartRadians_,
        dimRadiusArcEndRadians_);

    auto entity = std::make_unique<DimensionEntity>(
        IdGenerator::Next(),
        trueCenter,
        attachment,
        dimLineLocation,
        dimRadiusOverrideText_,
        DimensionKind::JoggedRadius);
    entity->SetRadiusValue(dimRadiusValue_);
    entity->SetJogCenter(centerOverride);
    entity->SetJogPoint(jogLocation);
    if (dimRadiusHasArcSpan_) {
        entity->SetArcSpan(dimRadiusArcStartRadians_, dimRadiusArcEndRadians_);
    }
    entity->SetTextRotationRadians(dimRadiusTextRotationRadians_);
    ApplyDimensionStyleName(*entity);
    commandManager_->AddEntity(std::move(entity));

    ResetDimRadiusOptions();
    inputPoints_.clear();
    ClearPreview();
    EmitStatus(QStringLiteral("折弯半径标注已创建；继续选择圆或圆弧"));
}

void CadView::HandleDimDiameterPress(const GeoPoint& point)
{
    if (!document_ || !commandManager_) {
        return;
    }

    if (inputPoints_.empty()) {
        if (!TryBeginArcDimensionPick(point)) {
            EmitStatus(QStringLiteral("直径标注：请选择圆或圆弧"));
            return;
        }
        EmitStatus(QStringLiteral("直径标注：指定尺寸线位置或 [多行文字(M)/文字(T)/角度(A)]"));
        return;
    }

    const GeoPoint center = inputPoints_.front();
    const GeoPoint attachment = ComputeRadiusAttachmentPoint(
        center,
        dimRadiusValue_,
        point,
        dimRadiusHasArcSpan_,
        dimRadiusArcStartRadians_,
        dimRadiusArcEndRadians_);

    auto entity = std::make_unique<DimensionEntity>(
        IdGenerator::Next(),
        center,
        attachment,
        point,
        dimRadiusOverrideText_,
        DimensionKind::Diameter);
    entity->SetRadiusValue(dimRadiusValue_);
    if (dimRadiusHasArcSpan_) {
        entity->SetArcSpan(dimRadiusArcStartRadians_, dimRadiusArcEndRadians_);
    }
    entity->SetTextRotationRadians(dimRadiusTextRotationRadians_);
    ApplyDimensionStyleName(*entity);
    commandManager_->AddEntity(std::move(entity));

    ResetDimRadiusOptions();
    inputPoints_.clear();
    ClearPreview();
    EmitStatus(QStringLiteral("直径标注已创建；继续选择圆或圆弧"));
}

void CadView::ResetDimAngularOptions()
{
    dimAngularPlacementReady_ = false;
    dimAngularThreePointMode_ = false;
    dimAngularAwaitCircleArm2_ = false;
    dimAngularHasArcSpan_ = false;
    dimAngularArcStartRadians_ = 0.0;
    dimAngularArcEndRadians_ = 0.0;
    dimAngularSourceArcRadius_ = 0.0;
    dimAngularCircleRadius_ = 0.0;
    dimAngularLine1Id_ = 0;
    dimAngularQuadrantLock_ = -1;
    dimAngularOverrideText_.clear();
    dimAngularTextRotationRadians_ = 0.0;
}

void CadView::BeginAngularPlacementReady(
    const GeoPoint& vertex,
    const GeoPoint& arm1,
    const GeoPoint& arm2,
    const bool hasArcSpan,
    const double arcStartRadians,
    const double arcEndRadians,
    const double sourceArcRadius)
{
    dimAngularVertex_ = vertex;
    dimAngularArm1_ = arm1;
    dimAngularArm2_ = arm2;
    dimAngularPlacementReady_ = true;
    dimAngularHasArcSpan_ = hasArcSpan;
    dimAngularArcStartRadians_ = arcStartRadians;
    dimAngularArcEndRadians_ = arcEndRadians;
    dimAngularSourceArcRadius_ = sourceArcRadius;
    dimAngularAwaitCircleArm2_ = false;
    dimAngularLine1Id_ = 0;
    dimAngularThreePointMode_ = false;
    inputPoints_.clear();
    dragCurrentPoint_ = vertex;
}

void CadView::HandleDimAngularPress(const GeoPoint& point)
{
    Q_UNUSED(point);
    if (!document_ || !commandManager_) {
        return;
    }

    // Live cursor for object selection; osnap-resolved point for geometry samples.
    const GeoPoint rawCursor = MapToModel(
        viewport() ? viewport()->mapTo(this, lastCursorViewportPos_) : lastCursorViewportPos_);
    const GeoPoint snapped = ResolveInputPoint(rawCursor);
    const double tolerance = ModelToleranceFromPixels(snapTolerancePixels_);
    const double centerTolerance = ModelToleranceFromPixels(std::max(2.0, snapTolerancePixels_));
    const QRectF visibleModelRect = VisibleModelRect(snapTolerancePixels_);

    if (dimAngularPlacementReady_) {
        auto entity = std::make_unique<DimensionEntity>(
            IdGenerator::Next(),
            dimAngularVertex_,
            dimAngularArm1_,
            snapped,
            dimAngularOverrideText_,
            DimensionKind::Angular);
        entity->SetJogCenter(dimAngularArm2_);
        if (dimAngularHasArcSpan_) {
            entity->SetArcSpan(dimAngularArcStartRadians_, dimAngularArcEndRadians_);
            entity->SetRadiusValue(dimAngularSourceArcRadius_);
        }
        entity->SetTextRotationRadians(dimAngularTextRotationRadians_);
        ApplyDimensionStyleName(*entity);
        commandManager_->AddEntity(std::move(entity));

        dimAngularPlacementReady_ = false;
        dimAngularOverrideText_.clear();
        dimAngularTextRotationRadians_ = 0.0;
        dimAngularQuadrantLock_ = -1;
        ClearPreview();
        if (cadScene_) {
            cadScene_->InvalidateItemRegions();
        }
        EmitStatus(QStringLiteral("角度标注已创建；继续选择对象或回车使用三点标注"));
        return;
    }

    if (dimAngularThreePointMode_) {
        inputPoints_.push_back(snapped);
        if (inputPoints_.size() == 1) {
            EmitStatus(QStringLiteral("三点角度标注：指定第一个端点"));
            return;
        }
        if (inputPoints_.size() == 2) {
            EmitStatus(QStringLiteral("三点角度标注：指定第二个端点"));
            return;
        }
        if (inputPoints_.size() >= 3) {
            BeginAngularPlacementReady(inputPoints_[0], inputPoints_[1], inputPoints_[2]);
            EmitStatus(QStringLiteral("角度标注：指定标注弧位置或 [多行文字(M)/文字(T)/角度(A)/象限(Q)]"));
        }
        return;
    }

    if (dimAngularAwaitCircleArm2_) {
        const GeoPoint center = dimAngularVertex_;
        const double radius = dimAngularCircleRadius_;
        if (radius <= Tolerance::Model) {
            EmitStatus(QStringLiteral("角度标注：圆半径无效"));
            return;
        }
        const double angle = std::atan2(snapped.Y() - center.Y(), snapped.X() - center.X());
        const GeoPoint arm2 = PointOnCircle(center, radius, angle);
        BeginAngularPlacementReady(center, dimAngularArm1_, arm2);
        EmitStatus(QStringLiteral("角度标注：指定标注弧位置或 [多行文字(M)/文字(T)/角度(A)/象限(Q)]"));
        return;
    }

    if (dimAngularLine1Id_ != 0) {
        const EntityId hit = HitTest(rawCursor);
        if (hit == 0 || hit == dimAngularLine1Id_) {
            EmitStatus(QStringLiteral("角度标注：选择第二条直线或多段线"));
            return;
        }
        const Entity* entity1 = document_->FindEntity(dimAngularLine1Id_);
        const Entity* entity2 = document_->FindEntity(hit);
        if (!entity1 || !entity2) {
            dimAngularLine1Id_ = 0;
            return;
        }
        const std::optional<GeoLine> line1 = LineSegmentFromEntity(
            *entity1, dimAngularLine1Pick_, visibleModelRect, tolerance);
        const std::optional<GeoLine> line2 = LineSegmentFromEntity(
            *entity2, rawCursor, visibleModelRect, tolerance);
        if (!line1 || !line2) {
            EmitStatus(QStringLiteral("角度标注：请选择直线或多段线"));
            return;
        }
        const std::optional<GeoPoint> vertexOpt = InfiniteLineIntersection(*line1, *line2);
        if (!vertexOpt) {
            EmitStatus(QStringLiteral("角度标注：两线平行，无法标注夹角"));
            dimAngularLine1Id_ = 0;
            return;
        }
        const GeoPoint vertex = *vertexOpt;
        const GeoPoint arm1 = ArmPointOnLine(*line1, vertex, dimAngularLine1Pick_);
        const GeoPoint arm2 = ArmPointOnLine(*line2, vertex, rawCursor);
        dimAngularLine1Id_ = 0;
        BeginAngularPlacementReady(vertex, arm1, arm2);
        EmitStatus(QStringLiteral("角度标注：指定标注弧位置或 [多行文字(M)/文字(T)/角度(A)/象限(Q)]"));
        return;
    }

    std::optional<CircleArcPick> pick = PickCircleOrArcAt(
        *document_, rawCursor, tolerance, visibleModelRect);
    if (!pick) {
        if (const std::optional<ObjectSnapHit> hit = FindObjectSnapHit(rawCursor)) {
            if (hit->kind == ObjectSnapKind::Center) {
                pick = PickCircleOrArcByCenter(*document_, hit->point, centerTolerance);
            } else if (hit->kind == ObjectSnapKind::Quadrant
                || hit->kind == ObjectSnapKind::Nearest) {
                pick = PickCircleOrArcAt(*document_, hit->point, tolerance, visibleModelRect);
            }
        }
    }
    if (pick) {
        const GeoPoint center = pick->center;
        if (pick->hasArcSpan) {
            const GeoPoint arm1 = PointOnCircle(center, pick->radius, pick->arcStartRadians);
            const GeoPoint arm2 = PointOnCircle(center, pick->radius, pick->arcEndRadians);
            BeginAngularPlacementReady(
                center,
                arm1,
                arm2,
                true,
                pick->arcStartRadians,
                pick->arcEndRadians,
                pick->radius);
            EmitStatus(QStringLiteral("角度标注：指定标注弧位置或 [多行文字(M)/文字(T)/角度(A)/象限(Q)]"));
            return;
        }

        const double angle = std::atan2(snapped.Y() - center.Y(), snapped.X() - center.X());
        dimAngularVertex_ = center;
        dimAngularArm1_ = PointOnCircle(center, pick->radius, angle);
        dimAngularCircleRadius_ = pick->radius;
        dimAngularAwaitCircleArm2_ = true;
        EmitStatus(QStringLiteral("角度标注：指定圆上第二个角点"));
        return;
    }

    const EntityId hit = HitTest(rawCursor);
    if (hit != 0) {
        const Entity* entity = document_->FindEntity(hit);
        if (entity && (entity->Type() == EntityType::Line || entity->Type() == EntityType::Polyline)) {
            dimAngularLine1Id_ = hit;
            dimAngularLine1Pick_ = rawCursor;
            EmitStatus(QStringLiteral("角度标注：选择第二条直线或多段线"));
            return;
        }
    }

    EmitStatus(QStringLiteral("角度标注：选择圆弧/圆/直线，或回车后用三点定义角"));
}

void CadView::PromptDimAngularTextEdit(const bool multiline)
{
    if (!dimAngularPlacementReady_) {
        return;
    }
    const DimensionStyle dimStyle = ActiveDimensionStyle();
    const AngularDimensionLayout layout = BuildAngularDimensionLayout(
        dimAngularVertex_,
        dimAngularArm1_,
        dimAngularArm2_,
        dragCurrentPoint_,
        dimStyle,
        dimAngularOverrideText_,
        dimAngularTextRotationRadians_,
        dimAngularHasArcSpan_,
        dimAngularArcStartRadians_,
        dimAngularArcEndRadians_,
        dimAngularSourceArcRadius_);

    if (multiline) {
        MTextEditorDialog dialog(*document_, this);
        MTextEditorOptions options;
        options.content = dimAngularOverrideText_.empty()
            ? layout.label.toUtf8().constData()
            : dimAngularOverrideText_;
        dialog.SetOptions(options);
        if (dialog.exec() != QDialog::Accepted) {
            return;
        }
        dimAngularOverrideText_ = dialog.Options().content;
        UpdatePreview(dragCurrentPoint_);
        EmitStatus(QStringLiteral("角度标注：已设置标注文字"));
    } else {
        bool ok = false;
        const QString text = QInputDialog::getText(
            this,
            tr("标注文字"),
            tr("标注内容（<> 表示测量值，可追加 %%D 显示度符号）:"),
            QLineEdit::Normal,
            layout.label,
            &ok);
        if (ok) {
            dimAngularOverrideText_ = text.toUtf8().constData();
            UpdatePreview(dragCurrentPoint_);
            EmitStatus(QStringLiteral("角度标注：已设置标注文字"));
        }
    }
}

void CadView::PromptDimAngularTextAngle()
{
    if (!dimAngularPlacementReady_) {
        return;
    }
    bool ok = false;
    const double angle = QInputDialog::getDouble(
        this,
        tr("文字角度"),
        tr("标注文字显示角度 (°):"),
        qRadiansToDegrees(dimAngularTextRotationRadians_),
        -360.0,
        360.0,
        2,
        &ok);
    if (ok) {
        dimAngularTextRotationRadians_ = qDegreesToRadians(angle);
        UpdatePreview(dragCurrentPoint_);
        EmitStatus(QStringLiteral("角度标注：已设置文字角度"));
    }
}

void CadView::CycleDimAngularQuadrantLock()
{
    if (!dimAngularPlacementReady_) {
        return;
    }
    dimAngularQuadrantLock_ = (dimAngularQuadrantLock_ + 1) % 4;
    UpdatePreview(dragCurrentPoint_);
    EmitStatus(QStringLiteral("角度标注：象限锁定 Q%1").arg(dimAngularQuadrantLock_ + 1));
}

void CadView::SetPreviewAngularDimLabel(const AngularDimensionLayout& layout, const DimensionStyle& style)
{
    if (!scene()) {
        ClearPreviewDimLabel();
        return;
    }

    QRectF frameDirty;
    if (previewDimLabelItem_ && previewDimLabelItem_->scene() == scene()) {
        frameDirty = previewDimLabelItem_->mapRectToScene(previewDimLabelItem_->boundingRect());
    }

    if (!previewDimLabelItem_ || previewDimLabelItem_->scene() != scene()) {
        ClearPreviewDimLabel();
        previewDimLabelItem_ = scene()->addSimpleText(QString());
        previewDimLabelItem_->setZValue(1001.0);
        previewDimLabelItem_->setCacheMode(QGraphicsItem::NoCache);
    }

    QFont font = previewDimLabelItem_->font();
    font.setPixelSize(qMax(1, qRound(std::max(1.0, style.textHeight))));
    previewDimLabelItem_->setFont(font);
    previewDimLabelItem_->setBrush(QColor(64, 180, 255));
    previewDimLabelItem_->setText(layout.label);

    const DimensionTextPreview textPreview = AngularDimensionTextPreview(layout, style);
    SetPreviewMeasureLabelPosition(
        previewDimLabelItem_,
        textPreview.center,
        textPreview.rotationDegrees);

    const QRectF newBounds = previewDimLabelItem_->mapRectToScene(previewDimLabelItem_->boundingRect());
    frameDirty = frameDirty.isValid() ? frameDirty.united(newBounds) : newBounds;
    if (frameDirty.isValid()) {
        const double margin = ModelToleranceFromPixels(8.0);
        const QRectF invalidRect = frameDirty.adjusted(-margin, -margin, margin, margin);
        scene()->invalidate(invalidRect, QGraphicsScene::AllLayers);
        ScheduleViewportRepaint(invalidRect);
        return;
    }
    ScheduleViewportRepaint();
}

void CadView::SetPreviewOrdinateDimLabel(const OrdinateDimensionLayout& layout, const DimensionStyle& style)
{
    if (!scene()) {
        ClearPreviewDimLabel();
        return;
    }

    QRectF frameDirty;
    if (previewDimLabelItem_ && previewDimLabelItem_->scene() == scene()) {
        frameDirty = previewDimLabelItem_->mapRectToScene(previewDimLabelItem_->boundingRect());
    }

    if (!previewDimLabelItem_ || previewDimLabelItem_->scene() != scene()) {
        ClearPreviewDimLabel();
        previewDimLabelItem_ = scene()->addSimpleText(QString());
        previewDimLabelItem_->setZValue(1001.0);
        previewDimLabelItem_->setCacheMode(QGraphicsItem::NoCache);
    }

    QFont font = previewDimLabelItem_->font();
    font.setPixelSize(qMax(1, qRound(std::max(1.0, style.textHeight))));
    previewDimLabelItem_->setFont(font);
    previewDimLabelItem_->setBrush(QColor(64, 180, 255));
    previewDimLabelItem_->setText(layout.label);

    const DimensionTextPreview textPreview = OrdinateDimensionTextPreview(layout, style);
    SetPreviewMeasureLabelPosition(
        previewDimLabelItem_,
        textPreview.center,
        textPreview.rotationDegrees);

    const QRectF newBounds = previewDimLabelItem_->mapRectToScene(previewDimLabelItem_->boundingRect());
    frameDirty = frameDirty.isValid() ? frameDirty.united(newBounds) : newBounds;
    if (frameDirty.isValid()) {
        const double margin = ModelToleranceFromPixels(8.0);
        const QRectF invalidRect = frameDirty.adjusted(-margin, -margin, margin, margin);
        scene()->invalidate(invalidRect, QGraphicsScene::AllLayers);
        ScheduleViewportRepaint(invalidRect);
        return;
    }
    ScheduleViewportRepaint();
}

void CadView::PromptDimJoggedRadiusTextEdit(const bool multiline)
{
    PromptDimRadiusTextEdit(multiline);
}

void CadView::PromptDimJoggedRadiusTextAngle()
{
    PromptDimRadiusTextAngle();
}

void CadView::PromptDimDiameterTextEdit(const bool multiline)
{
    PromptDimRadiusTextEdit(multiline);
}

void CadView::PromptDimDiameterTextAngle()
{
    PromptDimRadiusTextAngle();
}

void CadView::EditTextEntity(EntityId id)
{
    if (!document_ || !commandManager_) {
        return;
    }

    Entity* entity = document_->FindEntity(id);
    if (!entity || entity->Type() != EntityType::Text) {
        return;
    }

    const auto& text = static_cast<const TextEntity&>(*entity);
    SingleLineTextEditorDialog dialog(*document_, this);
    SingleLineTextOptions options;
    options.text = text.Text();
    options.height = text.Height();
    options.rotationDegrees = qRadiansToDegrees(text.RotationRadians());
    options.widthFactor = text.WidthFactor();
    options.obliqueDegrees = qRadiansToDegrees(text.ObliqueAngleRadians());
    options.horizontalAlign = text.HorizontalAlign();
    options.verticalAlign = text.VerticalAlign();
    options.textStyle = entity->Style().textStyle;
    options.style = entity->Style();
    dialog.SetOptions(options);

    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    options = dialog.Options();
    auto replacement = std::make_unique<TextEntity>(
        id,
        text.Position(),
        options.text,
        options.height,
        qDegreesToRadians(options.rotationDegrees),
        options.horizontalAlign,
        options.verticalAlign,
        options.widthFactor,
        qDegreesToRadians(options.obliqueDegrees));
    replacement->SetLayerName(entity->LayerName());
    replacement->SetSelected(entity->IsSelected());
    replacement->Style() = options.style;
    replacement->Style().textStyle = options.textStyle;
    commandManager_->ReplaceEntity(id, std::move(replacement));
    if (cadScene_) {
        cadScene_->InvalidateItemRegions();
    }
    EmitStatus(QStringLiteral("单行文字已更新"));
}

void CadView::EditMTextEntity(EntityId id)
{
    if (!document_ || !commandManager_) {
        return;
    }

    Entity* entity = document_->FindEntity(id);
    if (!entity || entity->Type() != EntityType::MText) {
        return;
    }

    const auto& text = static_cast<const MTextEntity&>(*entity);
    MTextEditorDialog dialog(*document_, this);
    MTextEditorOptions options;
    options.content = text.Content();
    options.height = text.Height();
    options.rotationDegrees = qRadiansToDegrees(text.RotationRadians());
    options.referenceWidth = text.ReferenceWidth();
    options.lineSpacingFactor = text.LineSpacingFactor();
    options.attachment = text.Attachment();
    options.paragraphAlign = text.ParagraphAlign();
    options.backgroundMask = text.BackgroundMask();
    options.textStyle = entity->Style().textStyle;
    options.style = entity->Style();
    dialog.SetOptions(options);

    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    options = dialog.Options();
    auto replacement = std::make_unique<MTextEntity>(
        id,
        text.Position(),
        options.content,
        options.height,
        qDegreesToRadians(options.rotationDegrees),
        options.referenceWidth,
        options.lineSpacingFactor,
        options.attachment,
        options.paragraphAlign,
        options.backgroundMask);
    replacement->SetLayerName(entity->LayerName());
    replacement->SetSelected(entity->IsSelected());
    replacement->Style() = options.style;
    replacement->Style().textStyle = options.textStyle;
    commandManager_->ReplaceEntity(id, std::move(replacement));
    if (cadScene_) {
        cadScene_->InvalidateItemRegions();
    }
    EmitStatus(QStringLiteral("多行文字已更新"));
}

void CadView::FinishMultiPointDraw()
{
    if (!commandManager_ || inputPoints_.size() < 2) {
        ResetTransientState();
        return;
    }
    if (toolMode_ == ToolMode::DrawPolyline || toolMode_ == ToolMode::DrawSpline) {
        if (auto entity = DrawToolEngine::FinalizeContinuousDraw(toolMode_, drawOptions_, inputPoints_)) {
            NoteArcContinuation(*entity);
            commandManager_->AddEntity(std::move(entity));
        }
    }
    ResetTransientState();
}

void CadView::FinishDrawCommand()
{
    if (!IsDrawTool(toolMode_)) {
        return;
    }
    if (DrawToolEngine::IsContinuousDrawTool(toolMode_)) {
        FinishMultiPointDraw();
    } else {
        // AutoCAD LINE: Enter/right-click ends the command; already-committed
        // segments remain. Clear the rubber-band / chain and stay ready or mark done.
        ClearLineChain();
        ResetTransientState();
    }
    drawSegmentFinished_ = true;
}

bool CadView::IsDrawInProgress() const
{
    return !inputPoints_.empty();
}

bool CadView::IsContinuousLineDraw() const
{
    return toolMode_ == ToolMode::DrawLine
        && drawOptions_.method == DrawMethod::LineTwoPoints;
}

bool CadView::IsContinuousPolylineDraw() const
{
    return toolMode_ == ToolMode::DrawPolyline
        && drawOptions_.method == DrawMethod::PolylineContinuous;
}

bool CadView::IsSplineDraw() const
{
    return toolMode_ == ToolMode::DrawSpline;
}

bool CadView::IsFilletChamferCommand() const
{
    return toolMode_ == ToolMode::Fillet || toolMode_ == ToolMode::Chamfer;
}

bool CadView::IsPolygonCommand() const
{
    return toolMode_ == ToolMode::DrawPolygon;
}

bool CadView::IsCommandOptionHudActive() const
{
    return (IsContinuousPolylineDraw() && IsDrawInProgress()) || IsSplineDraw()
        || IsSplineGripEditing() || IsFilletChamferCommand() || IsPolygonCommand()
        || IsArrayCommand();
}

void CadView::ResetPolygonSession()
{
    polygonPhase_ = (drawOptions_.method == DrawMethod::PolygonEdge)
        ? PolygonCommandPhase::Edge
        : PolygonCommandPhase::Center;
}

QString CadView::PolygonPromptText() const
{
    switch (polygonPhase_) {
    case PolygonCommandPhase::Option: {
        const bool circumscribed = drawOptions_.method == DrawMethod::PolygonCircumscribed;
        return circumscribed
            ? QStringLiteral("输入选项 [内接于圆(I)/外切于圆(C)] <C>")
            : QStringLiteral("输入选项 [内接于圆(I)/外切于圆(C)] <I>");
    }
    case PolygonCommandPhase::Radius:
        return QStringLiteral("指定圆的半径");
    case PolygonCommandPhase::Edge:
        return inputPoints_.empty()
            ? QStringLiteral("指定边的第一个端点")
            : QStringLiteral("指定边的第二个端点");
    case PolygonCommandPhase::Center:
    default:
        return QStringLiteral("指定正多边形的中心点或 [边(E)]");
    }
}

QVector<DynCommandOption> CadView::CurrentPolygonHudOptions() const
{
    QVector<DynCommandOption> options;
    switch (polygonPhase_) {
    case PolygonCommandPhase::Option:
        options.push_back({QStringLiteral("I"), QStringLiteral("内接于圆(I)"), true});
        options.push_back({QStringLiteral("C"), QStringLiteral("外切于圆(C)"), true});
        break;
    case PolygonCommandPhase::Center:
        options.push_back({QStringLiteral("E"), QStringLiteral("边(E)"), true});
        break;
    default:
        break;
    }
    return options;
}

void CadView::UpdatePolygonHud()
{
    if (!IsPolygonCommand() || !viewport()) {
        return;
    }
    EnsureOptionInput();
    const bool keepEdit = optionInput_->IsKeywordEditing();
    optionInput_->ClearCycleList();
    optionInput_->SetPrompt(PolygonPromptText());
    optionInput_->SetOptions(CurrentPolygonHudOptions());
    optionInput_->MoveToViewPosition(lastCursorViewportPos_);
    optionInput_->show();
    optionInput_->raise();
    if (!keepEdit) {
        optionInput_->HideKeywordEdit();
    }
}

void CadView::ApplyPolygonKeyword(const QString& keyword)
{
    const QString k = keyword.trimmed().toUpper();
    if (polygonPhase_ == PolygonCommandPhase::Center) {
        if (k == QLatin1String("E")) {
            drawOptions_.method = DrawMethod::PolygonEdge;
            polygonPhase_ = PolygonCommandPhase::Edge;
            inputPoints_.clear();
            ClearPreview();
            EmitStatus(PolygonPromptText());
            UpdatePolygonHud();
            return;
        }
        if (k == QLatin1String("I")) {
            drawOptions_.method = DrawMethod::PolygonInscribed;
            return;
        }
        if (k == QLatin1String("C")) {
            drawOptions_.method = DrawMethod::PolygonCircumscribed;
            return;
        }
        return;
    }
    if (polygonPhase_ == PolygonCommandPhase::Option || polygonPhase_ == PolygonCommandPhase::Radius) {
        if (k == QLatin1String("C")) {
            drawOptions_.method = DrawMethod::PolygonCircumscribed;
        } else if (k == QLatin1String("I")) {
            drawOptions_.method = DrawMethod::PolygonInscribed;
        } else if (k.isEmpty()) {
            if (drawOptions_.method != DrawMethod::PolygonCircumscribed) {
                drawOptions_.method = DrawMethod::PolygonInscribed;
            }
        } else {
            EmitStatus(QStringLiteral("无效的选项关键字"));
            return;
        }
        polygonPhase_ = PolygonCommandPhase::Radius;
        EmitStatus(PolygonPromptText());
        UpdatePolygonHud();
        if (viewport()) {
            UpdatePreview(ApplySnap(MapToModel(viewport()->mapFromGlobal(QCursor::pos()))));
        }
    }
}

namespace {

QString FormatArrayNumber(double value)
{
    QString text = QString::number(value, 'f', 4);
    while (text.contains(QLatin1Char('.'))
        && (text.endsWith(QLatin1Char('0')) || text.endsWith(QLatin1Char('.')))) {
        text.chop(1);
    }
    return text;
}

} // namespace

bool CadView::IsArrayCommand() const
{
    return toolMode_ == ToolMode::ArrayPolar;
}

CadView::ArrayCommandPhase CadView::ArrayHomePhase() const
{
    return arrayCenter_ ? ArrayCommandPhase::Edit : ArrayCommandPhase::Center;
}

double CadView::PolarAngleBetweenDegrees() const
{
    const int count = std::max(1, polarArrayOptions_.itemCount);
    if (count <= 1) {
        return 0.0;
    }
    const double fill = polarArrayOptions_.fillAngleDegrees;
    const bool fullCircle = std::abs(std::abs(fill) - 360.0) <= 1.0e-9;
    return fill / static_cast<double>(fullCircle ? count : count - 1);
}

QString CadView::ArrayPromptText() const
{
    if (toolMode_ != ToolMode::ArrayPolar) {
        return {};
    }
    switch (arrayPhase_) {
    case ArrayCommandPhase::BasePoint:
        return QStringLiteral("指定基点 <质心>:");
    case ArrayCommandPhase::ItemCount:
        return QStringLiteral("输入阵列中的项目数 <%1>:")
            .arg(std::max(2, polarArrayOptions_.itemCount));
    case ArrayCommandPhase::AngleBetween:
        return QStringLiteral("指定项目间的角度 <%1>:")
            .arg(FormatArrayNumber(PolarAngleBetweenDegrees()));
    case ArrayCommandPhase::FillAngle:
        return QStringLiteral("指定填充角度(+=逆时针，-=顺时针) <%1>:")
            .arg(FormatArrayNumber(polarArrayOptions_.fillAngleDegrees));
    case ArrayCommandPhase::RotateItems:
        return QStringLiteral("是否旋转阵列项目？[是(Y)/否(N)] <%1>:")
            .arg(polarArrayOptions_.rotateItems ? QStringLiteral("是") : QStringLiteral("否"));
    case ArrayCommandPhase::Edit:
        return QStringLiteral("输入阵列选项或 [基点(B)/项目(I)/项目间角度(A)/填充角度(F)/旋转项目(ROT)/退出(X)] <退出>:");
    case ArrayCommandPhase::Center:
    default:
        return QStringLiteral("指定阵列的中心点或 [基点(B)]:");
    }
}

QVector<DynCommandOption> CadView::CurrentArrayHudOptions() const
{
    QVector<DynCommandOption> options;
    if (toolMode_ != ToolMode::ArrayPolar) {
        return options;
    }
    switch (arrayPhase_) {
    case ArrayCommandPhase::RotateItems:
        options.push_back({QStringLiteral("Y"), QStringLiteral("是(Y)"), true});
        options.push_back({QStringLiteral("N"), QStringLiteral("否(N)"), true});
        break;
    case ArrayCommandPhase::Edit:
        options.push_back({QStringLiteral("B"), QStringLiteral("基点(B)"), true});
        options.push_back({QStringLiteral("I"), QStringLiteral("项目(I)"), true});
        options.push_back({QStringLiteral("A"), QStringLiteral("项目间角度(A)"), true});
        options.push_back({QStringLiteral("F"), QStringLiteral("填充角度(F)"), true});
        options.push_back({QStringLiteral("ROT"), QStringLiteral("旋转项目(ROT)"), true});
        options.push_back({QStringLiteral("X"), QStringLiteral("退出(X)"), true});
        break;
    case ArrayCommandPhase::Center:
        options.push_back({QStringLiteral("B"), QStringLiteral("基点(B)"), true});
        break;
    default:
        break;
    }
    return options;
}

bool CadView::IsArrayNumericPhase() const
{
    return toolMode_ == ToolMode::ArrayPolar
        && (arrayPhase_ == ArrayCommandPhase::ItemCount
            || arrayPhase_ == ArrayCommandPhase::AngleBetween
            || arrayPhase_ == ArrayCommandPhase::FillAngle);
}

QString CadView::ArrayNumericDefaultText() const
{
    if (arrayPhase_ == ArrayCommandPhase::ItemCount) {
        return QString::number(std::max(2, polarArrayOptions_.itemCount));
    }
    if (arrayPhase_ == ArrayCommandPhase::AngleBetween) {
        return FormatArrayNumber(PolarAngleBetweenDegrees());
    }
    if (arrayPhase_ == ArrayCommandPhase::FillAngle) {
        return FormatArrayNumber(polarArrayOptions_.fillAngleDegrees);
    }
    return {};
}

void CadView::UpdateArrayHud()
{
    if (!IsArrayCommand() || !viewport()) {
        return;
    }
    EnsureOptionInput();
    const bool keepEdit = optionInput_->IsKeywordEditing();
    optionInput_->ClearCycleList();
    optionInput_->SetPrompt(ArrayPromptText());
    optionInput_->SetOptions(CurrentArrayHudOptions());
    optionInput_->MoveToViewPosition(lastCursorViewportPos_);
    optionInput_->show();
    optionInput_->raise();
    if (IsArrayNumericPhase() && !keepEdit) {
        optionInput_->BeginValueEdit(ArrayNumericDefaultText());
    } else if (!keepEdit) {
        optionInput_->HideKeywordEdit();
    }
}

void CadView::ReturnToArrayPrompt()
{
    arrayPhase_ = ArrayHomePhase();
    EmitStatus(ArrayPromptText());
    if (viewport()) {
        UpdatePreview(ApplySnap(MapToModel(viewport()->mapFromGlobal(QCursor::pos()))));
    } else {
        UpdateArrayHud();
    }
}

void CadView::ApplyArrayKeyword(const QString& keyword)
{
    const QString k = keyword.trimmed().toUpper();
    bool okNum = false;
    const double number = keyword.trimmed().toDouble(&okNum);
    const auto reject = [this](const QString& message) {
        EmitStatus(message);
        UpdateArrayHud();
    };

    if (arrayPhase_ == ArrayCommandPhase::ItemCount) {
        if (okNum) {
            const int itemCount = static_cast<int>(std::lround(number));
            if (itemCount < 2) {
                reject(QStringLiteral("需要 2 到 10000 之间的整数"));
                return;
            }
            polarArrayOptions_.itemCount = std::min(10000, itemCount);
        } else if (!k.isEmpty()) {
            reject(QStringLiteral("需要 2 到 10000 之间的整数"));
            return;
        }
        ReturnToArrayPrompt();
        return;
    }
    if (arrayPhase_ == ArrayCommandPhase::FillAngle) {
        if (okNum) {
            if (std::abs(number) <= Tolerance::Model || std::abs(number) > 360.0) {
                reject(QStringLiteral("需要非零且介于 -360 和 360 之间的角度"));
                return;
            }
            polarArrayOptions_.fillAngleDegrees = number;
        } else if (!k.isEmpty()) {
            reject(QStringLiteral("需要非零且介于 -360 和 360 之间的角度"));
            return;
        }
        ReturnToArrayPrompt();
        return;
    }
    if (arrayPhase_ == ArrayCommandPhase::AngleBetween) {
        if (okNum) {
            const int count = std::max(2, polarArrayOptions_.itemCount);
            if (std::abs(number) <= Tolerance::Model) {
                reject(QStringLiteral("项目间角度不能为 0"));
                return;
            }
            const double asFullCircle = number * static_cast<double>(count);
            if (std::abs(std::abs(asFullCircle) - 360.0) <= 1.0e-6) {
                polarArrayOptions_.fillAngleDegrees = std::copysign(360.0, number);
            } else {
                const double fill = number * static_cast<double>(count - 1);
                if (std::abs(fill) > 360.0) {
                    reject(QStringLiteral("项目间角度过大，填充角度不能超过 360"));
                    return;
                }
                polarArrayOptions_.fillAngleDegrees = fill;
            }
        } else if (!k.isEmpty()) {
            reject(QStringLiteral("需要有效的项目间角度"));
            return;
        }
        ReturnToArrayPrompt();
        return;
    }
    if (arrayPhase_ == ArrayCommandPhase::RotateItems) {
        if (k == QLatin1String("Y") || k == QStringLiteral("是")) {
            polarArrayOptions_.rotateItems = true;
        } else if (k == QLatin1String("N") || k == QStringLiteral("否")) {
            polarArrayOptions_.rotateItems = false;
        } else if (!k.isEmpty()) {
            reject(QStringLiteral("无效的选项关键字"));
            return;
        }
        ReturnToArrayPrompt();
        return;
    }
    if (arrayPhase_ == ArrayCommandPhase::BasePoint) {
        if (k.isEmpty()) {
            polarArrayOptions_.basePoint.reset();
            ReturnToArrayPrompt();
            return;
        }
        reject(QStringLiteral("请指定基点，或按 Enter 使用质心"));
        return;
    }

    if (k.isEmpty()) {
        if (arrayPhase_ == ArrayCommandPhase::Edit) {
            CommitPolarArray();
        } else {
            EmitStatus(ArrayPromptText());
            UpdateArrayHud();
        }
        return;
    }
    if (k == QLatin1String("B")) {
        arrayPhase_ = ArrayCommandPhase::BasePoint;
    } else if (arrayPhase_ == ArrayCommandPhase::Edit && k == QLatin1String("I")) {
        arrayPhase_ = ArrayCommandPhase::ItemCount;
    } else if (arrayPhase_ == ArrayCommandPhase::Edit && k == QLatin1String("A")) {
        arrayPhase_ = ArrayCommandPhase::AngleBetween;
    } else if (arrayPhase_ == ArrayCommandPhase::Edit && k == QLatin1String("F")) {
        arrayPhase_ = ArrayCommandPhase::FillAngle;
    } else if (arrayPhase_ == ArrayCommandPhase::Edit
        && (k == QLatin1String("R") || k == QLatin1String("RO") || k == QLatin1String("ROT"))) {
        arrayPhase_ = ArrayCommandPhase::RotateItems;
    } else if (arrayPhase_ == ArrayCommandPhase::Edit && k == QLatin1String("X")) {
        CommitPolarArray();
        return;
    } else {
        reject(QStringLiteral("无效的选项关键字"));
        return;
    }
    EmitStatus(ArrayPromptText());
    if (viewport()) {
        UpdatePreview(ApplySnap(MapToModel(viewport()->mapFromGlobal(QCursor::pos()))));
    } else {
        UpdateArrayHud();
    }
}

bool CadView::CommitPolarArray()
{
    if (!commandManager_ || !arrayCenter_) {
        return false;
    }
    const bool ok = commandManager_->CreatePolarArray(
        arraySourceIds_, *arrayCenter_, polarArrayOptions_);
    EmitStatus(ok
        ? QStringLiteral("环形阵列完成：%1 个项目，填充角度 %2°")
              .arg(std::max(1, polarArrayOptions_.itemCount))
              .arg(polarArrayOptions_.fillAngleDegrees, 0, 'f', 2)
        : QStringLiteral("环形阵列失败：项目数必须大于 1"));
    SetToolMode(ToolMode::Select);
    return ok;
}

void CadView::ResetFilletChamferSession()
{
    filletPhase_ = FilletChamferPhase::First;
    filletMultiple_ = false;
    filletUndoDepth_ = 0;
    editHelperEntityId_ = 0;
    inputPoints_.clear();
    filletFirstEdge_.reset();
    filletChamferHudNotice_.clear();
    RestoreFilletPreviewSources();
    if (document_) {
        document_->ClearSelection();
    }
    if (cadScene_) {
        cadScene_->SetHoveredEntity(0);
        cadScene_->RebuildGrips();
    }
    ClearPreview();
}

FilletOptions CadView::ActiveFilletOptions(bool shiftCorner) const
{
    FilletOptions options;
    options.radius = shiftCorner ? 0.0 : filletRadius_;
    options.trim = filletTrim_;
    return options;
}

ChamferOptions CadView::ActiveChamferOptions(bool shiftCorner) const
{
    ChamferOptions options;
    options.method = chamferMethod_;
    options.trim = filletTrim_;
    if (shiftCorner) {
        options.distance1 = 0.0;
        options.distance2 = 0.0;
        options.length = 0.0;
        options.angleDegrees = 0.0;
        options.method = ChamferMethod::Distance;
        return options;
    }
    options.distance1 = chamferDist1_;
    options.distance2 = chamferDist2_;
    options.length = chamferLength_;
    options.angleDegrees = chamferAngleDeg_;
    return options;
}

QString CadView::FilletChamferSettingsStatus() const
{
    if (toolMode_ == ToolMode::Fillet) {
        return QStringLiteral("当前设置: 模式 = %1, 半径 = %2")
            .arg(filletTrim_ ? QStringLiteral("修剪") : QStringLiteral("不修剪"))
            .arg(filletRadius_, 0, 'f', 4);
    }
    if (chamferMethod_ == ChamferMethod::Angle) {
        return QStringLiteral("(%1 模式) 当前倒角 长度 = %2, 角度 = %3")
            .arg(filletTrim_ ? QStringLiteral("修剪") : QStringLiteral("不修剪"))
            .arg(chamferLength_, 0, 'f', 4)
            .arg(chamferAngleDeg_, 0, 'f', 4);
    }
    return QStringLiteral("(%1 模式) 当前倒角 Dist1 = %2, Dist2 = %3")
        .arg(filletTrim_ ? QStringLiteral("修剪") : QStringLiteral("不修剪"))
        .arg(chamferDist1_, 0, 'f', 4)
        .arg(chamferDist2_, 0, 'f', 4);
}

QString CadView::FilletChamferPromptText() const
{
    const bool fillet = toolMode_ == ToolMode::Fillet;
    switch (filletPhase_) {
    case FilletChamferPhase::Radius:
        return QStringLiteral("指定圆角半径 <%1>:").arg(filletRadius_, 0, 'f', 4);
    case FilletChamferPhase::Dist1:
        return QStringLiteral("指定第一个倒角距离 <%1>:").arg(chamferDist1_, 0, 'f', 4);
    case FilletChamferPhase::Dist2:
        return QStringLiteral("指定第二个倒角距离 <%1>:").arg(chamferDist2_, 0, 'f', 4);
    case FilletChamferPhase::AngleLen:
        return QStringLiteral("指定第一条直线上的倒角长度 <%1>:").arg(chamferLength_, 0, 'f', 4);
    case FilletChamferPhase::AngleVal:
        return QStringLiteral("指定从第一条直线起的倒角角度 <%1>:").arg(chamferAngleDeg_, 0, 'f', 4);
    case FilletChamferPhase::Trim:
        return QStringLiteral("输入修剪模式选项 [修剪(T)/不修剪(N)] <%1>:")
            .arg(filletTrim_ ? QStringLiteral("修剪") : QStringLiteral("不修剪"));
    case FilletChamferPhase::Method:
        return QStringLiteral("输入修剪方法 [距离(D)/角度(A)] <%1>:")
            .arg(chamferMethod_ == ChamferMethod::Angle ? QStringLiteral("角度") : QStringLiteral("距离"));
    case FilletChamferPhase::Polyline:
        return fillet
            ? QStringLiteral("选择二维多段线:")
            : QStringLiteral("选择二维多段线:");
    case FilletChamferPhase::Second:
        return fillet
            ? QStringLiteral("选择第二个对象，或按住 Shift 键选择对象以应用角点或")
            : QStringLiteral("选择第二条直线，或按住 Shift 键选择直线以应用角点或");
    case FilletChamferPhase::First:
    default:
        return fillet
            ? QStringLiteral("选择第一个对象或")
            : QStringLiteral("选择第一条直线或");
    }
}

QString CadView::FilletChamferStatusBarText() const
{
    const bool fillet = toolMode_ == ToolMode::Fillet;
    switch (filletPhase_) {
    case FilletChamferPhase::Radius:
    case FilletChamferPhase::Dist1:
    case FilletChamferPhase::Dist2:
    case FilletChamferPhase::AngleLen:
    case FilletChamferPhase::AngleVal:
    case FilletChamferPhase::Trim:
    case FilletChamferPhase::Method:
    case FilletChamferPhase::Polyline:
        return FilletChamferPromptText();
    case FilletChamferPhase::Second:
        return fillet
            ? QStringLiteral("选择第二个对象，或按住 Shift 键选择对象以应用角点或 [半径(R)]:")
            : QStringLiteral("选择第二条直线，或按住 Shift 键选择直线以应用角点或 [距离(D)/角度(A)/方法(E)]:");
    case FilletChamferPhase::First:
    default: {
        const QString prompt = fillet
            ? QStringLiteral("选择第一个对象或 [放弃(U)/多段线(P)/半径(R)/修剪(T)/多个(M)]:")
            : QStringLiteral("选择第一条直线或 [放弃(U)/多段线(P)/距离(D)/角度(A)/修剪(T)/方法(E)/多个(M)]:");
        return FilletChamferSettingsStatus() + QStringLiteral("    ") + prompt;
    }
    }
}

QString CadView::FilletChamferNumericDefaultText() const
{
    switch (filletPhase_) {
    case FilletChamferPhase::Radius:
        return QString::number(filletRadius_, 'f', 4);
    case FilletChamferPhase::Dist1:
        return QString::number(chamferDist1_, 'f', 4);
    case FilletChamferPhase::Dist2:
        return QString::number(chamferDist2_, 'f', 4);
    case FilletChamferPhase::AngleLen:
        return QString::number(chamferLength_, 'f', 4);
    case FilletChamferPhase::AngleVal:
        return QString::number(chamferAngleDeg_, 'f', 4);
    default:
        return {};
    }
}

bool CadView::IsFilletChamferNumericPhase() const
{
    return filletPhase_ == FilletChamferPhase::Radius
        || filletPhase_ == FilletChamferPhase::Dist1
        || filletPhase_ == FilletChamferPhase::Dist2
        || filletPhase_ == FilletChamferPhase::AngleLen
        || filletPhase_ == FilletChamferPhase::AngleVal;
}

QVector<DynCommandOption> CadView::CurrentFilletChamferHudOptions() const
{
    QVector<DynCommandOption> options;
    const bool fillet = toolMode_ == ToolMode::Fillet;
    switch (filletPhase_) {
    case FilletChamferPhase::Trim:
        options.push_back({QStringLiteral("T"), QStringLiteral("修剪(T)"), true});
        options.push_back({QStringLiteral("N"), QStringLiteral("不修剪(N)"), true});
        break;
    case FilletChamferPhase::Method:
        options.push_back({QStringLiteral("D"), QStringLiteral("距离(D)"), true});
        options.push_back({QStringLiteral("A"), QStringLiteral("角度(A)"), true});
        break;
    case FilletChamferPhase::Radius:
    case FilletChamferPhase::Dist1:
    case FilletChamferPhase::Dist2:
    case FilletChamferPhase::AngleLen:
    case FilletChamferPhase::AngleVal:
    case FilletChamferPhase::Polyline:
        break;
    case FilletChamferPhase::Second:
        if (fillet) {
            options.push_back({QStringLiteral("R"), QStringLiteral("半径(R)"), true});
        } else {
            options.push_back({QStringLiteral("D"), QStringLiteral("距离(D)"), true});
            options.push_back({QStringLiteral("A"), QStringLiteral("角度(A)"), true});
            options.push_back({QStringLiteral("E"), QStringLiteral("方法(E)"), true});
        }
        break;
    case FilletChamferPhase::First:
    default:
        options.push_back({QStringLiteral("U"), QStringLiteral("放弃(U)"), filletUndoDepth_ > 0});
        options.push_back({QStringLiteral("P"), QStringLiteral("多段线(P)"), true});
        if (fillet) {
            options.push_back({QStringLiteral("R"), QStringLiteral("半径(R)"), true});
        } else {
            options.push_back({QStringLiteral("D"), QStringLiteral("距离(D)"), true});
            options.push_back({QStringLiteral("A"), QStringLiteral("角度(A)"), true});
            options.push_back({QStringLiteral("E"), QStringLiteral("方法(E)"), true});
        }
        options.push_back({QStringLiteral("T"), QStringLiteral("修剪(T)"), true});
        options.push_back({QStringLiteral("M"), QStringLiteral("多个(M)"), true});
        break;
    }
    return options;
}

void CadView::UpdateFilletChamferHud()
{
    if (!IsFilletChamferCommand() || !viewport()) {
        return;
    }
    EnsureOptionInput();
    const bool keepEdit = optionInput_->IsKeywordEditing();
    optionInput_->ClearCycleList();
    if (!filletChamferHudNotice_.isEmpty()) {
        optionInput_->SetPrompt(filletChamferHudNotice_);
        optionInput_->SetOptions({});
        optionInput_->HideKeywordEdit();
        optionInput_->MoveToViewPosition(lastCursorViewportPos_);
        optionInput_->show();
        optionInput_->raise();
        return;
    }
    optionInput_->SetPrompt(FilletChamferPromptText());
    optionInput_->SetOptions(CurrentFilletChamferHudOptions());
    optionInput_->MoveToViewPosition(lastCursorViewportPos_);
    optionInput_->show();
    optionInput_->raise();
    if (IsFilletChamferNumericPhase() && !keepEdit) {
        optionInput_->BeginValueEdit(FilletChamferNumericDefaultText());
    } else if (!IsFilletChamferNumericPhase() && !keepEdit) {
        optionInput_->HideKeywordEdit();
    }
}

void CadView::ApplyFilletChamferKeyword(const QString& keyword)
{
    const QString k = keyword.trimmed().toUpper();
    bool okNum = false;
    const double number = keyword.trimmed().toDouble(&okNum);

    auto backToFirst = [this]() {
        filletPhase_ = FilletChamferPhase::First;
        editHelperEntityId_ = 0;
        inputPoints_.clear();
        EmitStatus(FilletChamferStatusBarText());
        UpdateFilletChamferHud();
    };

    if (filletPhase_ == FilletChamferPhase::Radius) {
        if (okNum) {
            SetFilletRadius(number);
        }
        backToFirst();
        return;
    }
    if (filletPhase_ == FilletChamferPhase::Dist1) {
        if (okNum) {
            chamferDist1_ = std::max(0.0, number);
            cornerDistance_ = chamferDist1_;
        }
        filletPhase_ = FilletChamferPhase::Dist2;
        EmitStatus(FilletChamferStatusBarText());
        UpdateFilletChamferHud();
        return;
    }
    if (filletPhase_ == FilletChamferPhase::Dist2) {
        if (okNum) {
            chamferDist2_ = std::max(0.0, number);
        }
        chamferMethod_ = ChamferMethod::Distance;
        backToFirst();
        return;
    }
    if (filletPhase_ == FilletChamferPhase::AngleLen) {
        if (okNum) {
            chamferLength_ = std::max(0.0, number);
        }
        filletPhase_ = FilletChamferPhase::AngleVal;
        EmitStatus(FilletChamferStatusBarText());
        UpdateFilletChamferHud();
        return;
    }
    if (filletPhase_ == FilletChamferPhase::AngleVal) {
        if (okNum) {
            chamferAngleDeg_ = number;
        }
        chamferMethod_ = ChamferMethod::Angle;
        backToFirst();
        return;
    }
    if (filletPhase_ == FilletChamferPhase::Trim) {
        if (k == QLatin1String("N") || k.contains(QStringLiteral("不修剪"))) {
            filletTrim_ = false;
        } else if (k == QLatin1String("T") || k.contains(QStringLiteral("修剪")) || k.isEmpty()) {
            filletTrim_ = true;
        }
        backToFirst();
        return;
    }
    if (filletPhase_ == FilletChamferPhase::Method) {
        if (k == QLatin1String("A") || k.contains(QStringLiteral("角度"))) {
            chamferMethod_ = ChamferMethod::Angle;
        } else {
            chamferMethod_ = ChamferMethod::Distance;
        }
        backToFirst();
        return;
    }

    if (k == QLatin1String("U") || k.contains(QStringLiteral("放弃"))) {
        if (filletUndoDepth_ > 0 && commandManager_ && commandManager_->CanUndo()) {
            commandManager_->Undo();
            --filletUndoDepth_;
            EmitStatus(QStringLiteral("已放弃上一次圆角/倒角"));
        } else {
            EmitStatus(QStringLiteral("没有可以放弃的圆角/倒角"));
        }
        backToFirst();
        return;
    }
    if (k == QLatin1String("P") || k.contains(QStringLiteral("多段线"))) {
        filletPhase_ = FilletChamferPhase::Polyline;
        editHelperEntityId_ = 0;
        inputPoints_.clear();
        EmitStatus(FilletChamferStatusBarText());
        UpdateFilletChamferHud();
        return;
    }
    if (k == QLatin1String("R") || k.contains(QStringLiteral("半径"))) {
        filletPhase_ = FilletChamferPhase::Radius;
        EmitStatus(FilletChamferStatusBarText());
        UpdateFilletChamferHud();
        return;
    }
    if (k == QLatin1String("D") || k.contains(QStringLiteral("距离"))) {
        filletPhase_ = FilletChamferPhase::Dist1;
        EmitStatus(FilletChamferStatusBarText());
        UpdateFilletChamferHud();
        return;
    }
    if (k == QLatin1String("A") || k.contains(QStringLiteral("角度"))) {
        filletPhase_ = FilletChamferPhase::AngleLen;
        EmitStatus(FilletChamferStatusBarText());
        UpdateFilletChamferHud();
        return;
    }
    if (k == QLatin1String("T") || k.contains(QStringLiteral("修剪"))) {
        filletPhase_ = FilletChamferPhase::Trim;
        EmitStatus(FilletChamferStatusBarText());
        UpdateFilletChamferHud();
        return;
    }
    if (k == QLatin1String("E") || k.contains(QStringLiteral("方法"))) {
        filletPhase_ = FilletChamferPhase::Method;
        EmitStatus(FilletChamferStatusBarText());
        UpdateFilletChamferHud();
        return;
    }
    if (k == QLatin1String("M") || k.contains(QStringLiteral("多个"))) {
        filletMultiple_ = true;
        EmitStatus(QStringLiteral("多个模式已打开。") + FilletChamferStatusBarText());
        UpdateFilletChamferHud();
        return;
    }
    if (!k.isEmpty()) {
        EmitStatus(QStringLiteral("无效的选项关键字"));
    }
}

void CadView::HandleFilletChamferPress(const GeoPoint& point, Qt::KeyboardModifiers modifiers)
{
    if (!document_ || !commandManager_) {
        return;
    }
    const EntityId hit = HitTest(point);
    const bool fillet = toolMode_ == ToolMode::Fillet;
    auto isFilletObject = [](EntityType type) {
        return type == EntityType::Line
            || type == EntityType::Arc
            || type == EntityType::Circle
            || type == EntityType::Polyline
            || type == EntityType::Rectangle
            || type == EntityType::Polygon;
    };
    auto isChamferObject = [](EntityType type) {
        return type == EntityType::Line
            || type == EntityType::Polyline
            || type == EntityType::Rectangle
            || type == EntityType::Polygon;
    };
    auto isPolylineLike = [](EntityType type) {
        return type == EntityType::Polyline
            || type == EntityType::Rectangle
            || type == EntityType::Polygon;
    };

    if (filletPhase_ == FilletChamferPhase::Polyline) {
        if (hit == 0) {
            EmitStatus(QStringLiteral("请选择一条二维多段线"));
            return;
        }
        const Entity* entity = document_->FindEntity(hit);
        if (!entity || !isPolylineLike(entity->Type())) {
            EmitStatus(QStringLiteral("多段线选项支持二维多段线、矩形和正多边形"));
            return;
        }
        CornerEditError error = CornerEditError::NoSolution;
        const bool ok = fillet
            ? commandManager_->FilletPolyline(hit, ActiveFilletOptions(false), &error)
            : commandManager_->ChamferPolyline(hit, ActiveChamferOptions(false), &error);
        if (ok) {
            filletChamferHudNotice_.clear();
            EmitStatus(fillet ? QStringLiteral("已为多段线添加圆角") : QStringLiteral("已为多段线添加倒角"));
        } else if (error == CornerEditError::RadiusTooLarge) {
            filletChamferHudNotice_ = QStringLiteral("圆角半径太大");
            EmitStatus(FilletChamferStatusBarText());
            UpdateFilletChamferHud();
            RepaintViewport();
            return;
        } else if (error == CornerEditError::DistanceTooLarge) {
            filletChamferHudNotice_ = QStringLiteral("倒角距离太大");
            EmitStatus(FilletChamferStatusBarText());
            UpdateFilletChamferHud();
            RepaintViewport();
            return;
        } else {
            filletChamferHudNotice_.clear();
            EmitStatus(QStringLiteral("无法处理该多段线：顶点间距不足或不是直线段"));
        }
        if (ok) {
            ++filletUndoDepth_;
        }
        if (filletMultiple_) {
            filletPhase_ = FilletChamferPhase::First;
            UpdateFilletChamferHud();
            return;
        }
        SetToolMode(ToolMode::Select);
        return;
    }

    if (filletPhase_ != FilletChamferPhase::Second) {
        if (hit == 0) {
            EmitStatus(fillet ? QStringLiteral("请选择第一个对象") : QStringLiteral("请选择第一条直线"));
            return;
        }
        const Entity* entity = document_->FindEntity(hit);
        if (!entity || (fillet ? !isFilletObject(entity->Type()) : !isChamferObject(entity->Type()))) {
            EmitStatus(fillet
                ? QStringLiteral("圆角支持直线、圆弧、圆、多段线、矩形和正多边形")
                : QStringLiteral("倒角支持直线、多段线、矩形和正多边形"));
            return;
        }
        editHelperEntityId_ = hit;
        inputPoints_.clear();
        inputPoints_.push_back(point);
        filletFirstEdge_ = EditKernel::PickedLinearEdge(*entity, point);
        if (document_) {
            document_->ClearSelection();
        }
        if (cadScene_) {
            cadScene_->SetHoveredEntity(0);
            cadScene_->RebuildGrips();
        }
        filletPhase_ = FilletChamferPhase::Second;
        ShowFilletFirstEdgeHighlight();
        EmitStatus(FilletChamferStatusBarText());
        UpdateFilletChamferHud();
        return;
    }

    if (hit == 0) {
        EmitStatus(QStringLiteral("请选择第二个对象"));
        return;
    }
    const Entity* entity2 = document_->FindEntity(hit);
    if (!entity2 || (fillet ? !isFilletObject(entity2->Type()) : !isChamferObject(entity2->Type()))) {
        EmitStatus(fillet
            ? QStringLiteral("圆角支持直线、圆弧、圆、多段线、矩形和正多边形")
            : QStringLiteral("倒角支持直线、多段线、矩形和正多边形"));
        return;
    }
    FinishFilletChamferPair(hit, point, modifiers.testFlag(Qt::ShiftModifier));
}

bool CadView::FinishFilletChamferPair(EntityId secondId, const GeoPoint& pick2, bool shiftCorner)
{
    if (!commandManager_ || inputPoints_.empty()) {
        return false;
    }
    const GeoPoint pick1 = inputPoints_.front();
    const bool fillet = toolMode_ == ToolMode::Fillet;
    CornerEditError error = CornerEditError::NoSolution;
    const bool ok = fillet
        ? commandManager_->FilletSelected(
            editHelperEntityId_, secondId, ActiveFilletOptions(shiftCorner), pick1, pick2, &error)
        : commandManager_->ChamferSelected(
            editHelperEntityId_, secondId, ActiveChamferOptions(shiftCorner), pick1, pick2, &error);
    if (!ok) {
        if (error == CornerEditError::RadiusTooLarge) {
            filletChamferHudNotice_ = QStringLiteral("圆角半径太大");
        } else if (error == CornerEditError::DistanceTooLarge) {
            filletChamferHudNotice_ = QStringLiteral("倒角距离太大");
        } else {
            filletChamferHudNotice_.clear();
            EmitStatus(fillet
                ? QStringLiteral("圆角失败：半径过大、边不相交或类型不支持")
                : QStringLiteral("倒角失败：距离过大、边不相交或类型不支持"));
            UpdateFilletChamferHud();
            return false;
        }
        EmitStatus(FilletChamferStatusBarText());
        UpdateFilletChamferHud();
        RepaintViewport();
        return false;
    }
    filletChamferHudNotice_.clear();
    ++filletUndoDepth_;
    ClearPreview();
    if (filletMultiple_) {
        editHelperEntityId_ = 0;
        inputPoints_.clear();
        filletPhase_ = FilletChamferPhase::First;
        if (document_) {
            document_->ClearSelection();
        }
        EmitStatus(FilletChamferStatusBarText());
        UpdateFilletChamferHud();
        return true;
    }
    EmitStatus(fillet ? QStringLiteral("圆角完成") : QStringLiteral("倒角完成"));
    SetToolMode(ToolMode::Select);
    return true;
}

void CadView::RestoreFilletPreviewSources()
{
    if (!cadScene_ || filletPreviewHiddenIds_.empty()) {
        filletPreviewHiddenIds_.clear();
        return;
    }
    for (const EntityId id : filletPreviewHiddenIds_) {
        cadScene_->SetEntityVisible(id, true);
    }
    filletPreviewHiddenIds_.clear();
}

void CadView::HideFilletPreviewSources(const std::vector<EntityId>& ids)
{
    if (filletPreviewHiddenIds_ == ids) {
        return;
    }
    RestoreFilletPreviewSources();
    if (!cadScene_) {
        return;
    }
    filletPreviewHiddenIds_ = ids;
    for (const EntityId id : filletPreviewHiddenIds_) {
        cadScene_->SetEntityVisible(id, false);
    }
}

void CadView::ShowFilletFirstEdgeHighlight()
{
    RestoreFilletPreviewSources();
    if (!filletFirstEdge_) {
        ClearPreview();
        return;
    }
    QPainterPath path;
    path.moveTo(ToScenePoint(filletFirstEdge_->Start()));
    path.lineTo(ToScenePoint(filletFirstEdge_->End()));
    SetPreviewPath(path);
}

void CadView::UpdateFilletChamferPreview(const GeoPoint& cursor)
{
    if (!document_ || !commandManager_ || filletPhase_ != FilletChamferPhase::Second
        || editHelperEntityId_ == 0 || inputPoints_.empty()) {
        ClearPreview();
        return;
    }
    if (cadScene_) {
        cadScene_->SetHoveredEntity(0);
    }
    const EntityId hit = HitTest(cursor);
    if (hit == 0) {
        filletChamferHudNotice_.clear();
        ShowFilletFirstEdgeHighlight();
        EmitStatus(FilletChamferStatusBarText());
        return;
    }
    const bool fillet = toolMode_ == ToolMode::Fillet;
    CornerEditError error = CornerEditError::NoSolution;
    std::optional<CornerEditOutput> output;
    if (fillet) {
        output = EditKernel::FilletEntities(
            *document_,
            editHelperEntityId_,
            hit,
            ActiveFilletOptions(false),
            inputPoints_.front(),
            cursor,
            &error);
    } else {
        output = EditKernel::ChamferEntities(
            *document_,
            editHelperEntityId_,
            hit,
            ActiveChamferOptions(false),
            inputPoints_.front(),
            cursor,
            &error);
    }
    if (!output) {
        ShowFilletFirstEdgeHighlight();
        if (error == CornerEditError::RadiusTooLarge) {
            filletChamferHudNotice_ = QStringLiteral("圆角半径太大");
        } else if (error == CornerEditError::DistanceTooLarge) {
            filletChamferHudNotice_ = QStringLiteral("倒角距离太大");
        } else {
            filletChamferHudNotice_.clear();
        }
        EmitStatus(FilletChamferStatusBarText());
        RepaintViewport();
        return;
    }
    filletChamferHudNotice_.clear();
    EmitStatus(FilletChamferStatusBarText());
    GeoShape shape;
    for (const auto& entity : output->created) {
        if (entity) {
            shape.Append(entity->Shaper());
        }
    }
    if (!output->removeIds.empty()) {
        HideFilletPreviewSources(output->removeIds);
    } else {
        RestoreFilletPreviewSources();
    }
    const CurveDisplayOptions display = MakeCurveDisplayOptions();
    SetPreviewPath(GeoShapePainter::BuildPath(shape, nullptr, &display));
}

QString CadView::PolylineOptionPrompt() const
{
    const bool canClose = inputPoints_.size() >= 2;
    if (drawOptions_.polylineArcMode) {
        return canClose
            ? QStringLiteral("指定圆弧的端点或 [角度(A)/圆心(CE)/闭合(CL)/方向(D)/半宽(H)/直线(L)/半径(R)/第二个点(S)/放弃(U)/宽度(W)]")
            : QStringLiteral("指定圆弧的端点或 [角度(A)/圆心(CE)/方向(D)/半宽(H)/直线(L)/半径(R)/第二个点(S)/放弃(U)/宽度(W)]");
    }
    if (drawOptions_.polylineLengthMode) {
        return QStringLiteral("指定直线的长度");
    }
    return canClose
        ? QStringLiteral("指定下一个点或 [圆弧(A)/闭合(C)/半宽(H)/长度(L)/放弃(U)/宽度(W)]")
        : QStringLiteral("指定下一个点或 [圆弧(A)/半宽(H)/长度(L)/放弃(U)/宽度(W)]");
}

void CadView::ResetPolylineDrawOptions()
{
    drawOptions_.polylineArcMode = false;
    drawOptions_.polylineClosed = false;
    drawOptions_.polylineLengthMode = false;
    drawOptions_.polylineBulges.clear();
}

GeoPoint CadView::PolylineDrawEndTangent() const
{
    const GeoPoint* fallback = lastArcContinue_.valid ? &lastArcContinue_.tangentDirection : nullptr;
    return DrawAlgorithms::PolylineEndTangentUnit(
        inputPoints_, drawOptions_.polylineBulges, fallback);
}

void CadView::CommitLastPolylineSegment()
{
    if (inputPoints_.size() < 2) {
        return;
    }
    if (drawOptions_.polylineBulges.size() < inputPoints_.size()) {
        drawOptions_.polylineBulges.resize(inputPoints_.size(), 0.0);
    }
    const std::size_t seg = inputPoints_.size() - 2;
    if (!drawOptions_.polylineArcMode) {
        drawOptions_.polylineBulges[seg] = 0.0;
        return;
    }
    std::vector<GeoPoint> previous(inputPoints_.begin(), inputPoints_.end() - 1);
    std::vector<double> prevBulges = drawOptions_.polylineBulges;
    if (prevBulges.size() > previous.size()) {
        prevBulges.resize(previous.size());
    }
    const GeoPoint* fallback = lastArcContinue_.valid ? &lastArcContinue_.tangentDirection : nullptr;
    GeoPoint tangent = DrawAlgorithms::PolylineEndTangentUnit(previous, prevBulges, fallback);
    if (drawOptions_.clockwise) {
        tangent = GeoPoint(-tangent.X(), -tangent.Y());
    }
    if (auto arc = DrawAlgorithms::ArcFromTangentStartEnd(previous.back(), tangent, inputPoints_.back())) {
        drawOptions_.polylineBulges[seg] = DrawAlgorithms::BulgeFromArc(*arc);
    } else {
        drawOptions_.polylineBulges[seg] = 0.0;
    }
}

void CadView::HandlePolylineOptionKey(int key)
{
    switch (key) {
    case Qt::Key_A:
        ApplyPolylineOptionKeyword(QStringLiteral("A"));
        break;
    case Qt::Key_C:
        ApplyPolylineOptionKeyword(drawOptions_.polylineArcMode
            ? QStringLiteral("CL")
            : QStringLiteral("C"));
        break;
    case Qt::Key_U:
        ApplyPolylineOptionKeyword(QStringLiteral("U"));
        break;
    case Qt::Key_L:
        ApplyPolylineOptionKeyword(QStringLiteral("L"));
        break;
    case Qt::Key_W:
        ApplyPolylineOptionKeyword(QStringLiteral("W"));
        break;
    case Qt::Key_H:
        ApplyPolylineOptionKeyword(QStringLiteral("H"));
        break;
    default:
        break;
    }
}

QVector<DynCommandOption> CadView::CurrentPolylineOptions() const
{
    QVector<DynCommandOption> options;
    const bool canClose = inputPoints_.size() >= 3;
    if (drawOptions_.polylineArcMode) {
        options.push_back({QStringLiteral("A"), QStringLiteral("角度"), true});
        options.push_back({QStringLiteral("CE"), QStringLiteral("圆心"), true});
        options.push_back({QStringLiteral("CL"), QStringLiteral("闭合"), canClose});
        options.push_back({QStringLiteral("D"), QStringLiteral("方向"), true});
        options.push_back({QStringLiteral("H"), QStringLiteral("半宽"), true});
        options.push_back({QStringLiteral("L"), QStringLiteral("直线"), true});
        options.push_back({QStringLiteral("R"), QStringLiteral("半径"), true});
        options.push_back({QStringLiteral("S"), QStringLiteral("第二个点"), true});
        options.push_back({QStringLiteral("U"), QStringLiteral("放弃"), true});
        options.push_back({QStringLiteral("W"), QStringLiteral("宽度"), true});
    } else {
        options.push_back({QStringLiteral("A"), QStringLiteral("圆弧"), true});
        options.push_back({QStringLiteral("C"), QStringLiteral("闭合"), canClose});
        options.push_back({QStringLiteral("H"), QStringLiteral("半宽"), true});
        options.push_back({QStringLiteral("L"), QStringLiteral("长度"), true});
        options.push_back({QStringLiteral("U"), QStringLiteral("放弃"), true});
        options.push_back({QStringLiteral("W"), QStringLiteral("宽度"), true});
    }
    return options;
}

QString CadView::PolylineDynPromptText() const
{
    if (drawOptions_.polylineLengthMode) {
        return QStringLiteral("指定直线的长度");
    }
    if (drawOptions_.polylineArcMode) {
        return QStringLiteral("指定圆弧的端点或");
    }
    return QStringLiteral("指定下一点或");
}

void CadView::ApplyPolylineOptionKeyword(const QString& keyword)
{
    const QString k = keyword.trimmed().toUpper();
    if (k.isEmpty()) {
        return;
    }

    if (!drawOptions_.polylineArcMode && k == QLatin1String("A")) {
        drawOptions_.polylineArcMode = true;
        drawOptions_.polylineLengthMode = false;
        EmitStatus(PolylineOptionPrompt());
    } else if (drawOptions_.polylineArcMode && k == QLatin1String("A")) {
        EmitStatus(QStringLiteral("当前版本暂不支持圆弧角度选项"));
        return;
    } else if (k == QLatin1String("CE")) {
        EmitStatus(QStringLiteral("当前版本暂不支持圆心选项"));
        return;
    } else if (k == QLatin1String("D")) {
        EmitStatus(QStringLiteral("当前版本暂不支持方向选项"));
        return;
    } else if (k == QLatin1String("R")) {
        EmitStatus(QStringLiteral("当前版本暂不支持半径选项"));
        return;
    } else if (k == QLatin1String("S")) {
        EmitStatus(QStringLiteral("当前版本暂不支持第二个点选项"));
        return;
    } else if (k == QLatin1String("L")) {
        if (drawOptions_.polylineArcMode) {
            drawOptions_.polylineArcMode = false;
            drawOptions_.polylineLengthMode = false;
            EmitStatus(PolylineOptionPrompt());
        } else {
            if (inputPoints_.size() < 2 && !lastArcContinue_.valid) {
                EmitStatus(QStringLiteral("没有上一段，无法指定长度"));
                return;
            }
            drawOptions_.polylineLengthMode = true;
            HideOptionInput();
            EmitStatus(QStringLiteral("指定直线的长度"));
        }
    } else if (k == QLatin1String("U")) {
        UndoPolylineVertex();
        return;
    } else if (k == QLatin1String("C") || k == QLatin1String("CL")) {
        ClosePolyline();
        return;
    } else if (k == QLatin1String("W")) {
        EmitStatus(QStringLiteral("当前版本暂不支持宽度"));
        return;
    } else if (k == QLatin1String("H")) {
        EmitStatus(QStringLiteral("当前版本暂不支持半宽"));
        return;
    } else {
        EmitStatus(QStringLiteral("无效的选项关键字"));
        return;
    }

    if (viewport() && IsContinuousPolylineDraw() && IsDrawInProgress()) {
        UpdatePolylineOptionInput(lastCursorViewportPos_);
        UpdatePreview(ApplySnap(MapToModel(viewport()->mapFromGlobal(QCursor::pos()))));
    }
}

void CadView::EnsureOptionInput()
{
    if (!optionInput_) {
        optionInput_ = new DynamicOptionInputWidget(viewport());
        optionInput_->hide();
    }
    optionInput_->onOptionChosen = [this](const QString& keyword) {
        EndPolylineKeywordEditUi();
        if (IsSplineGripEditing()) {
            ApplySplineGripEditKeyword(keyword);
        } else if (IsSplineDraw()) {
            ApplySplineOptionKeyword(keyword);
        } else if (IsFilletChamferCommand()) {
            ApplyFilletChamferKeyword(keyword);
        } else if (IsPolygonCommand()) {
            ApplyPolygonKeyword(keyword);
        } else if (IsArrayCommand()) {
            ApplyArrayKeyword(keyword);
        } else {
            ApplyPolylineOptionKeyword(keyword);
        }
        if (optionInput_ && optionInput_->IsKeywordEditing()) {
            optionInput_->FocusKeywordEdit();
        } else {
            setFocus(Qt::OtherFocusReason);
        }
    };
    optionInput_->onKeywordEditCancelled = [this]() {
        EndPolylineKeywordEditUi();
        if (IsFilletChamferCommand()) {
            EmitStatus(FilletChamferStatusBarText());
            UpdateFilletChamferHud();
        } else if (IsPolygonCommand()) {
            EmitStatus(PolygonPromptText());
            UpdatePolygonHud();
        } else if (IsArrayCommand()) {
            EmitStatus(ArrayPromptText());
            UpdateArrayHud();
        } else {
            EmitStatus(IsSplineDraw() ? SplineOptionPrompt() : PolylineOptionPrompt());
        }
        setFocus(Qt::OtherFocusReason);
        if (viewport()) {
            UpdatePreview(ApplySnap(MapToModel(viewport()->mapFromGlobal(QCursor::pos()))));
        }
    };
    optionInput_->onEscapeRequested = [this]() {
        if (!CancelActiveCommand()) {
            optionInput_->CancelKeywordEdit();
        }
    };
    optionInput_->onInvalidKeyword = [this](const QString& message) {
        EmitStatus(message);
    };
}

void CadView::HideOptionInput()
{
    if (optionInput_) {
        optionInput_->HideKeywordEdit();
        optionInput_->ClearCycleList();
        optionInput_->hide();
    }
    EndPolylineKeywordEditUi();
}

void CadView::UpdatePolylineOptionInput(const QPoint& viewPos)
{
    if (!IsContinuousPolylineDraw() || inputPoints_.empty() || drawOptions_.polylineLengthMode) {
        if (!IsPolylineOptionEditing()) {
            HideOptionInput();
        }
        return;
    }
    EnsureOptionInput();
    optionInput_->SetPrompt(PolylineDynPromptText());
    optionInput_->SetOptions(CurrentPolylineOptions());
    optionInput_->MoveToViewPosition(viewPos);
    optionInput_->show();
    optionInput_->raise();
}

void CadView::UpdateSplineOptionInput(const QPoint& viewPos)
{
    if (!IsSplineDraw()) {
        if (!IsPolylineOptionEditing()) {
            HideOptionInput();
        }
        return;
    }
    EnsureOptionInput();
    optionInput_->SetPrompt(SplineDynPromptText());
    optionInput_->SetOptions(CurrentSplineOptions());
    optionInput_->MoveToViewPosition(viewPos);
    optionInput_->show();
    optionInput_->raise();
}

bool CadView::IsPolylineOptionEditing() const
{
    return polylineKeywordEditActive_
        || (optionInput_ && optionInput_->IsKeywordEditing());
}

void CadView::BeginPolylineKeywordTyping(const QString& text)
{
    polylineKeywordEditActive_ = true;
    EnsureOptionInput();
    if (IsSplineGripEditing()) {
        const GeoPoint cursor = ApplySnap(MapToModel(lastCursorViewportPos_));
        UpdateSplineGripEditHud(lastCursorViewportPos_, cursor);
    } else if (IsSplineDraw()) {
        UpdateSplineOptionInput(lastCursorViewportPos_);
    } else if (IsFilletChamferCommand()) {
        optionInput_->ClearCycleList();
        optionInput_->SetPrompt(FilletChamferPromptText());
        optionInput_->SetOptions(CurrentFilletChamferHudOptions());
        optionInput_->MoveToViewPosition(lastCursorViewportPos_);
        optionInput_->show();
        optionInput_->raise();
    } else if (IsPolygonCommand()) {
        optionInput_->ClearCycleList();
        optionInput_->SetPrompt(PolygonPromptText());
        optionInput_->SetOptions(CurrentPolygonHudOptions());
        optionInput_->MoveToViewPosition(lastCursorViewportPos_);
        optionInput_->show();
        optionInput_->raise();
    } else if (IsArrayCommand()) {
        optionInput_->ClearCycleList();
        optionInput_->SetPrompt(ArrayPromptText());
        optionInput_->SetOptions(CurrentArrayHudOptions());
        optionInput_->MoveToViewPosition(lastCursorViewportPos_);
        optionInput_->show();
        optionInput_->raise();
    } else {
        UpdatePolylineOptionInput(lastCursorViewportPos_);
    }
    optionInput_->BeginTyping(text);
    if (polarInput_) {
        polarInput_->SetFieldsClickable(false);
    }
    if (pointInput_) {
        pointInput_->SetFieldsClickable(false);
    }
    optionInput_->FocusKeywordEdit();
}

void CadView::EndPolylineKeywordEditUi()
{
    polylineKeywordEditActive_ = false;
    if (polarInput_) {
        polarInput_->SetFieldsClickable(true);
    }
    if (pointInput_) {
        pointInput_->SetFieldsClickable(true);
    }
}

void CadView::KeepPolylineKeywordEditPointer(const QPoint& viewportPos)
{
    UpdateCursorOverlay(viewportPos);
    if (optionInput_) {
        optionInput_->raise();
    }
}

bool CadView::UndoPolylineVertex()
{
    if (!IsContinuousPolylineDraw()) {
        return false;
    }
    if (inputPoints_.empty()) {
        return false;
    }
    if (inputPoints_.size() == 1) {
        ResetTransientState();
        EmitStatus(QStringLiteral("指定起点"));
        return true;
    }
    inputPoints_.pop_back();
    if (drawOptions_.polylineBulges.size() > inputPoints_.size()) {
        drawOptions_.polylineBulges.resize(inputPoints_.size());
    }
    drawOptions_.polylineLengthMode = false;
    EmitStatus(PolylineOptionPrompt());
    ClearPreview();
    if (viewport()) {
        UpdatePreview(ApplySnap(MapToModel(viewport()->mapFromGlobal(QCursor::pos()))));
    }
    RefreshViewport();
    return true;
}

bool CadView::ClosePolyline()
{
    if (!IsContinuousPolylineDraw() || !commandManager_) {
        return false;
    }
    if (inputPoints_.size() < 3) {
        EmitStatus(QStringLiteral("至少需要两段才能闭合"));
        return true;
    }
    if (drawOptions_.polylineBulges.size() < inputPoints_.size()) {
        drawOptions_.polylineBulges.resize(inputPoints_.size(), 0.0);
    }
    if (drawOptions_.polylineArcMode
        && Distance2D(inputPoints_.front(), inputPoints_.back()) > Tolerance::Model) {
        const GeoPoint* fallback = lastArcContinue_.valid ? &lastArcContinue_.tangentDirection : nullptr;
        GeoPoint tangent = DrawAlgorithms::PolylineEndTangentUnit(
            inputPoints_, drawOptions_.polylineBulges, fallback);
        if (drawOptions_.clockwise) {
            tangent = GeoPoint(-tangent.X(), -tangent.Y());
        }
        if (auto arc = DrawAlgorithms::ArcFromTangentStartEnd(
                inputPoints_.back(), tangent, inputPoints_.front())) {
            drawOptions_.polylineBulges.back() = DrawAlgorithms::BulgeFromArc(*arc);
        } else {
            drawOptions_.polylineBulges.back() = 0.0;
        }
    } else {
        drawOptions_.polylineBulges.back() = 0.0;
    }
    drawOptions_.polylineClosed = true;
    FinishMultiPointDraw();
    EmitStatus(QStringLiteral("多段线已闭合"));
    return true;
}

QVector<DynCommandOption> CadView::CurrentSplineOptions() const
{
    QVector<DynCommandOption> options;
    if (inputPoints_.empty()) {
        options.push_back({QStringLiteral("M"), QStringLiteral("方式"), true});
        options.push_back({QStringLiteral("K"), QStringLiteral("节点"), true});
        options.push_back({QStringLiteral("O"), QStringLiteral("对象"), true});
        return options;
    }
    if (inputPoints_.size() >= 3) {
        options.push_back({QStringLiteral("C"), QStringLiteral("闭合"), true});
    }
    options.push_back({QStringLiteral("T"), QStringLiteral("起点切向"), true});
    options.push_back({QStringLiteral("L"), QStringLiteral("公差"), true});
    if (inputPoints_.size() >= 2) {
        options.push_back({QStringLiteral("U"), QStringLiteral("放弃"), true});
    }
    return options;
}

QString CadView::SplineDynPromptText() const
{
    return inputPoints_.empty()
        ? QStringLiteral("指定第一个点或")
        : QStringLiteral("输入下一个点或");
}

QString CadView::SplineOptionPrompt() const
{
    if (inputPoints_.empty()) {
        return QStringLiteral("指定第一个点或 [方式(M)/节点(K)/对象(O)]");
    }
    if (inputPoints_.size() == 1) {
        return QStringLiteral("输入下一个点或 [起点切向(T)/公差(L)]");
    }
    if (inputPoints_.size() == 2) {
        return QStringLiteral("输入下一个点或 [起点切向(T)/公差(L)/放弃(U)]");
    }
    return QStringLiteral("输入下一个点或 [闭合(C)/起点切向(T)/公差(L)/放弃(U)]");
}

void CadView::ApplySplineOptionKeyword(const QString& keyword)
{
    const QString k = keyword.trimmed().toUpper();
    if (k.isEmpty()) {
        return;
    }

    if (k == QLatin1String("M")) {
        drawOptions_.method = (drawOptions_.method == DrawMethod::SplineFitPoints)
            ? DrawMethod::SplineControlPoints
            : DrawMethod::SplineFitPoints;
        EmitStatus(drawOptions_.method == DrawMethod::SplineFitPoints
            ? QStringLiteral("方式：拟合点")
            : QStringLiteral("方式：控制点"));
    } else if (k == QLatin1String("K")) {
        EmitStatus(QStringLiteral("当前版本暂不支持节点选项"));
        return;
    } else if (k == QLatin1String("O")) {
        EmitStatus(QStringLiteral("当前版本暂不支持从对象转换"));
        return;
    } else if (k == QLatin1String("T")) {
        EmitStatus(QStringLiteral("当前版本暂不支持起点切向"));
        return;
    } else if (k == QLatin1String("L")) {
        EmitStatus(QStringLiteral("当前版本暂不支持公差选项"));
        return;
    } else if (k == QLatin1String("U")) {
        UndoSplineVertex();
        return;
    } else if (k == QLatin1String("C")) {
        CloseSpline();
        return;
    } else {
        EmitStatus(QStringLiteral("无效的选项关键字"));
        return;
    }

    if (viewport() && IsSplineDraw()) {
        if (inputPoints_.empty()) {
            UpdatePointInput(lastCursorViewportPos_, ApplySnap(MapToModel(lastCursorViewportPos_)));
        } else {
            UpdateSplineOptionInput(lastCursorViewportPos_);
            UpdatePreview(ApplySnap(MapToModel(viewport()->mapFromGlobal(QCursor::pos()))));
        }
    }
}

bool CadView::UndoSplineVertex()
{
    if (!IsSplineDraw()) {
        return false;
    }
    if (inputPoints_.empty()) {
        return false;
    }
    if (inputPoints_.size() == 1) {
        ResetTransientState();
        EmitStatus(SplineOptionPrompt());
        if (ShouldShowDynPointInput() && viewport()) {
            UpdatePointInput(
                lastCursorViewportPos_,
                ApplySnap(MapToModel(lastCursorViewportPos_)));
        }
        return true;
    }
    inputPoints_.pop_back();
    EmitStatus(SplineOptionPrompt());
    ClearPreview();
    if (viewport()) {
        UpdatePreview(ApplySnap(MapToModel(viewport()->mapFromGlobal(QCursor::pos()))));
    }
    RefreshViewport();
    return true;
}

bool CadView::CloseSpline()
{
    if (!IsSplineDraw() || !commandManager_) {
        return false;
    }
    if (inputPoints_.size() < 3) {
        EmitStatus(QStringLiteral("至少需要三个点才能闭合"));
        return true;
    }
    if (Distance2D(inputPoints_.front(), inputPoints_.back()) > Tolerance::Model) {
        inputPoints_.push_back(inputPoints_.front());
    }
    FinishMultiPointDraw();
    EmitStatus(QStringLiteral("样条曲线已闭合"));
    return true;
}

bool CadView::SupportsPolarTracking() const
{
    GeoPoint unused;
    return TryGetPolarBasePoint(&unused);
}

bool CadView::TryGetPolarBasePoint(GeoPoint* baseOut) const
{
    if (!baseOut) {
        return false;
    }

    if (splineGripEditing_) {
        *baseOut = gripStartPoint_;
        return true;
    }

    const DrawMethod method = drawOptions_.method;

    switch (toolMode_) {
    case ToolMode::DrawLine:
        if (method != DrawMethod::LineTwoPoints || inputPoints_.empty()) {
            return false;
        }
        *baseOut = inputPoints_.back();
        return true;

    case ToolMode::DrawPolyline:
        if (inputPoints_.empty()) {
            return false;
        }
        *baseOut = inputPoints_.back();
        return true;

    case ToolMode::DrawSpline:
        if (inputPoints_.empty()) {
            return false;
        }
        *baseOut = inputPoints_.back();
        return true;

    case ToolMode::DrawArc:
        // 圆心、起点、端点/角度：第二/第三点均以圆心为极轴原点。
        if (method == DrawMethod::ArcCenterStartEnd
            || method == DrawMethod::ArcCenterStartAngle) {
            if (inputPoints_.size() == 1 || inputPoints_.size() == 2) {
                *baseOut = inputPoints_.front();
                return true;
            }
            return false;
        }
        // 圆心、起点、长度：第二点从圆心（定半径）；第三点从起点测弦长。
        if (method == DrawMethod::ArcCenterStartChord) {
            if (inputPoints_.size() == 1) {
                *baseOut = inputPoints_.front();
                return true;
            }
            if (inputPoints_.size() == 2) {
                *baseOut = inputPoints_[1];
                return true;
            }
            return false;
        }
        // 三点圆弧：从上一顶点到光标做极轴追踪预览。
        if (method == DrawMethod::ArcThreePoints) {
            if (inputPoints_.empty() || inputPoints_.size() >= 3) {
                return false;
            }
            *baseOut = inputPoints_.back();
            return true;
        }
        // 起点、圆心、端点 / 起点、圆心、角度：
        // 第二点从起点；第三点从圆心。
        if (method == DrawMethod::ArcStartCenterEnd
            || method == DrawMethod::ArcStartCenterAngle) {
            if (inputPoints_.size() == 1) {
                *baseOut = inputPoints_.front();
                return true;
            }
            if (inputPoints_.size() == 2) {
                *baseOut = inputPoints_[1];
                return true;
            }
            return false;
        }
        // 起点、圆心、弦长：第二/第三点均从起点测距（第三点弦长）。
        if (method == DrawMethod::ArcStartCenterChord) {
            if (inputPoints_.size() == 1 || inputPoints_.size() == 2) {
                *baseOut = inputPoints_.front();
                return true;
            }
            return false;
        }
        // 起点、端点、角度：第二点从起点；第三点夹角极轴以「起点」为原点（角度弧/0° 基线自起点）。
        if (method == DrawMethod::ArcStartEndAngle) {
            if (inputPoints_.size() == 1 || inputPoints_.size() == 2) {
                *baseOut = inputPoints_.front();
                return true;
            }
            return false;
        }
        // 起点、端点、半径：第二点从起点；第三点以上一顶点（端点）为极轴基点。
        if (method == DrawMethod::ArcStartEndRadius) {
            if (inputPoints_.size() == 1) {
                *baseOut = inputPoints_.front();
                return true;
            }
            if (inputPoints_.size() == 2) {
                *baseOut = inputPoints_.back();
                return true;
            }
            return false;
        }
        // 起点、端点、方向：第二点从起点；第三点极轴也以起点为基点。
        if (method == DrawMethod::ArcStartEndDirection) {
            if (inputPoints_.size() == 1 || inputPoints_.size() == 2) {
                *baseOut = inputPoints_.front();
                return true;
            }
            return false;
        }
        // 连续：从上一段末端切线起点到光标做极轴追踪。
        if (method == DrawMethod::ArcContinue && lastArcContinue_.valid) {
            *baseOut = lastArcContinue_.start;
            return true;
        }
        return false;

    case ToolMode::DrawCircle:
        if ((method == DrawMethod::CircleCenterRadius
                || method == DrawMethod::CircleCenterDiameter
                || method == DrawMethod::CircleTwoPoints)
            && inputPoints_.size() == 1) {
            *baseOut = inputPoints_.front();
            return true;
        }
        if (method == DrawMethod::CircleThreePoints) {
            if (inputPoints_.size() == 1) {
                *baseOut = inputPoints_.front();
                return true;
            }
            if (inputPoints_.size() == 2) {
                *baseOut = inputPoints_.back();
                return true;
            }
        }
        if (method == DrawMethod::CircleTanTanRadius && inputPoints_.size() == 2) {
            *baseOut = inputPoints_.back();
            return true;
        }
        return false;

    default:
        return false;
    }
}

bool CadView::IsPolarAngleHintOnlyGuide() const
{
    // 起点-圆心-端点第三点：0° 水平基线 + 极角弧，不画半径整圆。
    return IsArcStartCenterEndEndPick();
}

bool CadView::IsArcStartCenterEndEndPick() const
{
    return toolMode_ == ToolMode::DrawArc
        && drawOptions_.method == DrawMethod::ArcStartCenterEnd
        && inputPoints_.size() == 2;
}

bool CadView::IsArcIncludedAnglePick() const
{
    return toolMode_ == ToolMode::DrawArc
        && (drawOptions_.method == DrawMethod::ArcStartCenterAngle
            || drawOptions_.method == DrawMethod::ArcCenterStartAngle)
        && inputPoints_.size() == 2;
}

GeoPoint CadView::ArcIncludedAngleStartPoint() const
{
    if (drawOptions_.method == DrawMethod::ArcCenterStartAngle) {
        return inputPoints_[1];
    }
    return inputPoints_[0];
}

GeoPoint CadView::ArcIncludedAngleCenterPoint() const
{
    if (drawOptions_.method == DrawMethod::ArcCenterStartAngle) {
        return inputPoints_[0];
    }
    return inputPoints_[1];
}

bool CadView::IsArcChordLengthPick() const
{
    return toolMode_ == ToolMode::DrawArc
        && (drawOptions_.method == DrawMethod::ArcStartCenterChord
            || drawOptions_.method == DrawMethod::ArcCenterStartChord)
        && inputPoints_.size() == 2;
}

GeoPoint CadView::ArcChordLengthStartPoint() const
{
    if (drawOptions_.method == DrawMethod::ArcCenterStartChord) {
        return inputPoints_[1];
    }
    return inputPoints_[0];
}

GeoPoint CadView::ArcChordLengthCenterPoint() const
{
    if (drawOptions_.method == DrawMethod::ArcCenterStartChord) {
        return inputPoints_[0];
    }
    return inputPoints_[1];
}

bool CadView::IsArcStartEndRadiusPick() const
{
    return toolMode_ == ToolMode::DrawArc
        && drawOptions_.method == DrawMethod::ArcStartEndRadius
        && inputPoints_.size() == 2;
}

bool CadView::IsArcStartEndDirectionPick() const
{
    // 第三点：指定起点切线方向。
    return toolMode_ == ToolMode::DrawArc
        && drawOptions_.method == DrawMethod::ArcStartEndDirection
        && inputPoints_.size() == 2;
}

bool CadView::IsArcStartEndAnglePick() const
{
    return toolMode_ == ToolMode::DrawArc
        && drawOptions_.method == DrawMethod::ArcStartEndAngle
        && inputPoints_.size() == 2;
}

bool CadView::IsArcCenterStartEndStartPick() const
{
    return toolMode_ == ToolMode::DrawArc
        && (drawOptions_.method == DrawMethod::ArcCenterStartEnd
            || drawOptions_.method == DrawMethod::ArcCenterStartAngle
            || drawOptions_.method == DrawMethod::ArcCenterStartChord)
        && inputPoints_.size() == 1;
}

bool CadView::IsArcCenterStartEndEndPick() const
{
    return toolMode_ == ToolMode::DrawArc
        && drawOptions_.method == DrawMethod::ArcCenterStartEnd
        && inputPoints_.size() == 2;
}

bool CadView::IsArcContinuePick() const
{
    return toolMode_ == ToolMode::DrawArc
        && drawOptions_.method == DrawMethod::ArcContinue
        && lastArcContinue_.valid;
}

bool CadView::IsCircleCenterRadiusPick() const
{
    return toolMode_ == ToolMode::DrawCircle
        && drawOptions_.method == DrawMethod::CircleCenterRadius
        && inputPoints_.size() == 1;
}

bool CadView::IsCircleCenterDiameterPick() const
{
    return toolMode_ == ToolMode::DrawCircle
        && drawOptions_.method == DrawMethod::CircleCenterDiameter
        && inputPoints_.size() == 1;
}

bool CadView::IsCircleCenterPolarPick() const
{
    return IsCircleCenterRadiusPick() || IsCircleCenterDiameterPick() || IsCircleTanTanRadiusValuePick();
}

bool CadView::IsCircleTanTanRadiusObjectPick() const
{
    return toolMode_ == ToolMode::DrawCircle
        && drawOptions_.method == DrawMethod::CircleTanTanRadius
        && inputPoints_.size() < 2;
}

bool CadView::IsCircleTanTanRadiusValuePick() const
{
    return toolMode_ == ToolMode::DrawCircle
        && drawOptions_.method == DrawMethod::CircleTanTanRadius
        && inputPoints_.size() == 2;
}

bool CadView::IsCircleTangentObjectPick() const
{
    if (toolMode_ != ToolMode::DrawCircle) {
        return false;
    }
    if (drawOptions_.method == DrawMethod::CircleTanTanRadius && inputPoints_.size() < 2) {
        return true;
    }
    if (drawOptions_.method == DrawMethod::CircleTanTanTan && inputPoints_.size() < 3) {
        return true;
    }
    return false;
}

GeoPoint CadView::ResolveTtrObjectPickPoint(const GeoPoint& rawPoint) const
{
    if (const std::optional<ObjectSnapHit> snap = FindObjectSnapHit(rawPoint)) {
        // 圆心/几何中心会落到对象内部，TTR 需要切线对象上的点；其它捕捉点仍采用。
        if (snap->kind != ObjectSnapKind::Center
            && snap->kind != ObjectSnapKind::GeometricCenter
            && HitTest(snap->point) != 0) {
            return snap->point;
        }
    }
    return rawPoint;
}

bool CadView::IsCircleTwoPointsPick() const
{
    return toolMode_ == ToolMode::DrawCircle
        && drawOptions_.method == DrawMethod::CircleTwoPoints
        && inputPoints_.size() == 1;
}

bool CadView::IsCircleThreePointsSecondPick() const
{
    return toolMode_ == ToolMode::DrawCircle
        && drawOptions_.method == DrawMethod::CircleThreePoints
        && inputPoints_.size() == 1;
}

bool CadView::IsCircleThreePointsThirdPick() const
{
    return toolMode_ == ToolMode::DrawCircle
        && drawOptions_.method == DrawMethod::CircleThreePoints
        && inputPoints_.size() == 2;
}

bool CadView::IsCirclePolarSegmentPick() const
{
    return IsCircleTwoPointsPick()
        || IsCircleThreePointsSecondPick()
        || IsCircleThreePointsThirdPick();
}

void CadView::ClearLineChain()
{
    lineChainPoints_.clear();
    lineChainEntityIds_.clear();
}

bool CadView::UndoLineSegment()
{
    if (!IsContinuousLineDraw() || !commandManager_) {
        return false;
    }

    // No committed segment yet: undo the first point (AutoCAD: Undo at start).
    if (lineChainEntityIds_.empty() || lineChainPoints_.size() <= 1) {
        ResetTransientState();
        EmitStatus(QStringLiteral("指定第一点"));
        return true;
    }

    // Prefer undoing the last AddEntity from this LINE chain.
    bool undone = false;
    if (commandManager_->CanUndo()) {
        commandManager_->Undo();
        undone = true;
    }
    if (!undone && document_ && !lineChainEntityIds_.empty()) {
        // Fallback: remove the last chain entity directly if undo stack was empty.
        document_->RemoveEntity(lineChainEntityIds_.back());
        undone = true;
    }
    if (!undone) {
        EmitStatus(QStringLiteral("无法放弃上一段直线"));
        return true;
    }

    if (!lineChainEntityIds_.empty()) {
        lineChainEntityIds_.pop_back();
    }
    if (!lineChainPoints_.empty()) {
        lineChainPoints_.pop_back();
    }
    inputPoints_.clear();
    if (!lineChainPoints_.empty()) {
        inputPoints_.push_back(lineChainPoints_.back());
    }
    ClearPreview();
    if (inputPoints_.empty()) {
        EmitStatus(QStringLiteral("指定第一点"));
    } else {
        EmitStatus(QStringLiteral("指定下一点或 [放弃(U)/闭合(C)]"));
        if (viewport()) {
            UpdatePreview(ApplySnap(MapToModel(viewport()->mapFromGlobal(QCursor::pos()))));
        }
    }
    RefreshViewport();
    return true;
}

bool CadView::CloseLineChain()
{
    if (!IsContinuousLineDraw() || !commandManager_) {
        return false;
    }
    // Need at least two segments (3 vertices) before Close is meaningful, same as AutoCAD.
    if (lineChainPoints_.size() < 3 || lineChainEntityIds_.size() < 2) {
        EmitStatus(QStringLiteral("至少需要两段直线才能闭合"));
        return true;
    }

    const GeoPoint start = lineChainPoints_.front();
    const GeoPoint end = lineChainPoints_.back();
    if (Distance2D(start, end) <= Tolerance::Model) {
        EmitStatus(QStringLiteral("起点与终点重合，无需闭合"));
        FinishDrawCommand();
        return true;
    }

    auto closing = std::make_unique<LineEntity>(IdGenerator::Next(), GeoLine(end, start));
    const EntityId closingId = closing->Id();
    NoteArcContinuation(*closing);
    commandManager_->AddEntity(std::move(closing));
    lineChainEntityIds_.push_back(closingId);
    lineChainPoints_.push_back(start);
    FinishDrawCommand();
    EmitStatus(QStringLiteral("直线已闭合"));
    return true;
}

void CadView::InvalidateGridBackground()
{
    RefreshViewport();
}

void CadView::RefreshViewport()
{
    if (QGraphicsScene* currentScene = scene()) {
        const QRectF visible = mapToScene(viewport()->rect()).boundingRect();
        const double margin = ModelToleranceFromPixels(16.0);
        currentScene->invalidate(visible.adjusted(-margin, -margin, margin, margin), QGraphicsScene::AllLayers);
    }
    RepaintViewport();
}

void CadView::BeginMirrorWorkflow()
{
    ResetMirrorState();
    if (!document_) {
        return;
    }

    mirrorTargetIds_ = document_->SelectedEntityIds();
    if (mirrorTargetIds_.empty()) {
        mirrorPhase_ = MirrorPhase::SelectObjects;
        EmitStatus(QStringLiteral("选择要镜像的对象（Shift 减选，Enter/右键结束选择）"));
    } else {
        mirrorPhase_ = MirrorPhase::PickAxisFirst;
        EmitStatus(QStringLiteral("指定镜像线第一点："));
    }
}

void CadView::ResetMirrorState()
{
    mirrorPhase_ = MirrorPhase::Inactive;
    mirrorTargetIds_.clear();
    mirrorAxisStart_ = {};
    mirrorAxisEnd_ = {};
}

void CadView::FinishMirrorSelection()
{
    if (!document_) {
        return;
    }

    mirrorTargetIds_ = document_->SelectedEntityIds();
    if (mirrorTargetIds_.empty()) {
        EmitStatus(QStringLiteral("未选择对象，请继续选择要镜像的图元"));
        return;
    }

    mirrorPhase_ = MirrorPhase::PickAxisFirst;
    EmitStatus(QStringLiteral("指定镜像线第一点："));
}

void CadView::HandleMirrorPress(const GeoPoint& point, Qt::KeyboardModifiers modifiers)
{
    if (!document_) {
        return;
    }

    if (mirrorPhase_ == MirrorPhase::SelectObjects) {
        const EntityId hit = HitTest(point);
        if (hit == 0) {
            return;
        }
        if (modifiers.testFlag(Qt::ShiftModifier)) {
            document_->ToggleSelection(hit);
        } else if (!IsSelected(hit)) {
            document_->ToggleSelection(hit);
        }
        mirrorTargetIds_ = document_->SelectedEntityIds();
        return;
    }

    if (mirrorPhase_ == MirrorPhase::PickAxisFirst) {
        mirrorAxisStart_ = point;
        mirrorPhase_ = MirrorPhase::PickAxisSecond;
        EmitStatus(QStringLiteral("指定镜像线第二点："));
        return;
    }

    if (mirrorPhase_ == MirrorPhase::PickAxisSecond) {
        mirrorAxisEnd_ = point;
        mirrorPhase_ = MirrorPhase::ConfirmEraseSource;
        ClearPreview();
        EmitStatus(QStringLiteral("是否删除源对象？[是(Y)/否(N)]，默认保留(N)"));
    }
}

void CadView::HandleMirrorRightClick()
{
    if (mirrorPhase_ == MirrorPhase::SelectObjects) {
        FinishMirrorSelection();
        return;
    }
    if (mirrorPhase_ == MirrorPhase::PickAxisSecond) {
        mirrorPhase_ = MirrorPhase::PickAxisFirst;
        ClearPreview();
        EmitStatus(QStringLiteral("指定镜像线第一点："));
        return;
    }
    if (mirrorPhase_ == MirrorPhase::ConfirmEraseSource) {
        mirrorPhase_ = MirrorPhase::PickAxisSecond;
        EmitStatus(QStringLiteral("指定镜像线第二点："));
        return;
    }
    BeginMirrorWorkflow();
}

void CadView::ConfirmMirror(bool eraseSource)
{
    if (!commandManager_ || mirrorTargetIds_.empty() || mirrorPhase_ != MirrorPhase::ConfirmEraseSource) {
        return;
    }

    const std::vector<EntityId> resultIds = commandManager_->MirrorSelected(
        mirrorTargetIds_,
        mirrorAxisStart_,
        mirrorAxisEnd_,
        eraseSource);
    if (resultIds.empty()) {
        EmitStatus(QStringLiteral("镜像失败"));
        return;
    }

    SelectEntities(resultIds);
    mirrorTargetIds_ = resultIds;
    mirrorPhase_ = MirrorPhase::PickAxisFirst;
    ClearPreview();
    EmitStatus(QStringLiteral("镜像完成。继续指定镜像线第一点，或按 Esc 退出"));
}

void CadView::SelectEntities(const std::vector<EntityId>& ids)
{
    if (!document_ || ids.empty()) {
        return;
    }

    document_->ClearSelection();
    for (EntityId id : ids) {
        if (Entity* entity = document_->FindEntity(id)) {
            entity->SetSelected(true);
        }
    }
    document_->SyncSelectionView();
}

void CadView::CopySelectedTextEntities()
{
    if (!document_) {
        return;
    }

    EndTextPasteSession(false);

    for (EntityId id : document_->SelectedEntityIds()) {
        const Entity* entity = document_->FindEntity(id);
        if (!entity) {
            continue;
        }
        if (entity->Type() != EntityType::Text && entity->Type() != EntityType::MText) {
            continue;
        }
        textClipboard_.push_back(entity->Clone());
    }

    if (textClipboard_.empty()) {
        EmitStatus(QStringLiteral("请先选中 TEXT 或 MTEXT 对象"));
        return;
    }

    textClipboardBase_ = CentroidOfTextEntities(textClipboard_);
    EmitStatus(QStringLiteral("已复制 %1 个文字对象").arg(textClipboard_.size()));
}

void CadView::BeginPasteText()
{
    if (textClipboard_.empty()) {
        EmitStatus(QStringLiteral("剪贴板为空，请先选中文字后 Ctrl+C 复制"));
        return;
    }

    if (toolMode_ != ToolMode::Select) {
        SetToolMode(ToolMode::Select);
    }

    CancelDrag();
    pasteTextPending_ = true;

    QPoint viewPos = viewport()->mapFromGlobal(QCursor::pos());
    if (!viewport()->rect().contains(viewPos)) {
        viewPos = viewport()->rect().center();
    }
    const GeoPoint modelPoint = ApplyOrtho(ApplySnap(MapToModel(viewPos)));
    ShowPastePointInput(viewPos, modelPoint);
    EmitStatus(QStringLiteral("粘贴：指定插入点（Tab X→Y→确认，Shift+Tab Y→X，Enter 确认，Esc 取消）"));
}

void CadView::FinishPasteTextAt(const GeoPoint& point)
{
    if (!document_ || !commandManager_ || textClipboard_.empty()) {
        CancelPasteText();
        return;
    }

    const double dx = point.X() - textClipboardBase_.X();
    const double dy = point.Y() - textClipboardBase_.Y();
    std::vector<std::unique_ptr<Entity>> pasted;
    std::vector<EntityId> newIds;
    pasted.reserve(textClipboard_.size());
    newIds.reserve(textClipboard_.size());

    for (const auto& prototype : textClipboard_) {
        std::unique_ptr<Entity> entity = prototype->Clone();
        entity->SetId(IdGenerator::Next());
        entity->SetSelected(false);
        entity->MoveBy(dx, dy);
        newIds.push_back(entity->Id());
        pasted.push_back(std::move(entity));
    }

    commandManager_->AddEntities(std::move(pasted));
    SelectEntities(newIds);
    pasteTextPending_ = false;
    HidePointInput();
    ClearPreview();
    if (cadScene_) {
        cadScene_->RebuildGrips();
        cadScene_->InvalidateItemRegions();
    }
    EmitStatus(QStringLiteral("已粘贴 %1 个文字对象").arg(newIds.size()));
}

void CadView::EndTextPasteSession(bool notify)
{
    const bool hadSession = pasteTextPending_ || !textClipboard_.empty();
    pasteTextPending_ = false;
    textClipboard_.clear();
    textClipboardBase_ = GeoPoint();
    HidePointInput();
    ClearPreview();
    if (notify && hadSession) {
        EmitStatus(QStringLiteral("文字粘贴命令已结束"));
    }
}

void CadView::CancelPasteText()
{
    EndTextPasteSession(true);
}

void CadView::UpdatePasteTextPreview(const GeoPoint& point)
{
    if (!scene() || textClipboard_.empty()) {
        ClearPreview();
        return;
    }

    const double dx = point.X() - textClipboardBase_.X();
    const double dy = point.Y() - textClipboardBase_.Y();
    QPainterPath path;
    TextLayoutOptions options;
    options.document = document_;

    for (const auto& prototype : textClipboard_) {
        std::unique_ptr<Entity> preview = prototype->Clone();
        preview->MoveBy(dx, dy);
        options.style = &preview->Style();
        const QRectF bounds = TextLikeEntityBounds(*preview, options);
        if (bounds.isValid()) {
            path.addRect(bounds);
        }
    }

    if (path.isEmpty()) {
        path.moveTo(ToScenePoint(point));
        path.addEllipse(ToScenePoint(point), 4.0, 4.0);
    }

    SetPreviewPath(path);
}

void CadView::UpdateTextHover(const GeoPoint& point)
{
    const EntityId hit = HitTestTextAtPoint(point);
    if (hit == hoveredTextEntityId_) {
        return;
    }

    if (cadScene_) {
        cadScene_->SetHoveredEntity(hit);
    }
    hoveredTextEntityId_ = hit;
}

void CadView::ClearTextHover()
{
    if (hoveredTextEntityId_ == 0) {
        return;
    }

    if (cadScene_) {
        cadScene_->SetHoveredEntity(0);
    }
    hoveredTextEntityId_ = 0;
}

bool CadView::ShouldShowDynPointInput() const
{
    if (pasteTextPending_) {
        return true;
    }
    if (boxSelecting_ || draggingSelection_ || draggingGrip_ || splineGripEditing_ || panning_) {
        return false;
    }
    if (!inputPoints_.empty()) {
        return false;
    }
    if (toolMode_ == ToolMode::DrawArc
        && drawOptions_.method == DrawMethod::ArcContinue
        && lastArcContinue_.valid) {
        return false;
    }

    switch (toolMode_) {
    case ToolMode::DrawPoint:
    case ToolMode::DrawLine:
    case ToolMode::DrawRay:
    case ToolMode::DrawXLine:
    case ToolMode::DrawRectangle:
    case ToolMode::DrawPolygon:
    case ToolMode::DrawCircle:
    case ToolMode::DrawArc:
    case ToolMode::DrawEllipse:
    case ToolMode::DrawParabola:
    case ToolMode::DrawHyperbola:
    case ToolMode::DrawSpline:
    case ToolMode::DrawPolyline:
    case ToolMode::DrawText:
    case ToolMode::DrawMText:
    case ToolMode::DrawDimLinear:
    case ToolMode::DrawDimAligned:
    case ToolMode::DrawDimOrdinate:
    case ToolMode::InsertBlock:
    case ToolMode::ArrayPolar:
        return arrayPhase_ == ArrayCommandPhase::Center
            || arrayPhase_ == ArrayCommandPhase::BasePoint;
    case ToolMode::DrawDimAngular:
        return dimAngularThreePointMode_;
    case ToolMode::Move:
    case ToolMode::Rotate:
    case ToolMode::Scale:
        return document_ && !document_->SelectedEntityIds().empty();
    case ToolMode::Mirror:
        return mirrorPhase_ == MirrorPhase::PickAxisFirst;
    default:
        return false;
    }
}

QString CadView::DynPointPrompt() const
{
    if (pasteTextPending_) {
        return QStringLiteral("指定插入点");
    }
    return DynPointPromptFor(toolMode_, drawOptions_.method);
}

void CadView::EnsurePointInput()
{
    if (pointInput_) {
        return;
    }

    pointInput_ = new DynamicPointInputWidget(viewport());
    pointInput_->hide();

    pointInput_->onValueChanged = [this]() {
        if (!pointInput_) {
            return;
        }
        if (pasteTextPending_) {
            UpdatePasteTextPreview(pointInput_->ModelPoint());
        }
    };
    pointInput_->onConfirmed = [this]() {
        CommitDynPointInput();
    };
    pointInput_->onCancelRequested = [this]() {
        if (pasteTextPending_) {
            EndTextPasteSession(true);
        } else if (IsDrawTool(toolMode_) || toolMode_ == ToolMode::DrawText
            || toolMode_ == ToolMode::DrawMText
            || toolMode_ == ToolMode::InsertBlock
            || toolMode_ == ToolMode::DrawDimLinear
            || toolMode_ == ToolMode::DrawDimAligned
            || toolMode_ == ToolMode::DrawDimOrdinate
            || toolMode_ == ToolMode::DrawDimAngular) {
            SetToolMode(ToolMode::Select);
            EmitStatus(QStringLiteral("已取消"));
        } else {
            HidePointInput();
            ResetTransientState();
            EmitStatus(QStringLiteral("已取消"));
        }
        setFocus(Qt::OtherFocusReason);
        RefreshViewport();
    };
}

void CadView::CommitDynPointInput()
{
    if (!pointInput_) {
        return;
    }
    const GeoPoint point = pointInput_->ModelPoint();
    if (pasteTextPending_) {
        FinishPasteTextAt(point);
        return;
    }
    HandleLeftPress(point, Qt::NoModifier);
    if (SupportsPolarTracking() && polarInput_) {
        polarInput_->ResetLocks();
    }
    if (!ShouldShowDynPointInput()) {
        HidePointInput();
    } else {
        pointInput_->ResetCoordinateInputLock();
        pointInput_->SetPrompt(DynPointPrompt());
        pointInput_->FocusX();
    }
    setFocus(Qt::OtherFocusReason);
    RefreshViewport();
}

void CadView::ShowPastePointInput(const QPoint& viewPos, const GeoPoint& modelPoint)
{
    EnsurePointInput();
    pointInput_->SetPrompt(QStringLiteral("指定插入点"));
    pointInput_->ResetCoordinateInputLock();
    pointInput_->SetModelPoint(modelPoint);
    pointInput_->MoveToViewPosition(viewPos);
    pointInput_->show();
    pointInput_->raise();
    pointInput_->FocusX();
    UpdatePasteTextPreview(modelPoint);
}

void CadView::HidePointInput()
{
    if (pointInput_) {
        pointInput_->hide();
        pointInput_->ResetCoordinateInputLock();
        pointInput_->SetPromptVisible(true);
        pointInput_->SetFieldsClickable(true);
    }
}

void CadView::UpdatePointInput(const QPoint& viewPos, const GeoPoint& modelPoint)
{
    if (!ShouldShowDynPointInput()) {
        HidePointInput();
        return;
    }

    EnsurePointInput();
    if (IsSplineDraw()) {
        pointInput_->SetPromptVisible(false);
        UpdateSplineOptionInput(viewPos);
        if (optionInput_ && optionInput_->isVisible()) {
            pointInput_->MoveToAbsolute(QPoint(
                optionInput_->x() + optionInput_->width() + 3,
                optionInput_->y()));
        } else {
            pointInput_->MoveToViewPosition(viewPos);
        }
    } else {
        pointInput_->SetPromptVisible(true);
        pointInput_->SetPrompt(DynPointPrompt());
        pointInput_->MoveToViewPosition(viewPos);
    }
    if (!pointInput_->IsCoordinateInputLocked()) {
        pointInput_->SetModelPoint(modelPoint);
    }
    if (!pointInput_->isVisible()) {
        pointInput_->show();
        pointInput_->raise();
        pointInput_->FocusX();
    } else {
        pointInput_->raise();
    }
    if (optionInput_ && optionInput_->isVisible()) {
        optionInput_->raise();
    }
}

void CadView::EnsurePolarInput()
{
    if (polarInput_) {
        return;
    }
    polarInput_ = new DynamicPolarInputWidget(viewport());
    polarInput_->hide();
    polarInput_->onValueChanged = [this]() {
        if (IsPolylineOptionEditing()) {
            return;
        }
        GeoPoint polarBase;
        if (!TryGetPolarBasePoint(&polarBase) || !viewport()) {
            return;
        }
        const GeoPoint cursor = ApplySnap(MapToModel(lastCursorViewportPos_));
        const GeoPoint end = ResolveDynamicLineEndpoint(polarBase, cursor);
        UpdatePreview(end);
        RefreshViewport();
    };
    polarInput_->onConfirmed = [this]() {
        if (IsPolylineOptionEditing()) {
            optionInput_->ConfirmInput();
            return;
        }
        CommitDynamicLinePoint();
    };
    polarInput_->onCancelRequested = [this]() {
        if (IsPolylineOptionEditing()) {
            optionInput_->CancelKeywordEdit();
            return;
        }
        if (IsDrawInProgress()) {
            ResetTransientState();
            EmitStatus(QStringLiteral("已取消当前绘制"));
        }
        setFocus(Qt::OtherFocusReason);
        RefreshViewport();
    };
    polarInput_->onInterceptKey = [this](QKeyEvent* keyEvent) {
        if (!IsCommandOptionHudActive() || keyEvent->modifiers() != Qt::NoModifier) {
            return false;
        }
        if (IsPolylineOptionEditing()) {
            if (keyEvent->key() == Qt::Key_Down || keyEvent->key() == Qt::Key_Up) {
                optionInput_->ShowOptionMenu();
                return true;
            }
            if (keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter) {
                optionInput_->ConfirmInput();
                return true;
            }
            if (keyEvent->key() == Qt::Key_Escape) {
                optionInput_->CancelKeywordEdit();
                return true;
            }
            const QString text = keyEvent->text();
            if (!text.isEmpty()) {
                BeginPolylineKeywordTyping(text);
            }
            return true;
        }
        if (keyEvent->key() == Qt::Key_Down || keyEvent->key() == Qt::Key_Up) {
            EnsureOptionInput();
            if (IsSplineDraw()) {
                UpdateSplineOptionInput(lastCursorViewportPos_);
            } else {
                UpdatePolylineOptionInput(lastCursorViewportPos_);
            }
            optionInput_->ShowOptionMenu();
            return true;
        }
        const QString text = keyEvent->text();
        if (!text.isEmpty() && text.at(0).isLetter()) {
            BeginPolylineKeywordTyping(text);
            return true;
        }
        return false;
    };
}

void CadView::EnsureDynPromptLabel()
{
    if (dynPromptLabel_) {
        return;
    }
    dynPromptLabel_ = new QLabel(viewport());
    dynPromptLabel_->setStyleSheet(
        QStringLiteral(
            "QLabel {"
            "  background-color: #a8a8a8;"
            "  color: #000000;"
            "  border: 1px solid #000000;"
            "  padding: 2px 8px;"
            "  font-size: 11px;"
            "}"));
    dynPromptLabel_->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    dynPromptLabel_->hide();
}

void CadView::ShowDynPrompt(const QString& text, const QPoint& viewPos)
{
    EnsureDynPromptLabel();
    dynPromptLabel_->setText(text);
    dynPromptLabel_->adjustSize();
    int x = viewPos.x() + 18;
    int y = viewPos.y() + 28;
    if (QWidget* p = viewport()) {
        x = std::clamp(x, 0, std::max(0, p->width() - dynPromptLabel_->width()));
        y = std::clamp(y, 0, std::max(0, p->height() - dynPromptLabel_->height()));
    }
    dynPromptLabel_->move(x, y);
    dynPromptLabel_->show();
    dynPromptLabel_->raise();
}

void CadView::HideDynPrompt()
{
    if (dynPromptLabel_) {
        dynPromptLabel_->hide();
    }
}

void CadView::HidePolarInput()
{
    if (polarInput_) {
        polarInput_->hide();
        polarInput_->SetLengthVisible(true);
        polarInput_->SetAngleVisible(true);
        polarInput_->ResetLocks();
    }
    HideDynPrompt();
    if (polarInput_) {
        polarInput_->SetTabHintVisible(true);
        polarInput_->SetPolarAnglePrefix(false);
    }
    if (!IsPolylineOptionEditing()
        && !IsSplineGripEditing()
        && !IsFilletChamferCommand()
        && !(IsSplineDraw() && ShouldShowDynPointInput())) {
        HideOptionInput();
    }
}

void CadView::UpdatePolarInput(const QPoint& viewPos, const GeoPoint& cursorPoint)
{
    if (IsPolylineOptionEditing()) {
        if (optionInput_ && optionInput_->isVisible()) {
            optionInput_->raise();
        }
        return;
    }
    GeoPoint base;
    if (!TryGetPolarBasePoint(&base)) {
        HidePolarInput();
        return;
    }

    EnsurePolarInput();
    if (IsArcStartEndAnglePick() || IsArcIncludedAnglePick() || IsArcStartEndRadiusPick()
        || IsArcChordLengthPick()
        || IsArcStartCenterEndEndPick()
        || (IsContinuousPolylineDraw() && drawOptions_.polylineArcMode)) {
        drawOptions_.clockwise =
            (QGuiApplication::keyboardModifiers() & Qt::ControlModifier) != 0;
    }
    const GeoPoint end = ResolveDynamicLineEndpoint(base, cursorPoint);
    const double dx = end.X() - base.X();
    const double dy = end.Y() - base.Y();
    const double length = std::hypot(dx, dy);
    const double angleRad = (length > Tolerance::Model) ? std::atan2(dy, dx) : 0.0;
    // Default (LINE / polyline): AutoCAD DYN side-relative angle [0, 180].
    double angleDeg = DynInputDisplayAngleDegrees(angleRad);

    // 圆心-起点-端点 第二点：侧向角 [0,180]。
    // 圆心-起点-长度 第二点：AutoCAD 显示绝对极轴角 [0,360)，90° 即正上方。
    // 连续：端点相对衔接起点的绝对极轴角 [0,360)。
    if (IsArcCenterStartEndStartPick()) {
        if (drawOptions_.method == DrawMethod::ArcCenterStartChord) {
            angleDeg = DrawAlgorithms::ToPositiveAngleDegrees(angleRad * kRadToDeg);
        } else {
            angleDeg = DynInputDisplayAngleDegrees(angleRad);
        }
    }
    if (IsArcContinuePick()) {
        angleDeg = DrawAlgorithms::ToPositiveAngleDegrees(angleRad * kRadToDeg);
    }
    if (IsSplineDraw()) {
        angleDeg = DrawAlgorithms::ToPositiveAngleDegrees(angleRad * kRadToDeg);
    }

    // 圆心-起点-端点 第三点：DYN 显示侧向绝对角 [0,180]。
    if (IsArcCenterStartEndEndPick() && inputPoints_.size() >= 2) {
        angleDeg = DynInputDisplayAngleDegrees(angleRad);
    }

    // 起点-圆心-端点第三点：光标相对水平轴（+X）的极角 [0,180]。
    if (IsArcStartCenterEndEndPick() && inputPoints_.size() >= 2) {
        angleDeg = DynInputDisplayAngleDegrees(angleRad);
    }

    // 起点-圆心-角度 / 圆心-起点-角度：DYN 显示包含角幅值。
    if (IsArcIncludedAnglePick() && inputPoints_.size() >= 2) {
        const GeoPoint& start = ArcIncludedAngleStartPoint();
        const double startAng = std::atan2(start.Y() - base.Y(), start.X() - base.X());
        double delta = angleRad - startAng;
        constexpr double kTwoPiLocal = 6.28318530717958647692;
        if (drawOptions_.clockwise) {
            while (delta > 0.0) {
                delta -= kTwoPiLocal;
            }
            while (delta <= -kTwoPiLocal) {
                delta += kTwoPiLocal;
            }
            angleDeg = std::abs(delta) * kRadToDeg;
        } else {
            angleDeg = IncludedAngleSweepRadians(startAng, angleRad) * kRadToDeg;
        }
    }

    // 起点-端点-角度：DYN 显示夹角幅值 θ。
    // 实线：光标在起点上方 → θ 劣弧；下方 → 360°-θ 优弧；Ctrl 再翻转。
    if (IsArcStartEndAnglePick() && inputPoints_.size() >= 2) {
        if (length > Tolerance::Model) {
            angleDeg = DynInputDisplayAngleDegrees(angleRad);
        } else {
            angleDeg = 0.0;
        }
    }

    // 起点-端点-方向：
    // - 第 2 点端点框：绝对极轴角 [0,360)；
    // - 第 3 点方向框：侧向角 [0,180]（向下 270° 显示为 90°）。
    // 第 3 点旁“极轴: 距离 < 角度”仍显示绝对角（如 270°）。
    if (toolMode_ == ToolMode::DrawArc
        && drawOptions_.method == DrawMethod::ArcStartEndDirection
        && inputPoints_.size() == 1) {
        angleDeg = DrawAlgorithms::ToPositiveAngleDegrees(angleRad * kRadToDeg);
    }

    // AutoCAD-like DYN: always anchor from the free end in screen pixels along
    // the rubber-band / angle arc. Model mid-points go off-screen when zoomed in;
    // a fixed pixel step-back stays on the visible tip and clear of the crosshair.
    const QPoint endView = mapFromScene(ToScenePoint(end));
    const QPoint baseView = mapFromScene(ToScenePoint(base));
    const QRect viewBounds = viewport() ? viewport()->rect() : QRect(0, 0, width(), height());
    const QRect safeView = viewBounds.adjusted(8, 8, -8, -8);

    constexpr double kBackFromEndPx = 100.0;
    constexpr double kFramePx = 56.0;
    constexpr double kHalfBoxPx = 35.0;
    constexpr double kGapPx = 6.0;

    QPoint midView = viewPos;
    QPoint angleView = viewPos + QPoint(56, 28);

    // AutoCAD 起点-圆心-端点第三点：角度框落在 0°→光标方向的极角弧上（半径=光标距）。
    if (IsArcStartCenterEndEndPick() && length > Tolerance::Model) {
        const double sweep = NormalizeAngleSigned(angleRad);
        const double midAng = sweep * 0.55;
        angleView = mapFromScene(ToScenePoint(GeoPoint(
            base.X() + std::cos(midAng) * length,
            base.Y() + std::sin(midAng) * length)));
        angleView.setX(std::clamp(angleView.x(), safeView.left(), safeView.right()));
        angleView.setY(std::clamp(angleView.y(), safeView.top(), safeView.bottom()));
        midView = angleView;
    } else if (IsArcIncludedAnglePick() && inputPoints_.size() >= 1 && length > Tolerance::Model) {
        const GeoPoint& start = ArcIncludedAngleStartPoint();
        const double startAng = std::atan2(start.Y() - base.Y(), start.X() - base.X());
        const double radius = Distance2D(base, start);
        if (radius > Tolerance::Model) {
            double delta = angleRad - startAng;
            constexpr double kTwoPiLocal = 6.28318530717958647692;
            if (drawOptions_.clockwise) {
                while (delta > 0.0) {
                    delta -= kTwoPiLocal;
                }
                while (delta <= -kTwoPiLocal) {
                    delta += kTwoPiLocal;
                }
            } else {
                while (delta <= 0.0) {
                    delta += kTwoPiLocal;
                }
                while (delta > kTwoPiLocal) {
                    delta -= kTwoPiLocal;
                }
                if (delta >= kTwoPiLocal - 1.0e-9) {
                    delta = 0.0;
                }
            }
            if (std::abs(delta) > 1.0 * kDegToRad) {
                const double frameR = std::max(length, radius);
                const double midAng = startAng + delta * 0.55;
                angleView = mapFromScene(ToScenePoint(GeoPoint(
                    base.X() + std::cos(midAng) * frameR,
                    base.Y() + std::sin(midAng) * frameR)));
                angleView.setX(std::clamp(angleView.x(), safeView.left(), safeView.right()));
                angleView.setY(std::clamp(angleView.y(), safeView.top(), safeView.bottom()));
                midView = angleView;
            }
        }
    } else if (IsArcCenterStartEndEndPick() && inputPoints_.size() >= 2) {
        // AutoCAD CSE 图3：角度框落在 0°→当前方向的轨迹弧上（显示 0..180）。
        const GeoPoint& start = inputPoints_[1];
        const double radius = Distance2D(base, start);
        const double frameR = radius > Tolerance::Model ? radius : length;
        if (frameR > Tolerance::Model) {
            const double sweep = NormalizeAngleSigned(angleRad);
            const double midAng = sweep * 0.55;
            angleView = mapFromScene(ToScenePoint(GeoPoint(
                base.X() + std::cos(midAng) * frameR,
                base.Y() + std::sin(midAng) * frameR)));
            angleView.setX(std::clamp(angleView.x(), safeView.left(), safeView.right()));
            angleView.setY(std::clamp(angleView.y(), safeView.top(), safeView.bottom()));
            midView = angleView;
        }
    } else if ((IsArcCenterStartEndStartPick() || IsArcContinuePick()) && length > Tolerance::Model) {
        // 图2 / 连续：角度框落在 0°→当前方向的侧向角轨迹上。
        const double sweep = NormalizeAngleSigned(angleRad);
        const double midAng = sweep * 0.55;
        angleView = mapFromScene(ToScenePoint(GeoPoint(
            base.X() + std::cos(midAng) * length,
            base.Y() + std::sin(midAng) * length)));
        angleView.setX(std::clamp(angleView.x(), safeView.left(), safeView.right()));
        angleView.setY(std::clamp(angleView.y(), safeView.top(), safeView.bottom()));

        // 长度框仍落在半径橡胶筋旁（黄线侧）。
        const QPoint endViewLocal = endView;
        const QPoint baseViewLocal = baseView;
        QPointF along(
            static_cast<double>(baseViewLocal.x() - endViewLocal.x()),
            static_cast<double>(baseViewLocal.y() - endViewLocal.y()));
        const double alongLen = std::hypot(along.x(), along.y());
        if (alongLen > 1.0) {
            along /= alongLen;
        }
        const double back = std::min(kBackFromEndPx, std::max(40.0, alongLen * 0.45));
        midView = endViewLocal;
        if (alongLen > 1.0) {
            midView = endViewLocal + QPoint(
                static_cast<int>(std::lround(along.x() * back)),
                static_cast<int>(std::lround(along.y() * back)));
        }
        const double ux = dx / length;
        const double uy = dy / length;
        double px = -uy;
        double py = ux;
        if (px > 1e-12 || (std::abs(px) <= 1e-12 && py > 0.0)) {
            px = -px;
            py = -py;
        }
        const double frameW = ModelToleranceFromPixels(kFramePx);
        const GeoPoint nearEnd(
            end.X() - ux * std::min(ModelToleranceFromPixels(kBackFromEndPx), length * 0.45),
            end.Y() - uy * std::min(ModelToleranceFromPixels(kBackFromEndPx), length * 0.45));
        const GeoPoint nearDashed(nearEnd.X() + px * frameW, nearEnd.Y() + py * frameW);
        QPointF out(
            mapFromScene(ToScenePoint(nearDashed)).x() - mapFromScene(ToScenePoint(nearEnd)).x(),
            mapFromScene(ToScenePoint(nearDashed)).y() - mapFromScene(ToScenePoint(nearEnd)).y());
        const double outLen = std::hypot(out.x(), out.y());
        if (outLen > 1.0) {
            out /= outLen;
            midView += QPoint(
                static_cast<int>(std::lround(out.x() * (kHalfBoxPx + kGapPx))),
                static_cast<int>(std::lround(out.y() * (kHalfBoxPx + kGapPx))));
        }
        midView.setX(std::clamp(midView.x(), safeView.left(), safeView.right()));
        midView.setY(std::clamp(midView.y(), safeView.top(), safeView.bottom()));
    } else if (IsArcStartEndAnglePick() && inputPoints_.size() >= 2 && length > Tolerance::Model) {
        // 角度框落在以起点为圆心的有向夹角轨迹上（跟随光标侧，不受 Ctrl 影响）。
        const double sweep = angleRad;
        const double midAng = sweep * 0.55;
        angleView = mapFromScene(ToScenePoint(GeoPoint(
            base.X() + std::cos(midAng) * length,
            base.Y() + std::sin(midAng) * length)));
        angleView.setX(std::clamp(angleView.x(), safeView.left(), safeView.right()));
        angleView.setY(std::clamp(angleView.y(), safeView.top(), safeView.bottom()));
        midView = angleView;
    } else if (IsArcStartEndDirectionPick() && length > Tolerance::Model) {
        // 角度框落在起点方向角轨迹上（与 AutoCAD SED 第三点一致）。
        const double sweep = angleRad;
        const double midAng = sweep * 0.55;
        angleView = mapFromScene(ToScenePoint(GeoPoint(
            base.X() + std::cos(midAng) * length,
            base.Y() + std::sin(midAng) * length)));
        angleView.setX(std::clamp(angleView.x(), safeView.left(), safeView.right()));
        angleView.setY(std::clamp(angleView.y(), safeView.top(), safeView.bottom()));
        midView = angleView;
    } else if (IsCircleCenterPolarPick() && length > Tolerance::Model) {
        // AutoCAD 圆心-半径/直径：数值框落在圆心→光标黄虚线上（自光标沿半径/直径回退）。
        QPointF along(
            static_cast<double>(baseView.x() - endView.x()),
            static_cast<double>(baseView.y() - endView.y()));
        const double alongLen = std::hypot(along.x(), along.y());
        if (alongLen > 1.0) {
            along /= alongLen;
        }
        const double back = std::min(kBackFromEndPx, std::max(40.0, alongLen * 0.45));
        midView = endView;
        if (alongLen > 1.0) {
            midView = endView + QPoint(
                static_cast<int>(std::lround(along.x() * back)),
                static_cast<int>(std::lround(along.y() * back)));
        }
        midView.setX(std::clamp(midView.x(), safeView.left(), safeView.right()));
        midView.setY(std::clamp(midView.y(), safeView.top(), safeView.bottom()));
        angleView = midView;
    } else if (length > Tolerance::Model) {
        const double ux = dx / length;
        const double uy = dy / length;
        double px = -uy;
        double py = ux;
        if (px > 1e-12 || (std::abs(px) <= 1e-12 && py > 0.0)) {
            px = -px;
            py = -py;
        }

        QPointF along(baseView.x() - endView.x(), baseView.y() - endView.y());
        const double alongLen = std::hypot(along.x(), along.y());
        if (alongLen > 1.0) {
            along /= alongLen;
        }

        // Corridor side from a local sample near the free end (zoom-stable).
        const double localBackModel = ModelToleranceFromPixels(kBackFromEndPx);
        const GeoPoint nearEnd(
            end.X() - ux * std::min(localBackModel, length * 0.45),
            end.Y() - uy * std::min(localBackModel, length * 0.45));
        const double frameW = ModelToleranceFromPixels(kFramePx);
        const GeoPoint nearDashed(nearEnd.X() + px * frameW, nearEnd.Y() + py * frameW);
        QPointF out(
            mapFromScene(ToScenePoint(nearDashed)).x() - mapFromScene(ToScenePoint(nearEnd)).x(),
            mapFromScene(ToScenePoint(nearDashed)).y() - mapFromScene(ToScenePoint(nearEnd)).y());
        const double outLen = std::hypot(out.x(), out.y());
        if (outLen > 1.0) {
            out /= outLen;
        }

        // Length: back along solid from free end, then into corridor (6px from solid).
        const double back = std::min(kBackFromEndPx, std::max(40.0, alongLen * 0.45));
        QPoint solidAnchor = endView;
        if (alongLen > 1.0) {
            solidAnchor = endView + QPoint(
                static_cast<int>(std::lround(along.x() * back)),
                static_cast<int>(std::lround(along.y() * back)));
        }
        midView = solidAnchor;
        if (outLen > 1.0) {
            const double push = kHalfBoxPx + kGapPx;
            midView += QPoint(
                static_cast<int>(std::lround(out.x() * push)),
                static_cast<int>(std::lround(out.y() * push)));
        }

        // Angle: on the arc, ~kBackFromEndPx screen distance from free end.
        const double sweep = NormalizeAngleSigned(angleRad);
        if (std::abs(sweep) > 1.0 * 3.14159265358979323846 / 180.0) {
            const double arcRadius = length;
            QPoint best = endView;
            double bestDelta = 1.0e100;
            bool found = false;
            for (int i = 0; i <= 64; ++i) {
                const double t = static_cast<double>(i) / 64.0;
                if (t > 0.92) {
                    continue;
                }
                const double a = sweep * t;
                const QPoint p = mapFromScene(ToScenePoint(GeoPoint(
                    base.X() + std::cos(a) * arcRadius,
                    base.Y() + std::sin(a) * arcRadius)));
                if (!safeView.contains(p)) {
                    continue;
                }
                const double d = std::hypot(
                    static_cast<double>(p.x() - endView.x()),
                    static_cast<double>(p.y() - endView.y()));
                const double delta = std::abs(d - kBackFromEndPx);
                if (delta < bestDelta) {
                    bestDelta = delta;
                    best = p;
                    found = true;
                }
            }
            if (found) {
                angleView = best;
            } else if (alongLen > 1.0) {
                QPointF side = (outLen > 1.0) ? out : QPointF(along.y(), -along.x());
                angleView = endView + QPoint(
                    static_cast<int>(std::lround(-side.x() * 72.0 + along.x() * 36.0)),
                    static_cast<int>(std::lround(-side.y() * 72.0 + along.y() * 36.0)));
            }
        }
    }

    const bool angleOnlyDyn = IsPolarAngleHintOnlyGuide()
        || IsArcStartEndAnglePick()
        || IsArcStartEndDirectionPick()
        || IsArcCenterStartEndEndPick()
        || IsArcStartCenterEndEndPick()
        || IsArcIncludedAnglePick();
    const bool lengthOnlyDyn = IsArcChordLengthPick() || IsArcStartEndRadiusPick()
        || IsCircleCenterPolarPick();
    polarInput_->SetLengthVisible(!angleOnlyDyn);
    polarInput_->SetAngleVisible(!lengthOnlyDyn);
    if (IsSplineDraw()) {
        polarInput_->SetTabHintVisible(false);
        polarInput_->SetPolarAnglePrefix(true);
        HideDynPrompt();
        UpdateSplineOptionInput(viewPos);
        polarInput_->SetLengthAngle(length, angleDeg);
        if (optionInput_ && optionInput_->isVisible()) {
            polarInput_->MoveToHorizontalRow(QPoint(
                optionInput_->x() + optionInput_->width() + 3,
                optionInput_->y()));
        } else {
            polarInput_->MoveToHorizontalRow(viewPos + QPoint(18, 28));
        }
    } else {
        polarInput_->SetTabHintVisible(true);
        polarInput_->SetPolarAnglePrefix(false);
        polarInput_->MoveToLinePositions(midView, angleView);
        polarInput_->SetLengthAngle(length, angleDeg);
    }
    if (IsSplineDraw()) {
        // Prompt is already in the option HUD next to the cursor.
    } else if (IsArcCenterStartEndStartPick()) {
        // 图2 AutoCAD：提示锚在光标旁。
        ShowDynPrompt(QStringLiteral("指定圆弧的起点:"), viewPos);
    } else if (IsCircleCenterRadiusPick()) {
        ShowDynPrompt(QStringLiteral("指定圆的半径或"), viewPos);
    } else if (IsCircleCenterDiameterPick()) {
        ShowDynPrompt(QStringLiteral("指定圆的直径"), viewPos);
    } else if (IsCircleTanTanRadiusValuePick()) {
        ShowDynPrompt(QStringLiteral("指定圆的半径:"), viewPos);
    } else if (IsCircleTwoPointsPick()) {
        ShowDynPrompt(QStringLiteral("指定圆直径的第二个端点:"), viewPos);
    } else if (IsCircleThreePointsSecondPick() || IsCircleThreePointsThirdPick()) {
        bool snapped = false;
        const double tracked = ApplyPolarTracking(angleRad, &snapped);
        const double polarDeg =
            DrawAlgorithms::ToPositiveAngleDegrees(tracked * kRadToDeg);
        bool orthoSnap = false;
        double shownDeg = polarDeg;
        if (snapped) {
            const double candidates[] = {0.0, 90.0, 180.0, 270.0};
            for (double c : candidates) {
                if (std::abs(polarDeg - c) <= 0.51) {
                    orthoSnap = true;
                    shownDeg = c;
                    break;
                }
            }
        }
        if (orthoSnap) {
            ShowDynPrompt(
                QStringLiteral("极轴: %1 < %2°")
                    .arg(length, 0, 'f', 4)
                    .arg(shownDeg, 0, 'f', 0),
                viewPos);
        } else if (IsCircleThreePointsThirdPick()) {
            ShowDynPrompt(QStringLiteral("指定圆上的第三个点:"), viewPos);
        } else {
            ShowDynPrompt(QStringLiteral("指定圆上的第二个点:"), viewPos);
        }
    } else if (IsArcContinuePick()) {
        bool snapped = false;
        const double tracked = ApplyPolarTracking(angleRad, &snapped);
        const double polarDeg =
            DrawAlgorithms::ToPositiveAngleDegrees(tracked * kRadToDeg);
        bool orthoSnap = false;
        double shownDeg = polarDeg;
        if (snapped) {
            const double candidates[] = {0.0, 90.0, 180.0, 270.0};
            for (double c : candidates) {
                if (std::abs(polarDeg - c) <= 0.51) {
                    orthoSnap = true;
                    shownDeg = c;
                    break;
                }
            }
        }
        if (orthoSnap) {
            ShowDynPrompt(
                QStringLiteral("极轴: %1 < %2°")
                    .arg(length, 0, 'f', 4)
                    .arg(shownDeg, 0, 'f', 0),
                viewPos);
        } else {
            ShowDynPrompt(QStringLiteral("指定圆弧的端点:"), viewPos);
        }
    } else if (IsArcChordLengthPick()) {
        bool snapped = false;
        const double tracked = ApplyPolarTracking(angleRad, &snapped);
        const double polarDeg =
            DrawAlgorithms::ToPositiveAngleDegrees(tracked * kRadToDeg);
        bool orthoSnap = false;
        double shownDeg = polarDeg;
        if (snapped) {
            const double candidates[] = {0.0, 90.0, 180.0, 270.0};
            for (double c : candidates) {
                if (std::abs(polarDeg - c) <= 0.51) {
                    orthoSnap = true;
                    shownDeg = c;
                    break;
                }
            }
        }
        if (orthoSnap) {
            ShowDynPrompt(
                QStringLiteral("极轴: %1 < %2°")
                    .arg(length, 0, 'f', 4)
                    .arg(shownDeg, 0, 'f', 0),
                viewPos);
        } else {
            ShowDynPrompt(
                QStringLiteral("指定弦长(按住 Ctrl 键以切换方向):"),
                viewPos);
        }
    } else if (IsArcStartEndRadiusPick()) {
        ShowDynPrompt(
            QStringLiteral("指定圆弧的半径(按住 Ctrl 键以切换方向):"),
            viewPos);
    } else if (IsArcIncludedAnglePick()) {
        ShowDynPrompt(
            QStringLiteral("指定夹角(按住 Ctrl 键以切换方向):"),
            viewPos);
    } else if (IsArcStartCenterEndEndPick()) {
        ShowDynPrompt(
            QStringLiteral("指定圆弧的端点(按住 Ctrl 键以切换方向)或"),
            viewPos);
    } else if (IsArcStartEndDirectionPick() || IsArcCenterStartEndEndPick()) {
        // 水平/垂直极轴吸附时显示「极轴: dist < ang°」；否则不显示命令旁白。
        bool orthoSnap = false;
        double polarDeg = 0.0;
        if (length > Tolerance::Model) {
            bool snapped = false;
            const double tracked = ApplyPolarTracking(angleRad, &snapped);
            constexpr double kPi = 3.14159265358979323846;
            polarDeg = DrawAlgorithms::ToPositiveAngleDegrees(tracked * 180.0 / kPi);
            if (snapped) {
                const double candidates[] = {0.0, 90.0, 180.0, 270.0};
                for (double c : candidates) {
                    if (std::abs(polarDeg - c) <= 0.51) {
                        orthoSnap = true;
                        polarDeg = c;
                        break;
                    }
                }
            }
        }
        if (orthoSnap) {
            ShowDynPrompt(
                QStringLiteral("极轴: %1 < %2°")
                    .arg(length, 0, 'f', 4)
                    .arg(polarDeg, 0, 'f', 0),
                viewPos);
        } else {
            HideDynPrompt();
        }
    } else if (IsContinuousPolylineDraw() && !inputPoints_.empty()) {
        if (drawOptions_.polylineLengthMode) {
            HideOptionInput();
            ShowDynPrompt(QStringLiteral("指定直线的长度:"), viewPos);
        } else {
            HideDynPrompt();
            UpdatePolylineOptionInput(viewPos);
        }
    } else {
        HideDynPrompt();
    }
    if (!polarInput_->isVisible()) {
        polarInput_->show();
        polarInput_->raise();
        const bool optionTyping = IsPolylineOptionEditing();
        if (!optionTyping) {
            if (angleOnlyDyn) {
                polarInput_->FocusAngle();
            } else {
                polarInput_->FocusLength();
            }
        }
    } else {
        polarInput_->raise();
    }
    if (optionInput_ && optionInput_->isVisible()) {
        optionInput_->raise();
    }
}

GeoPoint CadView::ResolveDynamicLineEndpoint(
    const GeoPoint& base,
    const GeoPoint& cursorPoint,
    bool* trackingSnappedOut) const
{
    const double dx = cursorPoint.X() - base.X();
    const double dy = cursorPoint.Y() - base.Y();
    double length = std::hypot(dx, dy);
    double angleRad = (length > Tolerance::Model) ? std::atan2(dy, dx) : 0.0;

    // 起点-圆心-弦长 / 圆心-起点-弦长：第三点使用距离，同时保留 AutoCAD 极轴追踪。
    // DYN 可为负（优弧）；定位用绝对值，符号经 SyncSclChordLengthOptions 传给引擎。
    if (IsArcChordLengthPick()) {
        bool trackingSnapped = false;
        if (polarInput_ && polarInput_->isVisible() && polarInput_->IsLengthLocked()) {
            length = std::abs(polarInput_->Length());
        }
        angleRad = ApplyPolarTracking(angleRad, &trackingSnapped);
        if (trackingSnappedOut) {
            *trackingSnappedOut = trackingSnapped;
        }
        if (length <= Tolerance::Model) {
            return base;
        }
        return GeoPoint(
            base.X() + std::cos(angleRad) * length,
            base.Y() + std::sin(angleRad) * length);
    }

    // 起点-端点-半径第三点：以上一顶点（端点）为基点，走与 LINE 相同的极轴长度/角度吸附。
    // （角度框隐藏，但极轴追踪仍生效；长度锁定 = 半径。）
    if (IsArcStartEndRadiusPick()) {
        bool trackingSnapped = false;
        if (polarInput_ && polarInput_->isVisible() && polarInput_->IsLengthLocked()) {
            length = std::abs(polarInput_->Length());
            if (!polarInput_->IsAngleLocked()) {
                angleRad = ApplyPolarTracking(angleRad, &trackingSnapped);
            }
        } else {
            angleRad = ApplyPolarTracking(angleRad, &trackingSnapped);
        }
        if (trackingSnappedOut) {
            *trackingSnappedOut = trackingSnapped;
        }
        if (length <= Tolerance::Model) {
            return base;
        }
        return GeoPoint(
            base.X() + std::cos(angleRad) * length,
            base.Y() + std::sin(angleRad) * length);
    }

    // 起点-端点-角度：第三点以起点为极轴原点；锁定角取幅值并保持光标同侧（Ctrl 只影响实线弧）。
    if (IsArcStartEndAnglePick()) {
        bool trackingSnapped = false;
        if (polarInput_ && polarInput_->isVisible() && polarInput_->IsAngleLocked()) {
            const double locked = polarInput_->AngleDegrees(); // 正数幅值
            const double cursorSign = (angleRad < 0.0) ? -1.0 : 1.0;
            angleRad = cursorSign * std::abs(locked) * 3.14159265358979323846 / 180.0;
            if (length < Tolerance::Model && inputPoints_.size() >= 2) {
                length = Distance2D(inputPoints_[0], inputPoints_[1]);
            }
        } else {
            angleRad = ApplyPolarTracking(angleRad, &trackingSnapped);
        }
        if (trackingSnappedOut) {
            *trackingSnappedOut = trackingSnapped;
        }
        if (length <= Tolerance::Model) {
            return base;
        }
        return GeoPoint(
            base.X() + std::cos(angleRad) * length,
            base.Y() + std::sin(angleRad) * length);
    }

    // 起点-端点-方向第三点：以上一顶点（端点）为极轴显示基点。
    // 角度框显示侧向角，但键入 >180° 时仍按绝对极轴角解释。
    if (IsArcStartEndDirectionPick()) {
        bool trackingSnapped = false;
        if (polarInput_ && polarInput_->isVisible() && polarInput_->IsAngleLocked()) {
            // SED 第三点存在 ±θ 两解。鼠标移动可用光标侧判别；纯键入时光标可能停在基点，
            // free angle≈0 会退化到 +θ，导致与鼠标吸附 90° 结果不一致。
            // 与 AutoCAD 对齐：基点处键入时优先取下侧解（-θ），与极轴 270° 对应。
            double freeAngleForLock = angleRad;
            if (std::hypot(dx, dy) <= Tolerance::Model) {
                freeAngleForLock = -1.57079632679489661923; // -90°
            }
            angleRad = DynInputLockedAngleRadians(
                polarInput_->AngleDegrees(), freeAngleForLock);
            if (length < Tolerance::Model && inputPoints_.size() >= 2) {
                length = Distance2D(inputPoints_[0], inputPoints_[1]);
            }
        } else {
            angleRad = ApplyPolarTracking(angleRad, &trackingSnapped);
        }
        if (trackingSnappedOut) {
            *trackingSnappedOut = trackingSnapped;
        }
        if (length <= Tolerance::Model) {
            return base;
        }
        return GeoPoint(
            base.X() + std::cos(angleRad) * length,
            base.Y() + std::sin(angleRad) * length);
    }

    // 起点-圆心-端点第三点：橙虚线跟到光标（可超出半径）；键入角度按相对 +X 的极角解释。
    if (IsArcStartCenterEndEndPick() && inputPoints_.size() >= 2) {
        const GeoPoint& center = inputPoints_[1];
        bool trackingSnapped = false;
        if (polarInput_ && polarInput_->isVisible() && polarInput_->IsAngleLocked()) {
            const double freeAng =
                (std::hypot(dx, dy) > Tolerance::Model) ? std::atan2(dy, dx) : angleRad;
            angleRad = DynInputLockedAngleRadians(polarInput_->AngleDegrees(), freeAng);
            if (length < Tolerance::Model) {
                const double radius = Distance2D(center, inputPoints_[0]);
                length = radius > Tolerance::Model ? radius : length;
            }
        } else {
            angleRad = ApplyPolarTracking(angleRad, &trackingSnapped);
        }
        if (trackingSnappedOut) {
            *trackingSnappedOut = trackingSnapped;
        }
        if (length <= Tolerance::Model) {
            return center;
        }
        return GeoPoint(
            center.X() + std::cos(angleRad) * length,
            center.Y() + std::sin(angleRad) * length);
    }

    // 起点-圆心-角度 / 圆心-起点-角度：第三点始终在圆弧上取角，不受光标距圆心远近影响。
    if (IsArcIncludedAnglePick() && inputPoints_.size() >= 2) {
        const GeoPoint& start = ArcIncludedAngleStartPoint();
        const GeoPoint& center = ArcIncludedAngleCenterPoint();
        const double radius = Distance2D(center, start);
        if (radius <= Tolerance::Model) {
            if (trackingSnappedOut) {
                *trackingSnappedOut = false;
            }
            return center;
        }
        const double startAng = std::atan2(start.Y() - center.Y(), start.X() - center.X());
        bool trackingSnapped = false;
        double radialAng = (length > Tolerance::Model) ? angleRad : startAng;
        if (polarInput_ && polarInput_->isVisible() && polarInput_->IsAngleLocked()) {
            // 键入正角=逆时针，负角=顺时针；Ctrl 再翻转。
            const double rawDeg = polarInput_->AngleDegrees();
            double magnitude = std::abs(rawDeg);
            bool clockwise = rawDeg < 0.0;
            if (drawOptions_.clockwise) {
                clockwise = !clockwise;
            }
            const double includedRad = (clockwise ? -magnitude : magnitude) * kPi / 180.0;
            radialAng = startAng + includedRad;
        } else if (drawOptions_.clockwise && length > Tolerance::Model) {
            // Ctrl：按顺时针包含角把光标方位落到圆上。
            double delta = angleRad - startAng;
            constexpr double kTwoPi = 6.28318530717958647692;
            while (delta > 0.0) {
                delta -= kTwoPi;
            }
            while (delta <= -kTwoPi) {
                delta += kTwoPi;
            }
            radialAng = startAng + delta;
        } else {
            radialAng = ApplyPolarTracking(angleRad, &trackingSnapped);
        }
        if (trackingSnappedOut) {
            *trackingSnappedOut = trackingSnapped;
        }
        return GeoPoint(
            center.X() + std::cos(radialAng) * radius,
            center.Y() + std::sin(radialAng) * radius);
    }

    // 圆心-起点-端点第三点：锁定角 = 侧向绝对角[0,180]；自由态按圆心极轴追踪。
    if (IsArcCenterStartEndEndPick() && inputPoints_.size() >= 2) {
        const GeoPoint& center = inputPoints_[0];
        const GeoPoint& start = inputPoints_[1];
        const double radius = Distance2D(center, start);
        bool trackingSnapped = false;
        if (polarInput_ && polarInput_->isVisible() && polarInput_->IsAngleLocked()) {
            const double freeAng =
                (std::hypot(dx, dy) > Tolerance::Model) ? std::atan2(dy, dx) : angleRad;
            angleRad = DynInputLockedAngleRadians(polarInput_->AngleDegrees(), freeAng);
            if (length < Tolerance::Model) {
                length = radius > Tolerance::Model ? radius : length;
            }
        } else {
            angleRad = ApplyPolarTracking(angleRad, &trackingSnapped);
        }
        if (trackingSnappedOut) {
            *trackingSnappedOut = trackingSnapped;
        }
        if (length <= Tolerance::Model) {
            return center;
        }
        return GeoPoint(
            center.X() + std::cos(angleRad) * length,
            center.Y() + std::sin(angleRad) * length);
    }

    // 连续：从衔接起点到端点，长度/角度均走极轴；键入角度按绝对极轴角解释。
    if (IsArcContinuePick()) {
        bool trackingSnapped = false;
        if (polarInput_ && polarInput_->isVisible()) {
            if (polarInput_->IsLengthLocked()) {
                length = std::abs(polarInput_->Length());
            }
            if (polarInput_->IsAngleLocked()) {
                angleRad = polarInput_->AngleDegrees() * kDegToRad;
            } else {
                angleRad = ApplyPolarTracking(angleRad, &trackingSnapped);
            }
        } else {
            angleRad = ApplyPolarTracking(angleRad, &trackingSnapped);
        }
        if (trackingSnappedOut) {
            *trackingSnappedOut = trackingSnapped;
        }
        if (length <= Tolerance::Model) {
            return base;
        }
        return GeoPoint(
            base.X() + std::cos(angleRad) * length,
            base.Y() + std::sin(angleRad) * length);
    }

    // 圆心-起点-端点第二点：DYN 角度按侧向角 [0,180] 解释。
    if (IsArcCenterStartEndStartPick()) {
        bool trackingSnapped = false;
        if (polarInput_ && polarInput_->isVisible()) {
            if (polarInput_->IsLengthLocked()) {
                length = std::abs(polarInput_->Length());
            }
            if (polarInput_->IsAngleLocked()) {
                if (drawOptions_.method == DrawMethod::ArcCenterStartChord) {
                    // AutoCAD C,S,L：第 2 步键入角度按绝对极轴角解释。
                    // 90° 固定为 +Y（圆心正上方），-90° 固定为 -Y，
                    // 不再依据自由光标所在侧把 90° 翻成 -90°。
                    angleRad = polarInput_->AngleDegrees() * kDegToRad;
                } else {
                    const double freeAng =
                        (std::hypot(dx, dy) > Tolerance::Model) ? std::atan2(dy, dx) : angleRad;
                    angleRad = DynInputLockedAngleRadians(polarInput_->AngleDegrees(), freeAng);
                }
            } else {
                angleRad = ApplyPolarTracking(angleRad, &trackingSnapped);
            }
        } else {
            angleRad = ApplyPolarTracking(angleRad, &trackingSnapped);
        }
        if (trackingSnappedOut) {
            *trackingSnappedOut = trackingSnapped;
        }
        if (length <= Tolerance::Model) {
            return base;
        }
        return GeoPoint(
            base.X() + std::cos(angleRad) * length,
            base.Y() + std::sin(angleRad) * length);
    }

    if (IsSplineDraw()) {
        bool trackingSnapped = false;
        if (polarInput_ && polarInput_->isVisible()) {
            if (polarInput_->IsLengthLocked()) {
                length = std::abs(polarInput_->Length());
            }
            if (polarInput_->IsAngleLocked()) {
                // AutoCAD 样条：键入角按绝对极轴 [0,360) 解释（90° 恒为 +Y）。
                angleRad = DrawAlgorithms::ToPositiveAngleDegrees(polarInput_->AngleDegrees())
                    * kDegToRad;
                if (!polarInput_->IsLengthLocked()) {
                    const double ux = std::cos(angleRad);
                    const double uy = std::sin(angleRad);
                    length = std::max(0.0, dx * ux + dy * uy);
                }
                ApplyPolarTracking(angleRad, &trackingSnapped);
            } else {
                angleRad = ApplyPolarTracking(angleRad, &trackingSnapped);
            }
        } else {
            angleRad = ApplyPolarTracking(angleRad, &trackingSnapped);
        }
        if (trackingSnappedOut) {
            *trackingSnappedOut = trackingSnapped;
        }
        if (length <= Tolerance::Model) {
            return base;
        }
        return GeoPoint(
            base.X() + std::cos(angleRad) * length,
            base.Y() + std::sin(angleRad) * length);
    }

    if (IsContinuousPolylineDraw() && drawOptions_.polylineLengthMode) {
        const GeoPoint tangent = PolylineDrawEndTangent();
        const double tlen = std::hypot(tangent.X(), tangent.Y());
        if (tlen > Tolerance::Model) {
            angleRad = std::atan2(tangent.Y(), tangent.X());
        }
        if (polarInput_ && polarInput_->isVisible() && polarInput_->IsLengthLocked()) {
            length = std::abs(polarInput_->Length());
        }
        if (trackingSnappedOut) {
            *trackingSnappedOut = false;
        }
        if (length <= Tolerance::Model) {
            return base;
        }
        return GeoPoint(
            base.X() + std::cos(angleRad) * length,
            base.Y() + std::sin(angleRad) * length);
    }

    bool trackingSnapped = false;
    if (!polarInput_ || (!polarInput_->IsLengthLocked() && !polarInput_->IsAngleLocked())) {
        angleRad = ApplyPolarTracking(angleRad, &trackingSnapped);
    }

    if (polarInput_ && polarInput_->isVisible()) {
        if (polarInput_->IsLengthLocked()) {
            length = std::abs(polarInput_->Length());
        }
        if (polarInput_->IsAngleLocked()) {
            const double freeAng =
                (std::hypot(dx, dy) > Tolerance::Model) ? std::atan2(dy, dx) : angleRad;
            // AutoCAD DYN: locked value is side-relative [0,180]; >180 = absolute CCW.
            angleRad = DynInputLockedAngleRadians(polarInput_->AngleDegrees(), freeAng);
            if (!polarInput_->IsLengthLocked()) {
                // Project cursor onto the locked-angle ray.
                const double ux = std::cos(angleRad);
                const double uy = std::sin(angleRad);
                length = std::max(0.0, dx * ux + dy * uy);
            }
            // Typed/locked angle on a polar increment still counts as polar tracking.
            ApplyPolarTracking(angleRad, &trackingSnapped);
        } else if (polarInput_->IsLengthLocked()) {
            // Keep free (possibly polar-tracked) angle with locked length.
            if (!trackingSnapped) {
                angleRad = (std::hypot(dx, dy) > Tolerance::Model) ? std::atan2(dy, dx) : angleRad;
                angleRad = ApplyPolarTracking(angleRad, &trackingSnapped);
            }
        }
    }

    if (trackingSnappedOut) {
        *trackingSnappedOut = trackingSnapped;
    }

    return GeoPoint(
        base.X() + std::cos(angleRad) * length,
        base.Y() + std::sin(angleRad) * length);
}

double CadView::ApplyPolarTracking(double angleRadians, bool* snapped) const
{
    if (snapped) {
        *snapped = false;
    }
    // AutoCAD-like polar increments (degrees). Keep signed (-180, 180]
    // so clockwise (negative) angles are not flipped to the long CCW way.
    constexpr double kIncrement = 30.0;
    constexpr double kSnapToleranceDeg = 4.0;
    constexpr double kPi = 3.14159265358979323846;
    const double signedDeg = NormalizeAngleSigned(angleRadians) * 180.0 / kPi;

    auto trySnap = [&](double increment) -> bool {
        const double snappedDeg = std::round(signedDeg / increment) * increment;
        double delta = std::abs(signedDeg - snappedDeg);
        if (delta > 180.0) {
            delta = 360.0 - delta;
        }
        if (delta <= kSnapToleranceDeg) {
            if (snapped) {
                *snapped = true;
            }
            angleRadians = NormalizeAngleSigned(snappedDeg * kPi / 180.0);
            return true;
        }
        return false;
    };

    if (trySnap(kIncrement) || trySnap(45.0)) {
        return angleRadians;
    }
    return angleRadians;
}

void CadView::UpdatePolarGuidePreview(
    const GeoPoint& base,
    const GeoPoint& end,
    double angleRadians,
    bool trackingSnapped,
    const GeoPoint* rawCursor)
{
    if (!scene()) {
        return;
    }
    // Specialized branches may return early; hide the optional green tracking
    // line first so it cannot leak from a previous chord-length frame.
    if (polarTrackingItem_) {
        polarTrackingItem_->hide();
    }

    const double length = std::hypot(end.X() - base.X(), end.Y() - base.Y());
    QPainterPath path;

    auto ensureItem = [this](QGraphicsPathItem*& item, qreal z) {
        if (!item || item->scene() != scene()) {
            if (item) {
                if (item->scene()) {
                    item->scene()->removeItem(item);
                }
                delete item;
            }
            item = scene()->addPath(QPainterPath(), QPen());
            item->setZValue(z);
            item->setCacheMode(QGraphicsItem::NoCache);
            item->setAcceptedMouseButtons(Qt::NoButton);
            item->setFlag(QGraphicsItem::ItemIsSelectable, false);
        }
    };

    auto applyDashedPen = [](QGraphicsPathItem* item, const QColor& color = QColor(200, 200, 200)) {
        QPen guidePen(color, 0);
        guidePen.setCosmetic(true);
        guidePen.setStyle(Qt::CustomDashLine);
        guidePen.setDashPattern({1.0, 2.2});
        guidePen.setCapStyle(Qt::RoundCap);
        item->setPen(guidePen);
        item->setBrush(Qt::NoBrush);
    };

    // AutoCAD 拟合样条 / 夹点拉伸：橙色位移橡胶筋；水平/垂直极轴时再画绿色无限极轴线。
    if (IsSplineDraw() || IsSplineGripEditing()) {
        if (polarGuideItem_) {
            polarGuideItem_->hide();
        }
        QPainterPath yellowPath;
        QPainterPath orthoPath;
        if (length > Tolerance::Model) {
            yellowPath.moveTo(ToScenePoint(base));
            yellowPath.lineTo(ToScenePoint(end));

            auto isOrthoPolarDegrees = [](double angleRad) -> bool {
                constexpr double kPi = 3.14159265358979323846;
                const double deg = NormalizeAngleSigned(angleRad) * 180.0 / kPi;
                const double candidates[] = {0.0, 90.0, -90.0, 180.0, -180.0};
                for (double candidate : candidates) {
                    if (std::abs(deg - candidate) <= 0.51) {
                        return true;
                    }
                }
                return false;
            };

            if (trackingSnapped && isOrthoPolarDegrees(angleRadians)) {
                const QRectF vis = VisibleModelRect(80.0);
                const double span = std::hypot(vis.width(), vis.height()) * 2.0 + 1.0;
                const double c = std::cos(angleRadians);
                const double s = std::sin(angleRadians);
                const GeoPoint a(base.X() - c * span, base.Y() - s * span);
                const GeoPoint b(base.X() + c * span, base.Y() + s * span);
                orthoPath.moveTo(ToScenePoint(a));
                orthoPath.lineTo(ToScenePoint(b));
            }
        }
        if (!yellowPath.isEmpty()) {
            ensureItem(polarRayItem_, 1101.0);
            QPen rayPen(QColor(255, 180, 48), 0);
            rayPen.setCosmetic(true);
            rayPen.setStyle(Qt::CustomDashLine);
            rayPen.setDashPattern({5.0, 3.5});
            rayPen.setCapStyle(Qt::FlatCap);
            polarRayItem_->setPen(rayPen);
            polarRayItem_->setBrush(Qt::NoBrush);
            polarRayItem_->setPath(yellowPath);
            polarRayItem_->show();
        } else if (polarRayItem_) {
            polarRayItem_->hide();
        }
        if (!orthoPath.isEmpty()) {
            ensureItem(polarTrackingItem_, 1100.0);
            QPen trackingPen(QColor(64, 220, 96), 0);
            trackingPen.setCosmetic(true);
            trackingPen.setStyle(Qt::CustomDashLine);
            trackingPen.setDashPattern({6.0, 4.0});
            trackingPen.setCapStyle(Qt::FlatCap);
            polarTrackingItem_->setPen(trackingPen);
            polarTrackingItem_->setBrush(Qt::NoBrush);
            polarTrackingItem_->setPath(orthoPath);
            polarTrackingItem_->show();
        }
        return;
    }

    // 圆心、半径 / 圆心、直径 第二点（对齐 AutoCAD）：
    // 黄虚线：圆心→光标（半径或直径橡胶筋）；正交极轴吸附时另绘绿色无限极轴线。
    // 直径模式下光标在圆外，距离=直径，圆半径=距离/2。
    if (IsCircleCenterPolarPick()) {
        QPainterPath yellowPath;
        QPainterPath orthoPath;

        if (length > Tolerance::Model) {
            yellowPath.moveTo(ToScenePoint(base));
            yellowPath.lineTo(ToScenePoint(end));

            auto isOrthoPolarDegrees = [](double angleRad) -> bool {
                constexpr double kPi = 3.14159265358979323846;
                const double deg = NormalizeAngleSigned(angleRad) * 180.0 / kPi;
                const double candidates[] = {0.0, 90.0, -90.0, 180.0, -180.0};
                for (double c : candidates) {
                    if (std::abs(deg - c) <= 0.51) {
                        return true;
                    }
                }
                return false;
            };

            if (trackingSnapped && isOrthoPolarDegrees(angleRadians)) {
                const QRectF vis = VisibleModelRect(80.0);
                const double span = std::hypot(vis.width(), vis.height()) * 2.0 + 1.0;
                const double c = std::cos(angleRadians);
                const double s = std::sin(angleRadians);
                const GeoPoint a(base.X() - c * span, base.Y() - s * span);
                const GeoPoint b(base.X() + c * span, base.Y() + s * span);
                orthoPath.moveTo(ToScenePoint(a));
                orthoPath.lineTo(ToScenePoint(b));
            }
        }

        if (polarGuideItem_) {
            polarGuideItem_->hide();
        }

        if (!yellowPath.isEmpty()) {
            ensureItem(polarRayItem_, 1101.0);
            QPen rayPen(QColor(255, 180, 48), 0);
            rayPen.setCosmetic(true);
            rayPen.setStyle(Qt::CustomDashLine);
            rayPen.setDashPattern({5.0, 3.5});
            rayPen.setCapStyle(Qt::FlatCap);
            polarRayItem_->setPen(rayPen);
            polarRayItem_->setBrush(Qt::NoBrush);
            polarRayItem_->setPath(yellowPath);
            polarRayItem_->show();
        } else if (polarRayItem_) {
            polarRayItem_->hide();
        }

        if (!orthoPath.isEmpty()) {
            ensureItem(polarTrackingItem_, 1100.0);
            QPen trackingPen(QColor(64, 220, 96), 0);
            trackingPen.setCosmetic(true);
            trackingPen.setStyle(Qt::CustomDashLine);
            trackingPen.setDashPattern({6.0, 4.0});
            trackingPen.setCapStyle(Qt::FlatCap);
            polarTrackingItem_->setPen(trackingPen);
            polarTrackingItem_->setBrush(Qt::NoBrush);
            polarTrackingItem_->setPath(orthoPath);
            polarTrackingItem_->show();
        }
        return;
    }

    // 两点圆第 2 点 / 三点圆第 2、3 点（对齐 AutoCAD）：
    // 黄虚线：上一顶点→光标；灰点线：长度廊道 + 0° 基线 + 角度轨迹；
    // 正交极轴吸附时绿色无限极轴线。长度框+角度框同时显示。
    if (IsCirclePolarSegmentPick()) {
        QPainterPath guidePath;
        QPainterPath yellowPath;
        QPainterPath orthoPath;

        if (length > Tolerance::Model) {
            const double dx = end.X() - base.X();
            const double dy = end.Y() - base.Y();
            const double frameW = ModelToleranceFromPixels(56.0);
            const double R = length;
            const double sweep = NormalizeAngleSigned(angleRadians);
            const double sweepAbs = std::abs(sweep);
            const double ux = dx / length;
            const double uy = dy / length;
            const bool nearVertical = std::abs(dx) < frameW * 0.85 && std::abs(dy) >= frameW;

            if (nearVertical) {
                const double leftX = std::min(base.X(), end.X()) - frameW;
                const double yHi = std::max(base.Y(), end.Y());
                const double yLo = std::min(base.Y(), end.Y());
                const double xAtHi = (base.Y() >= end.Y()) ? base.X() : end.X();
                const double xAtLo = (base.Y() < end.Y()) ? base.X() : end.X();
                const double rightX = base.X() + R;

                guidePath.moveTo(ToScenePoint(GeoPoint(leftX, yLo)));
                guidePath.lineTo(ToScenePoint(GeoPoint(leftX, yHi)));
                guidePath.moveTo(ToScenePoint(GeoPoint(leftX, yLo)));
                guidePath.lineTo(ToScenePoint(GeoPoint(xAtLo, yLo)));
                guidePath.moveTo(ToScenePoint(GeoPoint(leftX, yHi)));
                guidePath.lineTo(ToScenePoint(GeoPoint(xAtHi, yHi)));
                guidePath.moveTo(ToScenePoint(GeoPoint(xAtHi, yHi)));
                guidePath.lineTo(ToScenePoint(GeoPoint(rightX, yHi)));
                if (std::abs(yHi - base.Y()) > Tolerance::Model) {
                    guidePath.moveTo(ToScenePoint(base));
                    guidePath.lineTo(ToScenePoint(GeoPoint(rightX, base.Y())));
                }
            } else {
                double px = -uy;
                double py = ux;
                if (px > 1e-12 || (std::abs(px) <= 1e-12 && py > 0.0)) {
                    px = -px;
                    py = -py;
                }
                const GeoPoint p0(base.X() + px * frameW, base.Y() + py * frameW);
                const GeoPoint p1(end.X() + px * frameW, end.Y() + py * frameW);
                guidePath.moveTo(ToScenePoint(p0));
                guidePath.lineTo(ToScenePoint(p1));
                guidePath.moveTo(ToScenePoint(p0));
                guidePath.lineTo(ToScenePoint(base));
                guidePath.moveTo(ToScenePoint(p1));
                guidePath.lineTo(ToScenePoint(end));
                guidePath.moveTo(ToScenePoint(base));
                guidePath.lineTo(ToScenePoint(GeoPoint(base.X() + R, base.Y())));
            }

            if (sweepAbs > 1.0 * 3.14159265358979323846 / 180.0) {
                const int steps = std::max(48, static_cast<int>(sweepAbs / 0.025));
                for (int i = 0; i <= steps; ++i) {
                    const double t = static_cast<double>(i) / static_cast<double>(steps);
                    const double a = sweep * t;
                    const GeoPoint p(
                        base.X() + std::cos(a) * R,
                        base.Y() + std::sin(a) * R);
                    if (i == 0) {
                        guidePath.moveTo(ToScenePoint(p));
                    } else {
                        guidePath.lineTo(ToScenePoint(p));
                    }
                }
            }

            yellowPath.moveTo(ToScenePoint(base));
            yellowPath.lineTo(ToScenePoint(end));

            auto isOrthoPolarDegrees = [](double angleRad) -> bool {
                constexpr double kPi = 3.14159265358979323846;
                const double deg = NormalizeAngleSigned(angleRad) * 180.0 / kPi;
                const double candidates[] = {0.0, 90.0, -90.0, 180.0, -180.0};
                for (double c : candidates) {
                    if (std::abs(deg - c) <= 0.51) {
                        return true;
                    }
                }
                return false;
            };

            if (trackingSnapped && isOrthoPolarDegrees(angleRadians)) {
                const QRectF vis = VisibleModelRect(80.0);
                const double span = std::hypot(vis.width(), vis.height()) * 2.0 + 1.0;
                const double c = std::cos(angleRadians);
                const double s = std::sin(angleRadians);
                const GeoPoint a(base.X() - c * span, base.Y() - s * span);
                const GeoPoint b(base.X() + c * span, base.Y() + s * span);
                orthoPath.moveTo(ToScenePoint(a));
                orthoPath.lineTo(ToScenePoint(b));
            }
        }

        if (!guidePath.isEmpty()) {
            ensureItem(polarGuideItem_, 1102.0);
            applyDashedPen(polarGuideItem_, QColor(220, 220, 220));
            polarGuideItem_->setPath(guidePath);
            polarGuideItem_->show();
        } else if (polarGuideItem_) {
            polarGuideItem_->hide();
        }

        if (!orthoPath.isEmpty()) {
            ensureItem(polarTrackingItem_, 1100.0);
            QPen trackingPen(QColor(64, 220, 96), 0);
            trackingPen.setCosmetic(true);
            trackingPen.setStyle(Qt::CustomDashLine);
            trackingPen.setDashPattern({6.0, 4.0});
            trackingPen.setCapStyle(Qt::FlatCap);
            polarTrackingItem_->setPen(trackingPen);
            polarTrackingItem_->setBrush(Qt::NoBrush);
            polarTrackingItem_->setPath(orthoPath);
            polarTrackingItem_->show();
        }

        if (!yellowPath.isEmpty()) {
            ensureItem(polarRayItem_, 1101.0);
            QPen rayPen(QColor(255, 180, 48), 0);
            rayPen.setCosmetic(true);
            rayPen.setStyle(Qt::CustomDashLine);
            rayPen.setDashPattern({5.0, 3.5});
            rayPen.setCapStyle(Qt::FlatCap);
            polarRayItem_->setPen(rayPen);
            polarRayItem_->setBrush(Qt::NoBrush);
            polarRayItem_->setPath(yellowPath);
            polarRayItem_->show();
        } else if (polarRayItem_) {
            polarRayItem_->hide();
        }
        return;
    }

    // 圆心-起点-端点 第二点（对齐 AutoCAD）：
    // 黄虚线：圆心→起点（半径橡胶筋）
    // 白虚线：长度框平行追踪廊道 + 端帽 + 0° 基线 + 侧向角轨迹弧(0..180)
    if (IsArcCenterStartEndStartPick()) {
        QPainterPath guidePath;
        QPainterPath yellowPath;
        QPainterPath orthoPath;

        if (length > Tolerance::Model) {
            const double dx = end.X() - base.X();
            const double dy = end.Y() - base.Y();
            const double frameW = ModelToleranceFromPixels(56.0);
            const double R = length;
            const double sweep = NormalizeAngleSigned(angleRadians);
            const double sweepAbs = std::abs(sweep);
            const double ux = dx / length;
            const double uy = dy / length;
            const bool nearVertical = std::abs(dx) < frameW * 0.85 && std::abs(dy) >= frameW;

            if (nearVertical) {
                // 90°/270°：左侧平行竖线 + 端帽 + 顶部接到 0° 角
                const double leftX = std::min(base.X(), end.X()) - frameW;
                const double yHi = std::max(base.Y(), end.Y());
                const double yLo = std::min(base.Y(), end.Y());
                const double xAtHi = (base.Y() >= end.Y()) ? base.X() : end.X();
                const double xAtLo = (base.Y() < end.Y()) ? base.X() : end.X();
                const double rightX = base.X() + R;

                guidePath.moveTo(ToScenePoint(GeoPoint(leftX, yLo)));
                guidePath.lineTo(ToScenePoint(GeoPoint(leftX, yHi)));
                guidePath.moveTo(ToScenePoint(GeoPoint(leftX, yLo)));
                guidePath.lineTo(ToScenePoint(GeoPoint(xAtLo, yLo)));
                guidePath.moveTo(ToScenePoint(GeoPoint(leftX, yHi)));
                guidePath.lineTo(ToScenePoint(GeoPoint(xAtHi, yHi)));
                guidePath.moveTo(ToScenePoint(GeoPoint(xAtHi, yHi)));
                guidePath.lineTo(ToScenePoint(GeoPoint(rightX, yHi)));
                if (std::abs(yHi - base.Y()) > Tolerance::Model) {
                    guidePath.moveTo(ToScenePoint(base));
                    guidePath.lineTo(ToScenePoint(GeoPoint(rightX, base.Y())));
                }
            } else {
                // 斜向：与橡胶筋平行的左侧虚线廊道 + 端帽（长度框落在廊道内）
                double px = -uy;
                double py = ux;
                if (px > 1e-12 || (std::abs(px) <= 1e-12 && py > 0.0)) {
                    px = -px;
                    py = -py;
                }
                const GeoPoint p0(base.X() + px * frameW, base.Y() + py * frameW);
                const GeoPoint p1(end.X() + px * frameW, end.Y() + py * frameW);
                guidePath.moveTo(ToScenePoint(p0));
                guidePath.lineTo(ToScenePoint(p1));
                guidePath.moveTo(ToScenePoint(p0));
                guidePath.lineTo(ToScenePoint(base));
                guidePath.moveTo(ToScenePoint(p1));
                guidePath.lineTo(ToScenePoint(end));

                // 0° 基线
                guidePath.moveTo(ToScenePoint(base));
                guidePath.lineTo(ToScenePoint(GeoPoint(base.X() + R, base.Y())));
            }

            // 侧向角轨迹弧：从 +X 扫到当前侧向角，显示范围限制在 [0,180]。
            if (sweepAbs > 1.0 * 3.14159265358979323846 / 180.0) {
                const int steps = std::max(48, static_cast<int>(sweepAbs / 0.025));
                for (int i = 0; i <= steps; ++i) {
                    const double t = static_cast<double>(i) / static_cast<double>(steps);
                    const double a = sweep * t;
                    const GeoPoint p(
                        base.X() + std::cos(a) * R,
                        base.Y() + std::sin(a) * R);
                    if (i == 0) {
                        guidePath.moveTo(ToScenePoint(p));
                    } else {
                        guidePath.lineTo(ToScenePoint(p));
                    }
                }
            }

            yellowPath.moveTo(ToScenePoint(base));
            yellowPath.lineTo(ToScenePoint(end));

            auto isOrthoPolarDegrees = [](double angleRad) -> bool {
                constexpr double kPi = 3.14159265358979323846;
                const double deg = NormalizeAngleSigned(angleRad) * 180.0 / kPi;
                const double candidates[] = {0.0, 90.0, -90.0, 180.0, -180.0};
                for (double c : candidates) {
                    if (std::abs(deg - c) <= 0.51) {
                        return true;
                    }
                }
                return false;
            };

            if (isOrthoPolarDegrees(angleRadians)) {
                const QRectF vis = VisibleModelRect(80.0);
                const double span = std::hypot(vis.width(), vis.height()) * 2.0 + 1.0;
                const double c = std::cos(angleRadians);
                const double s = std::sin(angleRadians);
                const GeoPoint a(base.X() - c * span, base.Y() - s * span);
                const GeoPoint b(base.X() + c * span, base.Y() + s * span);
                orthoPath.moveTo(ToScenePoint(a));
                orthoPath.lineTo(ToScenePoint(b));
            }
        }

        if (!guidePath.isEmpty()) {
            ensureItem(polarGuideItem_, 1102.0);
            applyDashedPen(polarGuideItem_, QColor(220, 220, 220));
            polarGuideItem_->setPath(guidePath);
            polarGuideItem_->show();
        } else if (polarGuideItem_) {
            polarGuideItem_->hide();
        }

        if (!orthoPath.isEmpty()) {
            ensureItem(polarRayItem_, 1101.0);
            QPen rayPen(QColor(64, 220, 96), 0);
            rayPen.setCosmetic(true);
            rayPen.setStyle(Qt::CustomDashLine);
            rayPen.setDashPattern({6.0, 4.0});
            rayPen.setCapStyle(Qt::FlatCap);
            polarRayItem_->setPen(rayPen);
            polarRayItem_->setBrush(Qt::NoBrush);
            polarRayItem_->setPath(orthoPath);
            polarRayItem_->show();
        } else if (!yellowPath.isEmpty()) {
            ensureItem(polarRayItem_, 1101.0);
            QPen rayPen(QColor(255, 180, 48), 0);
            rayPen.setCosmetic(true);
            rayPen.setStyle(Qt::CustomDashLine);
            rayPen.setDashPattern({5.0, 3.5});
            rayPen.setCapStyle(Qt::FlatCap);
            polarRayItem_->setPen(rayPen);
            polarRayItem_->setBrush(Qt::NoBrush);
            polarRayItem_->setPath(yellowPath);
            polarRayItem_->show();
        } else if (polarRayItem_) {
            polarRayItem_->hide();
        }
        return;
    }

    // 连续（AutoCAD）：从衔接起点做极轴 DYN。
    // 灰点线：长度廊道 + 0° 基线 + 角度轨迹 + 相切圆虚线 + 圆心十字。
    // 正交吸附：绿色无限极轴线；否则黄虚线起点→端点。
    if (IsArcContinuePick()) {
        QPainterPath guidePath;
        QPainterPath yellowPath;
        QPainterPath orthoPath;

        auto isOrthoPolarDegrees = [](double angleRad) -> bool {
            constexpr double kPi = 3.14159265358979323846;
            const double deg = NormalizeAngleSigned(angleRad) * 180.0 / kPi;
            const double candidates[] = {0.0, 90.0, -90.0, 180.0, -180.0};
            for (double c : candidates) {
                if (std::abs(deg - c) <= 0.51) {
                    return true;
                }
            }
            return false;
        };

        if (length > Tolerance::Model) {
            const double dx = end.X() - base.X();
            const double dy = end.Y() - base.Y();
            const double frameW = ModelToleranceFromPixels(56.0);
            const double R = length;
            const double sweep = NormalizeAngleSigned(angleRadians);
            const double sweepAbs = std::abs(sweep);
            const double ux = dx / length;
            const double uy = dy / length;
            const bool nearVertical = std::abs(dx) < frameW * 0.85 && std::abs(dy) >= frameW;

            if (nearVertical) {
                const double leftX = std::min(base.X(), end.X()) - frameW;
                const double yHi = std::max(base.Y(), end.Y());
                const double yLo = std::min(base.Y(), end.Y());
                const double xAtHi = (base.Y() >= end.Y()) ? base.X() : end.X();
                const double xAtLo = (base.Y() < end.Y()) ? base.X() : end.X();
                const double rightX = base.X() + R;

                guidePath.moveTo(ToScenePoint(GeoPoint(leftX, yLo)));
                guidePath.lineTo(ToScenePoint(GeoPoint(leftX, yHi)));
                guidePath.moveTo(ToScenePoint(GeoPoint(leftX, yLo)));
                guidePath.lineTo(ToScenePoint(GeoPoint(xAtLo, yLo)));
                guidePath.moveTo(ToScenePoint(GeoPoint(leftX, yHi)));
                guidePath.lineTo(ToScenePoint(GeoPoint(xAtHi, yHi)));
                guidePath.moveTo(ToScenePoint(GeoPoint(xAtHi, yHi)));
                guidePath.lineTo(ToScenePoint(GeoPoint(rightX, yHi)));
                if (std::abs(yHi - base.Y()) > Tolerance::Model) {
                    guidePath.moveTo(ToScenePoint(base));
                    guidePath.lineTo(ToScenePoint(GeoPoint(rightX, base.Y())));
                }
            } else {
                double px = -uy;
                double py = ux;
                if (px > 1e-12 || (std::abs(px) <= 1e-12 && py > 0.0)) {
                    px = -px;
                    py = -py;
                }
                const GeoPoint p0(base.X() + px * frameW, base.Y() + py * frameW);
                const GeoPoint p1(end.X() + px * frameW, end.Y() + py * frameW);
                guidePath.moveTo(ToScenePoint(p0));
                guidePath.lineTo(ToScenePoint(p1));
                guidePath.moveTo(ToScenePoint(p0));
                guidePath.lineTo(ToScenePoint(base));
                guidePath.moveTo(ToScenePoint(p1));
                guidePath.lineTo(ToScenePoint(end));
                guidePath.moveTo(ToScenePoint(base));
                guidePath.lineTo(ToScenePoint(GeoPoint(base.X() + R, base.Y())));
            }

            if (sweepAbs > 1.0 * 3.14159265358979323846 / 180.0) {
                const int steps = std::max(48, static_cast<int>(sweepAbs / 0.025));
                for (int i = 0; i <= steps; ++i) {
                    const double t = static_cast<double>(i) / static_cast<double>(steps);
                    const double a = sweep * t;
                    const GeoPoint p(
                        base.X() + std::cos(a) * R,
                        base.Y() + std::sin(a) * R);
                    if (i == 0) {
                        guidePath.moveTo(ToScenePoint(p));
                    } else {
                        guidePath.lineTo(ToScenePoint(p));
                    }
                }
            }

            yellowPath.moveTo(ToScenePoint(base));
            yellowPath.lineTo(ToScenePoint(end));

            if (isOrthoPolarDegrees(angleRadians)) {
                const QRectF vis = VisibleModelRect(80.0);
                const double span = std::hypot(vis.width(), vis.height()) * 2.0 + 1.0;
                const double c = std::cos(angleRadians);
                const double s = std::sin(angleRadians);
                const GeoPoint a(base.X() - c * span, base.Y() - s * span);
                const GeoPoint b(base.X() + c * span, base.Y() + s * span);
                orthoPath.moveTo(ToScenePoint(a));
                orthoPath.lineTo(ToScenePoint(b));
            }
        }

        if (auto arc = DrawAlgorithms::ArcFromTangentStartEnd(
                lastArcContinue_.start, lastArcContinue_.tangentDirection, end)) {
            constexpr double kTwoPi = 6.28318530717958647692;
            const GeoPoint center = arc->Center();
            const double radius = arc->Radius();
            if (radius > Tolerance::Model) {
                const int steps = 96;
                for (int i = 0; i <= steps; ++i) {
                    const double a = kTwoPi * static_cast<double>(i) / static_cast<double>(steps);
                    const GeoPoint p(
                        center.X() + std::cos(a) * radius,
                        center.Y() + std::sin(a) * radius);
                    if (i == 0) {
                        guidePath.moveTo(ToScenePoint(p));
                    } else {
                        guidePath.lineTo(ToScenePoint(p));
                    }
                }
                const double mark = ModelToleranceFromPixels(8.0);
                guidePath.moveTo(ToScenePoint(GeoPoint(center.X() - mark, center.Y())));
                guidePath.lineTo(ToScenePoint(GeoPoint(center.X() + mark, center.Y())));
                guidePath.moveTo(ToScenePoint(GeoPoint(center.X(), center.Y() - mark)));
                guidePath.lineTo(ToScenePoint(GeoPoint(center.X(), center.Y() + mark)));
            }
        }

        if (!guidePath.isEmpty()) {
            ensureItem(polarGuideItem_, 1102.0);
            applyDashedPen(polarGuideItem_, QColor(220, 220, 220));
            polarGuideItem_->setPath(guidePath);
            polarGuideItem_->show();
        } else if (polarGuideItem_) {
            polarGuideItem_->hide();
        }

        if (!orthoPath.isEmpty()) {
            ensureItem(polarRayItem_, 1101.0);
            QPen rayPen(QColor(64, 220, 96), 0);
            rayPen.setCosmetic(true);
            rayPen.setStyle(Qt::CustomDashLine);
            rayPen.setDashPattern({6.0, 4.0});
            rayPen.setCapStyle(Qt::FlatCap);
            polarRayItem_->setPen(rayPen);
            polarRayItem_->setBrush(Qt::NoBrush);
            polarRayItem_->setPath(orthoPath);
            polarRayItem_->show();
        } else if (!yellowPath.isEmpty()) {
            ensureItem(polarRayItem_, 1101.0);
            QPen rayPen(QColor(255, 180, 48), 0);
            rayPen.setCosmetic(true);
            rayPen.setStyle(Qt::CustomDashLine);
            rayPen.setDashPattern({5.0, 3.5});
            rayPen.setCapStyle(Qt::FlatCap);
            polarRayItem_->setPen(rayPen);
            polarRayItem_->setBrush(Qt::NoBrush);
            polarRayItem_->setPath(yellowPath);
            polarRayItem_->show();
        } else if (polarRayItem_) {
            polarRayItem_->hide();
        }
        return;
    }

    // 圆心-起点-端点 第三点（AutoCAD 图3）：
    // 白虚线整圆（半径=圆心→起点）；正交极轴吸附时绿色无限极轴线。
    if (IsArcCenterStartEndEndPick() && inputPoints_.size() >= 2) {
        QPainterPath guidePath;
        QPainterPath orthoPath;

        const GeoPoint& center = inputPoints_[0];
        const GeoPoint& start = inputPoints_[1];
        const double radius = Distance2D(center, start);
        if (radius > Tolerance::Model) {
            constexpr double kTwoPi = 6.28318530717958647692;
            const int steps = 96;
            for (int i = 0; i <= steps; ++i) {
                const double a = kTwoPi * static_cast<double>(i) / static_cast<double>(steps);
                const GeoPoint p(
                    center.X() + std::cos(a) * radius,
                    center.Y() + std::sin(a) * radius);
                if (i == 0) {
                    guidePath.moveTo(ToScenePoint(p));
                } else {
                    guidePath.lineTo(ToScenePoint(p));
                }
            }
        }

        auto isOrthoPolarDegrees = [](double angleRad) -> bool {
            constexpr double kPi = 3.14159265358979323846;
            const double deg = NormalizeAngleSigned(angleRad) * 180.0 / kPi;
            const double candidates[] = {0.0, 90.0, -90.0, 180.0, -180.0};
            for (double c : candidates) {
                if (std::abs(deg - c) <= 0.51) {
                    return true;
                }
            }
            return false;
        };

        if (trackingSnapped && isOrthoPolarDegrees(angleRadians) && length > Tolerance::Model) {
            const QRectF vis = VisibleModelRect(80.0);
            const double span = std::hypot(vis.width(), vis.height()) * 2.0 + 1.0;
            const double c = std::cos(angleRadians);
            const double s = std::sin(angleRadians);
            const GeoPoint a(base.X() - c * span, base.Y() - s * span);
            const GeoPoint b(base.X() + c * span, base.Y() + s * span);
            orthoPath.moveTo(ToScenePoint(a));
            orthoPath.lineTo(ToScenePoint(b));
        }

        if (!guidePath.isEmpty()) {
            ensureItem(polarGuideItem_, 1102.0);
            QPen guidePen(QColor(230, 230, 230), 0);
            guidePen.setCosmetic(true);
            guidePen.setStyle(Qt::CustomDashLine);
            guidePen.setDashPattern({2.0, 2.5});
            guidePen.setCapStyle(Qt::RoundCap);
            polarGuideItem_->setPen(guidePen);
            polarGuideItem_->setBrush(Qt::NoBrush);
            polarGuideItem_->setPath(guidePath);
            polarGuideItem_->show();
        } else if (polarGuideItem_) {
            polarGuideItem_->hide();
        }

        if (!orthoPath.isEmpty()) {
            ensureItem(polarRayItem_, 1101.0);
            QPen rayPen(QColor(64, 220, 96), 0);
            rayPen.setCosmetic(true);
            rayPen.setStyle(Qt::CustomDashLine);
            rayPen.setDashPattern({6.0, 4.0});
            rayPen.setCapStyle(Qt::FlatCap);
            polarRayItem_->setPen(rayPen);
            polarRayItem_->setBrush(Qt::NoBrush);
            polarRayItem_->setPath(orthoPath);
            polarRayItem_->show();
        } else if (polarRayItem_) {
            polarRayItem_->hide();
        }
        return;
    }

    // 起点-圆心-长度 / 圆心-起点-长度第三点（AutoCAD）：
    // 黄虚线：起点→当前长度点（弦长输入线）。
    // 灰点线：起点→当前长度点的平行测量框（偏移线 + 两端帽），不是几何三角形。
    // 长度框由 UpdatePolarInput 的通用走廊定位放在该灰点线上。
    if (IsArcChordLengthPick()) {
        const GeoPoint start = ArcChordLengthStartPoint();
        QPainterPath guidePath;
        QPainterPath rayPath;
        const double chordInputLength = Distance2D(start, end);
        if (chordInputLength > Tolerance::Model) {
            rayPath.moveTo(ToScenePoint(start));
            rayPath.lineTo(ToScenePoint(end));

            const double ux = (end.X() - start.X()) / chordInputLength;
            const double uy = (end.Y() - start.Y()) / chordInputLength;
            double px = -uy;
            double py = ux;
            // Match the DYN length-box corridor side used by UpdatePolarInput.
            if (px > 1.0e-12 || (std::abs(px) <= 1.0e-12 && py > 0.0)) {
                px = -px;
                py = -py;
            }
            const double frameW = ModelToleranceFromPixels(56.0);
            const GeoPoint p0(start.X() + px * frameW, start.Y() + py * frameW);
            const GeoPoint p1(end.X() + px * frameW, end.Y() + py * frameW);
            guidePath.moveTo(ToScenePoint(p0));
            guidePath.lineTo(ToScenePoint(p1));
            guidePath.moveTo(ToScenePoint(p0));
            guidePath.lineTo(ToScenePoint(start));
            guidePath.moveTo(ToScenePoint(p1));
            guidePath.lineTo(ToScenePoint(end));
        }

        if (trackingSnapped && chordInputLength > Tolerance::Model) {
            const QRectF vis = VisibleModelRect(80.0);
            const double span = std::hypot(vis.width(), vis.height()) * 2.0 + 1.0;
            const double c = std::cos(angleRadians);
            const double s = std::sin(angleRadians);
            const GeoPoint a(start.X() - c * span, start.Y() - s * span);
            const GeoPoint b(start.X() + c * span, start.Y() + s * span);
            QPainterPath trackingPath;
            trackingPath.moveTo(ToScenePoint(a));
            trackingPath.lineTo(ToScenePoint(b));
            ensureItem(polarTrackingItem_, 1100.0);
            QPen trackingPen(QColor(64, 220, 96), 0);
            trackingPen.setCosmetic(true);
            trackingPen.setStyle(Qt::CustomDashLine);
            trackingPen.setDashPattern({6.0, 4.0});
            trackingPen.setCapStyle(Qt::FlatCap);
            polarTrackingItem_->setPen(trackingPen);
            polarTrackingItem_->setBrush(Qt::NoBrush);
            polarTrackingItem_->setPath(trackingPath);
            polarTrackingItem_->show();
        }

        if (!guidePath.isEmpty()) {
            ensureItem(polarGuideItem_, 1102.0);
            QPen guidePen(QColor(170, 170, 170), 0);
            guidePen.setCosmetic(true);
            guidePen.setStyle(Qt::CustomDashLine);
            guidePen.setDashPattern({1.0, 2.4});
            guidePen.setCapStyle(Qt::RoundCap);
            polarGuideItem_->setPen(guidePen);
            polarGuideItem_->setBrush(Qt::NoBrush);
            polarGuideItem_->setPath(guidePath);
            polarGuideItem_->show();
        } else if (polarGuideItem_) {
            polarGuideItem_->hide();
        }

        if (!rayPath.isEmpty()) {
            ensureItem(polarRayItem_, 1101.0);
            QPen rayPen(QColor(255, 190, 32), 0);
            rayPen.setCosmetic(true);
            rayPen.setStyle(Qt::CustomDashLine);
            rayPen.setDashPattern({5.0, 3.5});
            rayPen.setCapStyle(Qt::FlatCap);
            polarRayItem_->setPen(rayPen);
            polarRayItem_->setBrush(Qt::NoBrush);
            polarRayItem_->setPath(rayPath);
            polarRayItem_->show();
        } else if (polarRayItem_) {
            polarRayItem_->hide();
        }
        return;
    }

    // 起点-端点-半径第三点：以上一顶点（端点）为基点。
    // 橙虚线：端点→光标；白虚线：极轴左侧平行追踪线（对齐 AutoCAD 图2）。
    // 不画角度弧、不画 0° 基线长横线。
    if (IsArcStartEndRadiusPick() && inputPoints_.size() >= 2) {
        if (length > Tolerance::Model) {
            ensureItem(polarRayItem_, 1101.0);
            QPainterPath rayPath;
            rayPath.moveTo(ToScenePoint(base));
            rayPath.lineTo(ToScenePoint(end));
            QPen rayPen(QColor(255, 180, 48), 0);
            rayPen.setCosmetic(true);
            rayPen.setStyle(Qt::CustomDashLine);
            rayPen.setDashPattern({5.0, 3.5});
            rayPen.setCapStyle(Qt::FlatCap);
            polarRayItem_->setPen(rayPen);
            polarRayItem_->setBrush(Qt::NoBrush);
            polarRayItem_->setPath(rayPath);
            polarRayItem_->show();

            QPainterPath guidePath;
            const double dx = end.X() - base.X();
            const double dy = end.Y() - base.Y();
            const double frameW = ModelToleranceFromPixels(56.0);
            const bool nearVertical = std::abs(dx) < frameW * 0.85 && std::abs(dy) >= frameW;
            if (nearVertical) {
                // 左侧平行竖线 + 两端短横（与 LINE 垂直极轴框一致，无右侧/0° 长线）
                const double leftX = std::min(base.X(), end.X()) - frameW;
                const double yHi = std::max(base.Y(), end.Y());
                const double yLo = std::min(base.Y(), end.Y());
                const double xAtHi = (base.Y() >= end.Y()) ? base.X() : end.X();
                const double xAtLo = (base.Y() < end.Y()) ? base.X() : end.X();
                guidePath.moveTo(ToScenePoint(GeoPoint(leftX, yLo)));
                guidePath.lineTo(ToScenePoint(GeoPoint(leftX, yHi)));
                guidePath.moveTo(ToScenePoint(GeoPoint(leftX, yLo)));
                guidePath.lineTo(ToScenePoint(GeoPoint(xAtLo, yLo)));
                guidePath.moveTo(ToScenePoint(GeoPoint(leftX, yHi)));
                guidePath.lineTo(ToScenePoint(GeoPoint(xAtHi, yHi)));
            } else {
                // 斜向：与橡胶筋平行的左侧虚线 + 端帽
                const double ux = dx / length;
                const double uy = dy / length;
                double px = -uy;
                double py = ux;
                if (px < -1e-12 || (std::abs(px) <= 1e-12 && py < 0.0)) {
                    px = -px;
                    py = -py;
                }
                const GeoPoint p0(base.X() + px * frameW, base.Y() + py * frameW);
                const GeoPoint p1(end.X() + px * frameW, end.Y() + py * frameW);
                guidePath.moveTo(ToScenePoint(p0));
                guidePath.lineTo(ToScenePoint(p1));
                guidePath.moveTo(ToScenePoint(p0));
                guidePath.lineTo(ToScenePoint(base));
                guidePath.moveTo(ToScenePoint(p1));
                guidePath.lineTo(ToScenePoint(end));
            }

            ensureItem(polarGuideItem_, 1102.0);
            applyDashedPen(polarGuideItem_, QColor(220, 220, 220));
            polarGuideItem_->setPath(guidePath);
            polarGuideItem_->show();
        } else {
            if (polarRayItem_) {
                polarRayItem_->hide();
            }
            if (polarGuideItem_) {
                polarGuideItem_->hide();
            }
        }
        return;
    }

    // 起点-圆心-角度 / 圆心-起点-角度第三点（对齐 AutoCAD）：
    // 白虚线：起点径向（圆弧半径）+ 包含角轨迹弧（半径=max(光标距, 圆弧半径)）
    // 黄虚线：圆心→光标（可超出圆弧半径，表示当前扫掠方向）
    // 扫掠角：正=逆时针，负/Ctrl=顺时针。
    if (IsArcIncludedAnglePick()) {
        QPainterPath trajPath;
        QPainterPath armPath;

        auto appendTrajectory = [&](const GeoPoint& center,
                                    double startAng,
                                    double includedSweep,
                                    double frameR) {
            if (frameR <= Tolerance::Model
                || std::abs(includedSweep) <= 1.0 * kDegToRad) {
                return;
            }
            const int steps = std::max(48, static_cast<int>(std::abs(includedSweep) / 0.025));
            for (int i = 0; i <= steps; ++i) {
                const double t = static_cast<double>(i) / static_cast<double>(steps);
                const double a = startAng + includedSweep * t;
                const GeoPoint p(
                    center.X() + std::cos(a) * frameR,
                    center.Y() + std::sin(a) * frameR);
                if (i == 0) {
                    trajPath.moveTo(ToScenePoint(p));
                } else {
                    trajPath.lineTo(ToScenePoint(p));
                }
            }
        };

        const GeoPoint pickCursor = rawCursor ? *rawCursor : end;
        if (inputPoints_.size() >= 1) {
            const GeoPoint& start = ArcIncludedAngleStartPoint();
            const double startAng = std::atan2(start.Y() - base.Y(), start.X() - base.X());
            const double radius = Distance2D(base, start);
            const double cursorDist = Distance2D(base, pickCursor);
            if (radius > Tolerance::Model && cursorDist > Tolerance::Model) {
                double sweep = 0.0;
                // 键入夹角（含负角）时按显式扫掠画轨迹，避免端点在顺时针侧却画成 CCW 270° 虚线。
                if (polarInput_ && polarInput_->isVisible() && polarInput_->IsAngleLocked()) {
                    const double rawDeg = polarInput_->AngleDegrees();
                    double magnitude = std::abs(rawDeg);
                    bool clockwise = rawDeg < 0.0;
                    if (drawOptions_.clockwise) {
                        clockwise = !clockwise;
                    }
                    sweep = (clockwise ? -magnitude : magnitude) * kDegToRad;
                } else {
                    double delta = angleRadians - startAng;
                    constexpr double kTwoPiLocal = 6.28318530717958647692;
                    if (drawOptions_.clockwise) {
                        while (delta > 0.0) {
                            delta -= kTwoPiLocal;
                        }
                        while (delta <= -kTwoPiLocal) {
                            delta += kTwoPiLocal;
                        }
                    } else {
                        while (delta <= 0.0) {
                            delta += kTwoPiLocal;
                        }
                        while (delta > kTwoPiLocal) {
                            delta -= kTwoPiLocal;
                        }
                        if (delta >= kTwoPiLocal - 1.0e-9) {
                            delta = 0.0;
                        }
                    }
                    sweep = delta;
                }
                const double frameR = std::max(cursorDist, radius);

                // 白虚线：起点径向（圆弧半径）
                trajPath.moveTo(ToScenePoint(base));
                trajPath.lineTo(ToScenePoint(GeoPoint(
                    base.X() + std::cos(startAng) * radius,
                    base.Y() + std::sin(startAng) * radius)));
                appendTrajectory(base, startAng, sweep, frameR);

                // 黄虚线：圆心→光标
                armPath.moveTo(ToScenePoint(base));
                armPath.lineTo(ToScenePoint(pickCursor));
            }
        }

        if (!trajPath.isEmpty()) {
            ensureItem(polarGuideItem_, 1102.0);
            QPen trajPen(QColor(230, 230, 230), 0);
            trajPen.setCosmetic(true);
            trajPen.setStyle(Qt::CustomDashLine);
            trajPen.setDashPattern({2.0, 2.5});
            trajPen.setCapStyle(Qt::RoundCap);
            polarGuideItem_->setPen(trajPen);
            polarGuideItem_->setBrush(Qt::NoBrush);
            polarGuideItem_->setPath(trajPath);
            polarGuideItem_->show();
        } else if (polarGuideItem_) {
            polarGuideItem_->hide();
        }

        if (!armPath.isEmpty()) {
            ensureItem(polarRayItem_, 1101.0);
            QPen chordPen(QColor(255, 220, 64), 0);
            chordPen.setCosmetic(true);
            chordPen.setStyle(Qt::CustomDashLine);
            chordPen.setDashPattern({4.0, 3.0});
            chordPen.setCapStyle(Qt::FlatCap);
            polarRayItem_->setPen(chordPen);
            polarRayItem_->setBrush(Qt::NoBrush);
            polarRayItem_->setPath(armPath);
            polarRayItem_->show();
        } else if (polarRayItem_) {
            polarRayItem_->hide();
        }
        return;
    }

    // 起点-端点-方向第三点：极轴显示原点为上一顶点（端点）。
    // 白虚线：0°(+X) 基线 + 有向角度轨迹弧；橙虚线：端点→方向（当前角）。
    // 水平/垂直吸附：绿色无限极轴线 + 「极轴: …」提示。
    if (IsArcStartEndDirectionPick() && inputPoints_.size() >= 2) {
        QPainterPath guidePath;
        QPainterPath yellowPath;
        QPainterPath orthoPath;

        auto isOrthoPolarDegrees = [](double angleRad) -> bool {
            constexpr double kPi = 3.14159265358979323846;
            const double deg = NormalizeAngleSigned(angleRad) * 180.0 / kPi;
            const double candidates[] = {0.0, 90.0, -90.0, 180.0, -180.0};
            for (double c : candidates) {
                if (std::abs(deg - c) <= 0.51) {
                    return true;
                }
            }
            return false;
        };

        // 与 SEA 一致：有向角（下方为负），轨迹半径 = 起点到光标距离。
        const double sweep = angleRadians;
        double frameR = length;
        if (frameR <= Tolerance::Model && inputPoints_.size() >= 2) {
            frameR = Distance2D(inputPoints_[0], inputPoints_[1]);
        }

        if (frameR > Tolerance::Model) {
            guidePath.moveTo(ToScenePoint(base));
            guidePath.lineTo(ToScenePoint(GeoPoint(base.X() + frameR, base.Y())));

            if (std::abs(sweep) > 1.0 * 3.14159265358979323846 / 180.0) {
                const int steps = std::max(48, static_cast<int>(std::abs(sweep) / 0.025));
                for (int i = 0; i <= steps; ++i) {
                    const double t = static_cast<double>(i) / static_cast<double>(steps);
                    const double a = sweep * t;
                    const GeoPoint p(
                        base.X() + std::cos(a) * frameR,
                        base.Y() + std::sin(a) * frameR);
                    if (i == 0) {
                        guidePath.moveTo(ToScenePoint(p));
                    } else {
                        guidePath.lineTo(ToScenePoint(p));
                    }
                }
            }

            yellowPath.moveTo(ToScenePoint(base));
            yellowPath.lineTo(ToScenePoint(GeoPoint(
                base.X() + std::cos(sweep) * frameR,
                base.Y() + std::sin(sweep) * frameR)));
        }

        const bool orthoSnap = trackingSnapped && isOrthoPolarDegrees(angleRadians);
        if (orthoSnap && length > Tolerance::Model) {
            const QRectF vis = VisibleModelRect(80.0);
            const double span = std::hypot(vis.width(), vis.height()) * 2.0 + 1.0;
            const double c = std::cos(angleRadians);
            const double s = std::sin(angleRadians);
            const GeoPoint a(base.X() - c * span, base.Y() - s * span);
            const GeoPoint b(base.X() + c * span, base.Y() + s * span);
            orthoPath.moveTo(ToScenePoint(a));
            orthoPath.lineTo(ToScenePoint(b));
        }

        if (!guidePath.isEmpty()) {
            ensureItem(polarGuideItem_, 1102.0);
            QPen guidePen(QColor(230, 230, 230), 0);
            guidePen.setCosmetic(true);
            guidePen.setStyle(Qt::CustomDashLine);
            guidePen.setDashPattern({1.5, 2.5});
            guidePen.setCapStyle(Qt::RoundCap);
            polarGuideItem_->setPen(guidePen);
            polarGuideItem_->setBrush(Qt::NoBrush);
            polarGuideItem_->setPath(guidePath);
            polarGuideItem_->show();
        } else if (polarGuideItem_) {
            polarGuideItem_->hide();
        }

        // 水平/垂直时优先显示绿色极轴线；否则显示橙色方向射线。
        if (!orthoPath.isEmpty()) {
            ensureItem(polarRayItem_, 1101.0);
            QPen rayPen(QColor(64, 220, 96), 0);
            rayPen.setCosmetic(true);
            rayPen.setStyle(Qt::CustomDashLine);
            rayPen.setDashPattern({6.0, 4.0});
            rayPen.setCapStyle(Qt::FlatCap);
            polarRayItem_->setPen(rayPen);
            polarRayItem_->setBrush(Qt::NoBrush);
            polarRayItem_->setPath(orthoPath);
            polarRayItem_->show();
        } else if (!yellowPath.isEmpty()) {
            ensureItem(polarRayItem_, 1101.0);
            QPen chordPen(QColor(255, 180, 48), 0);
            chordPen.setCosmetic(true);
            chordPen.setStyle(Qt::CustomDashLine);
            chordPen.setDashPattern({4.0, 3.0});
            chordPen.setCapStyle(Qt::FlatCap);
            polarRayItem_->setPen(chordPen);
            polarRayItem_->setBrush(Qt::NoBrush);
            polarRayItem_->setPath(yellowPath);
            polarRayItem_->show();
        } else if (polarRayItem_) {
            polarRayItem_->hide();
        }
        return;
    }

    // 起点-端点-角度第三点：
    // 白虚线：起点 0°(+X) 基线 + 有向夹角轨迹（下方顺时针，幅值与 DYN 一致）
    // 黄虚线：自起点拉向光标（可延长到轨迹半径）
    // 不画弦(起点→端点)：与 AutoCAD SEA 一致，第三点阶段只保留角度引导。
    if (IsArcStartEndAnglePick() && inputPoints_.size() >= 2) {
        QPainterPath guidePath;
        QPainterPath yellowPath;

        const GeoPoint& startPt = inputPoints_[0];
        const GeoPoint& endPt = inputPoints_[1];
        // 引导用有向角（下方为负）；实线弧的优弧逻辑在 ArcFromStartEndAngle 内处理。
        const double sweep = angleRadians;
        double frameR = length;
        if (frameR <= Tolerance::Model) {
            frameR = Distance2D(startPt, endPt);
        }
        frameR = std::max(frameR, Distance2D(startPt, end));

        if (frameR > Tolerance::Model) {
            guidePath.moveTo(ToScenePoint(startPt));
            guidePath.lineTo(ToScenePoint(GeoPoint(startPt.X() + frameR, startPt.Y())));

            if (std::abs(sweep) > 1.0 * 3.14159265358979323846 / 180.0) {
                const int steps = std::max(48, static_cast<int>(std::abs(sweep) / 0.025));
                for (int i = 0; i <= steps; ++i) {
                    const double t = static_cast<double>(i) / static_cast<double>(steps);
                    const double a = sweep * t;
                    const GeoPoint p(
                        startPt.X() + std::cos(a) * frameR,
                        startPt.Y() + std::sin(a) * frameR);
                    if (i == 0) {
                        guidePath.moveTo(ToScenePoint(p));
                    } else {
                        guidePath.lineTo(ToScenePoint(p));
                    }
                }
            }

            yellowPath.moveTo(ToScenePoint(startPt));
            yellowPath.lineTo(ToScenePoint(GeoPoint(
                startPt.X() + std::cos(sweep) * frameR,
                startPt.Y() + std::sin(sweep) * frameR)));
        }

        if (!guidePath.isEmpty()) {
            ensureItem(polarGuideItem_, 1102.0);
            QPen guidePen(QColor(230, 230, 230), 0);
            guidePen.setCosmetic(true);
            guidePen.setStyle(Qt::CustomDashLine);
            guidePen.setDashPattern({1.5, 2.5});
            guidePen.setCapStyle(Qt::RoundCap);
            polarGuideItem_->setPen(guidePen);
            polarGuideItem_->setBrush(Qt::NoBrush);
            polarGuideItem_->setPath(guidePath);
            polarGuideItem_->show();
        } else if (polarGuideItem_) {
            polarGuideItem_->hide();
        }

        if (!yellowPath.isEmpty()) {
            ensureItem(polarRayItem_, 1101.0);
            QPen chordPen(QColor(255, 220, 64), 0);
            chordPen.setCosmetic(true);
            chordPen.setStyle(Qt::CustomDashLine);
            chordPen.setDashPattern({4.0, 3.0});
            chordPen.setCapStyle(Qt::FlatCap);
            polarRayItem_->setPen(chordPen);
            polarRayItem_->setBrush(Qt::NoBrush);
            polarRayItem_->setPath(yellowPath);
            polarRayItem_->show();
        } else if (polarRayItem_) {
            polarRayItem_->hide();
        }
        return;
    }

    // 起点-圆心-端点第三点：0°(+X) 基线 + 极角弧；橙虚线圆心→光标（可超出半径）。
    if (IsPolarAngleHintOnlyGuide()) {
        QPainterPath yellowPath;
        if (length > Tolerance::Model) {
            yellowPath.moveTo(ToScenePoint(base));
            yellowPath.lineTo(ToScenePoint(end));

            const double sweep = NormalizeAngleSigned(angleRadians);
            const double R = length;

            path.moveTo(ToScenePoint(base));
            path.lineTo(ToScenePoint(GeoPoint(base.X() + R, base.Y())));

            if (std::abs(sweep) > 1.0 * 3.14159265358979323846 / 180.0) {
                const int steps = std::max(36, static_cast<int>(std::abs(sweep) / 0.03));
                for (int i = 0; i <= steps; ++i) {
                    const double t = static_cast<double>(i) / static_cast<double>(steps);
                    const double a = sweep * t;
                    const GeoPoint p(
                        base.X() + std::cos(a) * R,
                        base.Y() + std::sin(a) * R);
                    if (i == 0) {
                        path.moveTo(ToScenePoint(p));
                    } else {
                        path.lineTo(ToScenePoint(p));
                    }
                }
            }
        }

        if (!path.isEmpty()) {
            ensureItem(polarGuideItem_, 1100.0);
            applyDashedPen(polarGuideItem_);
            polarGuideItem_->setPath(path);
            polarGuideItem_->show();
        } else if (polarGuideItem_) {
            polarGuideItem_->hide();
        }

        if (!yellowPath.isEmpty()) {
            ensureItem(polarRayItem_, 1101.0);
            QPen rayPen(QColor(255, 180, 48), 0);
            rayPen.setCosmetic(true);
            rayPen.setStyle(Qt::CustomDashLine);
            rayPen.setDashPattern({5.0, 3.5});
            rayPen.setCapStyle(Qt::FlatCap);
            polarRayItem_->setPen(rayPen);
            polarRayItem_->setBrush(Qt::NoBrush);
            polarRayItem_->setPath(yellowPath);
            polarRayItem_->show();
        } else if (polarRayItem_) {
            polarRayItem_->hide();
        }
        return;
    }

    Q_UNUSED(trackingSnapped);

    if (polarRayItem_) {
        polarRayItem_->hide();
    }

    if (length > Tolerance::Model) {
        const double dx = end.X() - base.X();
        const double dy = end.Y() - base.Y();
        // Left tracking offset ≈ AutoCAD dyninput gap (fits length HUD between
        // rubber-band and the parallel dashed ray).
        // Left tracking offset = 56px (AutoCAD fig1 gap between solid and dashed).
        const double frameW = ModelToleranceFromPixels(56.0);
        const double sweep = NormalizeAngleSigned(angleRadians);
        // Arc / 0° baseline radius == rubber-band length (AutoCAD dyninput).
        const double R = length;
        const double ux = dx / length;
        const double uy = dy / length;

        const bool nearVertical = std::abs(dx) < frameW * 0.85 && std::abs(dy) >= frameW;

        if (nearVertical) {
            // ---- 90°/270°: AutoCAD open tracking frame (left parallel + end caps) ----
            const double leftX = std::min(base.X(), end.X()) - frameW;
            const double yHi = std::max(base.Y(), end.Y());
            const double yLo = std::min(base.Y(), end.Y());
            const double xAtHi = (base.Y() >= end.Y()) ? base.X() : end.X();
            const double xAtLo = (base.Y() < end.Y()) ? base.X() : end.X();
            const double rightX = base.X() + R;

            path.moveTo(ToScenePoint(GeoPoint(leftX, yLo)));
            path.lineTo(ToScenePoint(GeoPoint(leftX, yHi)));
            path.moveTo(ToScenePoint(GeoPoint(leftX, yLo)));
            path.lineTo(ToScenePoint(GeoPoint(xAtLo, yLo)));
            path.moveTo(ToScenePoint(GeoPoint(leftX, yHi)));
            path.lineTo(ToScenePoint(GeoPoint(xAtHi, yHi)));
            // Screen-top → +X corner (meets angle arc).
            path.moveTo(ToScenePoint(GeoPoint(xAtHi, yHi)));
            path.lineTo(ToScenePoint(GeoPoint(rightX, yHi)));
            // 0° baseline from start when start is not already on the top ray.
            if (std::abs(yHi - base.Y()) > Tolerance::Model) {
                path.moveTo(ToScenePoint(base));
                path.lineTo(ToScenePoint(GeoPoint(rightX, base.Y())));
            }
        } else {
            // ---- AutoCAD fig2 (diagonal / polar DYN): ----
            // 1) Parallel dashed corridor beside the rubber-band (length HUD side)
            // 2) 0° horizontal from START (angle baseline)
            // 3) Angle arc (below)
            // NOT a collinear infinite polar ray, and NOT a ΔX/ΔY rectangle.
            double px = -uy;
            double py = ux;
            // Prefer the side toward model -X (same side as typical length HUD).
            if (px > 1e-12 || (std::abs(px) <= 1e-12 && py > 0.0)) {
                px = -px;
                py = -py;
            }
            const GeoPoint p0(base.X() + px * frameW, base.Y() + py * frameW);
            const GeoPoint p1(end.X() + px * frameW, end.Y() + py * frameW);

            // Parallel tracking line (fig2 left arrow).
            path.moveTo(ToScenePoint(p0));
            path.lineTo(ToScenePoint(p1));
            // Short caps to rubber-band ends.
            path.moveTo(ToScenePoint(p0));
            path.lineTo(ToScenePoint(base));
            path.moveTo(ToScenePoint(p1));
            path.lineTo(ToScenePoint(end));

            // 0° baseline from start along +X (fig2 top arrow).
            path.moveTo(ToScenePoint(base));
            path.lineTo(ToScenePoint(GeoPoint(base.X() + R, base.Y())));
        }

        // ---- Angle arc: radius = length, shortest signed sweep from +X ----
        if (R > Tolerance::Model
            && std::abs(sweep) > 1.0 * 3.14159265358979323846 / 180.0) {
            const int steps = std::max(36, static_cast<int>(std::abs(sweep) / 0.03));
            for (int i = 0; i <= steps; ++i) {
                const double t = static_cast<double>(i) / static_cast<double>(steps);
                const double a = sweep * t;
                const GeoPoint p(
                    base.X() + std::cos(a) * R,
                    base.Y() + std::sin(a) * R);
                if (i == 0) {
                    path.moveTo(ToScenePoint(p));
                } else {
                    path.lineTo(ToScenePoint(p));
                }
            }
        }
    }

    if (!path.isEmpty()) {
        ensureItem(polarGuideItem_, 1100.0);
        applyDashedPen(polarGuideItem_);
        polarGuideItem_->setPath(path);
        polarGuideItem_->show();
    } else if (polarGuideItem_) {
        polarGuideItem_->hide();
    }
}

void CadView::ClearPolarGuidePreview()
{
    QRectF bounds;
    auto mergeBounds = [&bounds](QGraphicsPathItem* item) {
        if (!item) {
            return;
        }
        const QRectF b = item->mapRectToScene(item->boundingRect());
        bounds = bounds.isValid() ? bounds.united(b) : b;
        if (item->scene()) {
            item->scene()->removeItem(item);
        }
        delete item;
    };
    mergeBounds(polarGuideItem_);
    polarGuideItem_ = nullptr;
    mergeBounds(polarRayItem_);
    polarRayItem_ = nullptr;
    mergeBounds(polarTrackingItem_);
    polarTrackingItem_ = nullptr;
    if (scene() && bounds.isValid()) {
        const double margin = ModelToleranceFromPixels(8.0);
        scene()->invalidate(bounds.adjusted(-margin, -margin, margin, margin), QGraphicsScene::AllLayers);
    }
}

void CadView::CommitDynamicLinePoint()
{
    if (IsPolylineOptionEditing()) {
        optionInput_->ConfirmInput();
        return;
    }
    if (IsSplineGripEditing()) {
        const GeoPoint cursor = ApplySnap(MapToModel(lastCursorViewportPos_));
        CommitSplineGripEdit(ResolveDynamicLineEndpoint(gripStartPoint_, cursor));
        if (polarInput_) {
            polarInput_->ResetLocks();
        }
        setFocus(Qt::OtherFocusReason);
        return;
    }
    GeoPoint polarBase;
    if (!TryGetPolarBasePoint(&polarBase)) {
        return;
    }
    const GeoPoint cursor = ApplySnap(MapToModel(lastCursorViewportPos_));
    const GeoPoint end = ResolveDynamicLineEndpoint(polarBase, cursor);
    HandleLeftPress(end, Qt::NoModifier);
    if (polarInput_) {
        polarInput_->ResetLocks();
        polarInput_->FocusLength();
    }
    // Enter 确认后立即刷新到“下一步”预览与 DYN，避免停留在上一步画面。
    if (viewport()) {
        const GeoPoint current = ApplySnap(MapToModel(lastCursorViewportPos_));
        GeoPoint nextBase;
        if (TryGetPolarBasePoint(&nextBase)) {
            UpdatePolarInput(lastCursorViewportPos_, current);
        }
        UpdatePreview(current);
        RefreshViewport();
    }
    setFocus(Qt::OtherFocusReason);
}

void CadView::UpdateMirrorPreview(const GeoPoint& point)
{
    if (!scene() || !document_ || mirrorTargetIds_.empty()) {
        ClearPreview();
        return;
    }

    QPainterPath path;
    path.moveTo(ToScenePoint(mirrorAxisStart_));
    path.lineTo(ToScenePoint(point));

    for (EntityId id : mirrorTargetIds_) {
        const Entity* entity = document_->FindEntity(id);
        if (!entity) {
            continue;
        }
        std::unique_ptr<Entity> preview = entity->Clone();
        preview->Mirror(mirrorAxisStart_, point);
        const CurveDisplayOptions display = MakeCurveDisplayOptions();
        path.addPath(GeoShapePainter::BuildPath(preview->Shaper(), nullptr, &display));
    }
    SetPreviewPath(path);
}

void CadView::UpdateArrayPreview(const GeoPoint& point)
{
    if (!document_ || arraySourceIds_.empty()) {
        ClearPreview();
        return;
    }

    std::vector<std::unique_ptr<Entity>> copies;
    if (toolMode_ == ToolMode::ArrayPolar) {
        const GeoPoint center = arrayCenter_.value_or(point);
        copies = EditKernel::ClonePolarArray(
            *document_, arraySourceIds_, center, polarArrayOptions_);
    } else if (toolMode_ == ToolMode::ArrayPath) {
        const EntityId pathId = HitTest(point);
        const Entity* path = pathId == 0 ? nullptr : document_->FindEntity(pathId);
        if (!path || !EditKernel::IsSupportedArrayPath(*path)
            || std::find(arraySourceIds_.begin(), arraySourceIds_.end(), pathId) != arraySourceIds_.end()) {
            ClearPreview();
            return;
        }
        copies = EditKernel::ClonePathArray(
            *document_, arraySourceIds_, pathId, pathArrayOptions_);
    }

    GeoShape previewShape;
    for (const auto& copy : copies) {
        if (copy) {
            previewShape.Append(copy->Shaper());
        }
    }
    const CurveDisplayOptions display = MakeCurveDisplayOptions();
    SetPreviewPath(GeoShapePainter::BuildPath(previewShape, nullptr, &display));
}

} // namespace cad
