#pragma once

#include "DataModel/CadDocument.h"
#include "DataModel/BlockDefinition.h"
#include "Command/EditKernel.h"
#include "Geometry/GeoFilletChamfer.h"
#include "Geometry/GeoPoint.h"
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace cad {

enum class CornerEditError;

class ICommand {
public:
    virtual ~ICommand() = default;
    virtual void Execute(CadDocument& document) = 0;
    virtual void Undo(CadDocument& document) = 0;
};

class CommandManager {
public:
    explicit CommandManager(CadDocument& document);

    CadDocument& Document() { return document_; }

    void Execute(std::unique_ptr<ICommand> command);
    void AddEntity(std::unique_ptr<Entity> entity);
    void AddEntities(std::vector<std::unique_ptr<Entity>> entities);
    void DeleteSelected();
    void MoveSelected(double dx, double dy);
    std::vector<EntityId> MirrorSelected(
        const std::vector<EntityId>& ids,
        const GeoPoint& axisStart,
        const GeoPoint& axisEnd,
        bool eraseSource);
    void RotateSelected(const GeoPoint& center, double angleRadians);
    void ScaleSelected(const GeoPoint& center, double factor);
    bool CreateRectangularArray(
        const std::vector<EntityId>& sourceIds,
        const RectangularArrayOptions& options);
    bool CreatePolarArray(
        const std::vector<EntityId>& sourceIds,
        const GeoPoint& center,
        const PolarArrayOptions& options);
    bool CreatePathArray(
        const std::vector<EntityId>& sourceIds,
        EntityId pathId,
        const PathArrayOptions& options);
    bool TrimSelected(EntityId cutterId, const GeoPoint& pickPoint);
    bool ExtendSelected(EntityId boundaryId, const GeoPoint& pickPoint);
    bool BreakSelected(EntityId targetId, const GeoPoint& firstPoint, const std::optional<GeoPoint>& secondPoint);
    bool FilletSelected(
        EntityId id1,
        EntityId id2,
        const FilletOptions& options,
        const GeoPoint& pick1,
        const GeoPoint& pick2,
        CornerEditError* error = nullptr);
    bool ChamferSelected(
        EntityId id1,
        EntityId id2,
        const ChamferOptions& options,
        const GeoPoint& pick1,
        const GeoPoint& pick2,
        CornerEditError* error = nullptr);
    bool FilletPolyline(EntityId id, const FilletOptions& options, CornerEditError* error = nullptr);
    bool ChamferPolyline(EntityId id, const ChamferOptions& options, CornerEditError* error = nullptr);
    bool OffsetSelected(const GeoPoint& fromPoint, const GeoPoint& toPoint);
    bool BooleanUnionSelected();
    bool BooleanSubtractSelected();
    bool HatchSelected(double spacing);
    void ReplaceEntity(EntityId id, std::unique_ptr<Entity> replacement);
    void ApplyGripEdit(EntityId id, int gripIndex, const GeoPoint& position);
    void ApplyEntityStyle(const std::vector<EntityId>& ids, const EntityStyle& style);
    void MutateEntityStyles(const std::vector<EntityId>& ids, std::function<void(EntityStyle&)> mutator);
    void MatchEntityProperties(EntityId sourceId, const std::vector<EntityId>& targetIds);
    void Undo();
    void Redo();
    bool CanUndo() const { return !undoStack_.empty(); }
    bool CanRedo() const { return !redoStack_.empty(); }

    void CreateDemoGeometry();

    bool CreateBlock(
        const std::string& name,
        double baseX,
        double baseY,
        const std::vector<EntityId>& entityIds,
        BlockCreateMode mode,
        bool allowExplode = true,
        bool uniformScale = true,
        bool annotative = false,
        std::vector<BlockAttributeDef> attributeDefs = {});
    EntityId InsertBlock(
        const std::string& blockName,
        const GeoPoint& insertion,
        double scale = 1.0,
        double rotationRadians = 0.0,
        bool explode = false,
        std::vector<BlockAttributeValue> attributes = {});
    int ExplodeSelectedBlocks();

private:
    CadDocument& document_;
    std::vector<std::unique_ptr<ICommand>> undoStack_;
    std::vector<std::unique_ptr<ICommand>> redoStack_;
};

} // namespace cad
