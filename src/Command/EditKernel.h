#pragma once

#include "Base/CadTypes.h"
#include "Geometry/GeoFilletChamfer.h"
#include "Geometry/GeoLine.h"
#include "Geometry/GeoPoint.h"
#include "Geometry/ClipperUtil.h"
#include <memory>
#include <optional>
#include <vector>

namespace cad {

class CadDocument;
class Entity;

struct CornerEditOutput {
    std::vector<EntityId> removeIds;
    std::vector<std::unique_ptr<Entity>> created;
};

enum class CornerEditError {
    None,
    NoSolution,
    RadiusTooLarge,
    DistanceTooLarge,
};

struct RectangularArrayOptions {
    int rows = 2;
    int columns = 3;
    double rowSpacing = 50.0;
    double columnSpacing = 50.0;
    double angleDegrees = 0.0;
};

struct PolarArrayOptions {
    int itemCount = 6;
    double fillAngleDegrees = 360.0;
    bool rotateItems = true;
    std::optional<GeoPoint> basePoint;
};

enum class PathArrayMethod {
    Divide,
    Measure,
};

struct PathArrayOptions {
    PathArrayMethod method = PathArrayMethod::Divide;
    int itemCount = 6;
    double spacing = 50.0;
    bool alignItems = true;
};

class EditKernel {
public:
    static std::vector<EntityId> MirrorEntities(CadDocument& document, const std::vector<EntityId>& ids, const GeoPoint& axisStart, const GeoPoint& axisEnd);
    static std::vector<std::unique_ptr<Entity>> CloneMirroredEntities(
        const CadDocument& document,
        const std::vector<EntityId>& ids,
        const GeoPoint& axisStart,
        const GeoPoint& axisEnd);
    static std::vector<EntityId> RotateEntities(CadDocument& document, const std::vector<EntityId>& ids, const GeoPoint& center, double angleRadians);
    static std::vector<EntityId> ScaleEntities(CadDocument& document, const std::vector<EntityId>& ids, const GeoPoint& center, double factor);
    static std::vector<std::unique_ptr<Entity>> CloneRectangularArray(
        const CadDocument& document,
        const std::vector<EntityId>& sourceIds,
        const RectangularArrayOptions& options);
    static std::vector<std::unique_ptr<Entity>> ClonePolarArray(
        const CadDocument& document,
        const std::vector<EntityId>& sourceIds,
        const GeoPoint& center,
        const PolarArrayOptions& options);
    static std::vector<std::unique_ptr<Entity>> ClonePathArray(
        const CadDocument& document,
        const std::vector<EntityId>& sourceIds,
        EntityId pathId,
        const PathArrayOptions& options);
    static bool IsSupportedArrayPath(const Entity& entity);
    static std::optional<GeoLine> EntityAsCutLine(const Entity& entity, const GeoPoint& nearPoint);
    static double ComputeOffsetDistance(const GeoPoint& from, const GeoPoint& to, const GeoRegion& region);
    static bool TrimLineEntity(CadDocument& document, EntityId targetId, EntityId cutterId, const GeoPoint& pickPoint);
    static bool ExtendLineEntity(CadDocument& document, EntityId targetId, EntityId boundaryId, const GeoPoint& pickPoint);
    static std::vector<std::unique_ptr<Entity>> OffsetSelectedEntities(
        CadDocument& document,
        const std::vector<EntityId>& ids,
        const GeoPoint& fromPoint,
        const GeoPoint& toPoint);
    static std::vector<EntityId> OffsetSelectedRegions(CadDocument& document, const std::vector<EntityId>& ids, double delta);
    static std::vector<EntityId> BooleanSelected(CadDocument& document, const std::vector<EntityId>& subjectIds, const std::vector<EntityId>& clipIds, BooleanOp op);
    static std::vector<std::unique_ptr<Entity>> BuildHatchEntities(const CadDocument& document, EntityId regionId, double spacing);
    static std::vector<std::unique_ptr<Entity>> BreakEntity(
        const CadDocument& document,
        EntityId targetId,
        const GeoPoint& firstPoint,
        const std::optional<GeoPoint>& secondPoint);
    static std::optional<GeoLine> PickedLinearEdge(const Entity& entity, const GeoPoint& pick);
    static std::optional<CornerEditOutput> FilletEntities(
        const CadDocument& document,
        EntityId id1,
        EntityId id2,
        const FilletOptions& options,
        const GeoPoint& pick1,
        const GeoPoint& pick2,
        CornerEditError* error = nullptr);
    static std::optional<CornerEditOutput> ChamferEntities(
        const CadDocument& document,
        EntityId id1,
        EntityId id2,
        const ChamferOptions& options,
        const GeoPoint& pick1,
        const GeoPoint& pick2,
        CornerEditError* error = nullptr);
    static std::optional<CornerEditOutput> FilletPolyline(
        const CadDocument& document,
        EntityId id,
        const FilletOptions& options,
        CornerEditError* error = nullptr);
    static std::optional<CornerEditOutput> ChamferPolyline(
        const CadDocument& document,
        EntityId id,
        const ChamferOptions& options,
        CornerEditError* error = nullptr);
    static bool ApplyGripEdit(CadDocument& document, EntityId id, int gripIndex, const GeoPoint& position);

    static std::vector<GeoRegion> CollectRegions(const CadDocument& document, const std::vector<EntityId>& ids);
};

} // namespace cad
