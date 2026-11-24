#include "Command/CommandManager.h"

#include "Base/IdGenerator.h"
#include "Command/EditKernel.h"
#include "DataModel/ArcEntity.h"
#include "DataModel/BlockReferenceEntity.h"
#include "DataModel/DynamicBlockEvaluator.h"
#include "DataModel/CircleEntity.h"
#include "DataModel/LineEntity.h"
#include "DataModel/PolylineEntity.h"
#include "DataModel/RegionEntity.h"
#include "Geometry/ClipperUtil.h"
#include "Geometry/GeoCircle.h"
#include "Geometry/GeoLine.h"
#include "Geometry/GeoRegion.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>
#include <optional>
#include <utility>

namespace cad {

namespace {

class AddEntityCommand : public ICommand {
public:
    explicit AddEntityCommand(std::unique_ptr<Entity> entity)
        : entity_(std::move(entity))
    {
    }

    void Execute(CadDocument& document) override
    {
        if (entity_) {
            entityId_ = entity_->Id();
            document.AddEntity(std::move(entity_));
        }
    }

    void Undo(CadDocument& document) override
    {
        entity_ = document.TakeEntity(entityId_);
        document.SyncSelectionView();
    }

private:
    EntityId entityId_ = 0;
    std::unique_ptr<Entity> entity_;
};

class DeleteEntitiesCommand : public ICommand {
public:
    explicit DeleteEntitiesCommand(std::vector<EntityId> ids)
        : ids_(std::move(ids))
    {
    }

    void Execute(CadDocument& document) override
    {
        deleted_ = document.TakeEntities(ids_);
    }

    void Undo(CadDocument& document) override
    {
        for (auto& entity : deleted_) {
            document.AddEntity(std::move(entity));
        }
        deleted_.clear();
        document.SyncSelectionView();
    }

private:
    std::vector<EntityId> ids_;
    std::vector<std::unique_ptr<Entity>> deleted_;
};

class MoveEntitiesCommand : public ICommand {
public:
    MoveEntitiesCommand(std::vector<EntityId> ids, double dx, double dy)
        : ids_(std::move(ids))
        , dx_(dx)
        , dy_(dy)
    {
    }

    void Execute(CadDocument& document) override
    {
        document.MoveEntities(ids_, dx_, dy_);
    }

    void Undo(CadDocument& document) override
    {
        document.MoveEntities(ids_, -dx_, -dy_);
    }

private:
    std::vector<EntityId> ids_;
    double dx_ = 0.0;
    double dy_ = 0.0;
};

class SnapshotEditCommand : public ICommand {
public:
    SnapshotEditCommand(std::vector<EntityId> ids, std::function<void(CadDocument&)> apply)
        : ids_(std::move(ids))
        , apply_(std::move(apply))
    {
    }

    void Execute(CadDocument& document) override
    {
        if (before_.empty()) {
            Capture(document);
        }
        apply_(document);
    }

    void Undo(CadDocument& document) override
    {
        Restore(document);
        document.SyncSelectionView();
    }

private:
    void Capture(CadDocument& document)
    {
        for (EntityId id : ids_) {
            if (const Entity* entity = document.FindEntity(id)) {
                before_.push_back(entity->Clone());
            }
        }
    }

    void Restore(CadDocument& document)
    {
        for (const auto& snapshot : before_) {
            if (!snapshot) {
                continue;
            }
            document.TakeEntity(snapshot->Id());
            document.AddEntity(snapshot->Clone());
        }
    }

    std::vector<EntityId> ids_;
    std::function<void(CadDocument&)> apply_;
    std::vector<std::unique_ptr<Entity>> before_;
};

class MirrorEntitiesCommand : public ICommand {
public:
    MirrorEntitiesCommand(std::vector<EntityId> sourceIds, GeoPoint axisStart, GeoPoint axisEnd, bool eraseSource)
        : sourceIds_(std::move(sourceIds))
        , axisStart_(axisStart)
        , axisEnd_(axisEnd)
        , eraseSource_(eraseSource)
    {
    }

    const std::vector<EntityId>& ResultIds() const { return resultIds_; }

    void Execute(CadDocument& document) override
    {
        if (eraseSource_) {
            if (before_.empty()) {
                for (EntityId id : sourceIds_) {
                    if (const Entity* entity = document.FindEntity(id)) {
                        before_.push_back(entity->Clone());
                    }
                }
            }
            EditKernel::MirrorEntities(document, sourceIds_, axisStart_, axisEnd_);
            resultIds_ = sourceIds_;
            return;
        }

        if (mirroredCopies_.empty()) {
            mirroredCopies_ = EditKernel::CloneMirroredEntities(document, sourceIds_, axisStart_, axisEnd_);
            resultIds_.clear();
            for (const auto& entity : mirroredCopies_) {
                resultIds_.push_back(entity->Id());
            }
        }
        for (auto& entity : mirroredCopies_) {
            if (!document.FindEntity(entity->Id())) {
                document.AddEntity(entity->Clone());
            }
        }
    }

    void Undo(CadDocument& document) override
    {
        if (eraseSource_) {
            for (const auto& snapshot : before_) {
                if (!snapshot) {
                    continue;
                }
                document.TakeEntity(snapshot->Id());
                document.AddEntity(snapshot->Clone());
            }
        } else {
            for (EntityId id : resultIds_) {
                if (std::unique_ptr<Entity> entity = document.TakeEntity(id)) {
                    for (auto& stored : mirroredCopies_) {
                        if (stored && stored->Id() == id) {
                            stored = std::move(entity);
                            break;
                        }
                    }
                }
            }
        }
        document.SyncSelectionView();
    }

private:
    std::vector<EntityId> sourceIds_;
    GeoPoint axisStart_;
    GeoPoint axisEnd_;
    bool eraseSource_ = false;
    std::vector<EntityId> resultIds_;
    std::vector<std::unique_ptr<Entity>> before_;
    std::vector<std::unique_ptr<Entity>> mirroredCopies_;
};

class AddEntitiesCommand : public ICommand {
public:
    explicit AddEntitiesCommand(std::vector<std::unique_ptr<Entity>> entities)
        : entities_(std::move(entities))
    {
    }

    void Execute(CadDocument& document) override
    {
        if (executed_) {
            for (EntityId id : createdIds_) {
                if (std::unique_ptr<Entity> entity = document.TakeEntity(id)) {
                    entities_.push_back(std::move(entity));
                }
            }
            createdIds_.clear();
        }
        for (auto& entity : entities_) {
            createdIds_.push_back(entity->Id());
            document.AddEntity(std::move(entity));
        }
        entities_.clear();
        executed_ = true;
    }

    void Undo(CadDocument& document) override
    {
        entities_.clear();
        for (EntityId id : createdIds_) {
            if (std::unique_ptr<Entity> entity = document.TakeEntity(id)) {
                entities_.push_back(std::move(entity));
            }
        }
        createdIds_.clear();
        document.SyncSelectionView();
    }

private:
    std::vector<std::unique_ptr<Entity>> entities_;
    std::vector<EntityId> createdIds_;
    bool executed_ = false;
};

class BreakEntityCommand : public ICommand {
public:
    BreakEntityCommand(EntityId sourceId, std::unique_ptr<Entity> original, std::vector<std::unique_ptr<Entity>> replacements)
        : sourceId_(sourceId)
        , original_(std::move(original))
        , replacements_(std::move(replacements))
    {
    }

    void Execute(CadDocument& document) override
    {
        if (document.FindEntity(sourceId_)) {
            document.TakeEntity(sourceId_);
        }
        createdIds_.clear();
        for (const auto& replacement : replacements_) {
            if (!replacement) {
                continue;
            }
            std::unique_ptr<Entity> entity = replacement->Clone();
            createdIds_.push_back(entity->Id());
            document.AddEntity(std::move(entity));
        }
        document.SyncSelectionView();
    }

    void Undo(CadDocument& document) override
    {
        for (EntityId id : createdIds_) {
            document.TakeEntity(id);
        }
        createdIds_.clear();
        if (original_) {
            document.AddEntity(original_->Clone());
        }
        document.SyncSelectionView();
    }

private:
    EntityId sourceId_;
    std::unique_ptr<Entity> original_;
    std::vector<std::unique_ptr<Entity>> replacements_;
    std::vector<EntityId> createdIds_;
};

class ReplaceEntitiesCommand : public ICommand {
public:
    ReplaceEntitiesCommand(
        std::vector<EntityId> sourceIds,
        std::vector<std::unique_ptr<Entity>> originals,
        std::vector<std::unique_ptr<Entity>> replacements)
        : sourceIds_(std::move(sourceIds))
        , originals_(std::move(originals))
        , replacements_(std::move(replacements))
    {
    }

    void Execute(CadDocument& document) override
    {
        for (EntityId id : sourceIds_) {
            document.TakeEntity(id);
        }
        createdIds_.clear();
        for (const auto& replacement : replacements_) {
            if (!replacement) {
                continue;
            }
            std::unique_ptr<Entity> entity = replacement->Clone();
            createdIds_.push_back(entity->Id());
            document.AddEntity(std::move(entity));
        }
        document.SyncSelectionView();
    }

    void Undo(CadDocument& document) override
    {
        for (EntityId id : createdIds_) {
            document.TakeEntity(id);
        }
        createdIds_.clear();
        for (const auto& original : originals_) {
            if (original) {
                document.AddEntity(original->Clone());
            }
        }
        document.SyncSelectionView();
    }

private:
    std::vector<EntityId> sourceIds_;
    std::vector<std::unique_ptr<Entity>> originals_;
    std::vector<std::unique_ptr<Entity>> replacements_;
    std::vector<EntityId> createdIds_;
};

class ReplaceEntityCommand : public ICommand {
public:
    ReplaceEntityCommand(EntityId id, std::unique_ptr<Entity> original, std::unique_ptr<Entity> replacement)
        : id_(id), original_(std::move(original)), replacement_(std::move(replacement)) {}

    void Execute(CadDocument& document) override
    {
        document.TakeEntity(id_);
        document.AddEntity(replacement_->Clone());
    }

    void Undo(CadDocument& document) override
    {
        document.TakeEntity(id_);
        document.AddEntity(original_->Clone());
        document.SyncSelectionView();
    }

private:
    EntityId id_;
    std::unique_ptr<Entity> original_;
    std::unique_ptr<Entity> replacement_;
};

} // namespace

CommandManager::CommandManager(CadDocument& document)
    : document_(document)
{
}

void CommandManager::Execute(std::unique_ptr<ICommand> command)
{
    if (!command) {
        return;
    }
    command->Execute(document_);
    undoStack_.push_back(std::move(command));
    redoStack_.clear();
}

void CommandManager::AddEntity(std::unique_ptr<Entity> entity)
{
    if (entity) {
        entity->SetLayerName(document_.CurrentLayerName());
        entity->SetStyle(document_.CurrentEntityStyle());
    }
    Execute(std::make_unique<AddEntityCommand>(std::move(entity)));
}

void CommandManager::AddEntities(std::vector<std::unique_ptr<Entity>> entities)
{
    if (entities.empty()) {
        return;
    }
    Execute(std::make_unique<AddEntitiesCommand>(std::move(entities)));
}

void CommandManager::DeleteSelected()
{
    std::vector<EntityId> selected = document_.EditableEntityIds(document_.SelectedEntityIds());
    if (!selected.empty()) {
        Execute(std::make_unique<DeleteEntitiesCommand>(std::move(selected)));
    }
}

void CommandManager::MoveSelected(double dx, double dy)
{
    std::vector<EntityId> selected = document_.EditableEntityIds(document_.SelectedEntityIds());
    if (!selected.empty()) {
        Execute(std::make_unique<MoveEntitiesCommand>(std::move(selected), dx, dy));
    }
}

std::vector<EntityId> CommandManager::MirrorSelected(
    const std::vector<EntityId>& ids,
    const GeoPoint& axisStart,
    const GeoPoint& axisEnd,
    bool eraseSource)
{
    if (ids.empty()) {
        return {};
    }
    const std::vector<EntityId> editable = document_.EditableEntityIds(ids);
    if (editable.empty()) {
        return {};
    }
    auto command = std::make_unique<MirrorEntitiesCommand>(editable, axisStart, axisEnd, eraseSource);
    command->Execute(document_);
    const std::vector<EntityId> resultIds = command->ResultIds();
    undoStack_.push_back(std::move(command));
    redoStack_.clear();
    return resultIds;
}

void CommandManager::RotateSelected(const GeoPoint& center, double angleRadians)
{
    std::vector<EntityId> selected = document_.EditableEntityIds(document_.SelectedEntityIds());
    if (selected.empty()) {
        return;
    }
    Execute(std::make_unique<SnapshotEditCommand>(selected, [=](CadDocument& document) {
        EditKernel::RotateEntities(document, selected, center, angleRadians);
    }));
}

void CommandManager::ScaleSelected(const GeoPoint& center, double factor)
{
    std::vector<EntityId> selected = document_.EditableEntityIds(document_.SelectedEntityIds());
    if (selected.empty()) {
        return;
    }
    Execute(std::make_unique<SnapshotEditCommand>(selected, [=](CadDocument& document) {
        EditKernel::ScaleEntities(document, selected, center, factor);
    }));
}

bool CommandManager::CreateRectangularArray(
    const std::vector<EntityId>& sourceIds,
    const RectangularArrayOptions& options)
{
    const std::vector<EntityId> editable = document_.EditableEntityIds(sourceIds);
    if (editable.empty()) {
        return false;
    }
    std::vector<std::unique_ptr<Entity>> copies =
        EditKernel::CloneRectangularArray(document_, editable, options);
    if (copies.empty()) {
        return false;
    }
    AddEntities(std::move(copies));
    document_.ClearSelection();
    return true;
}

bool CommandManager::CreatePolarArray(
    const std::vector<EntityId>& sourceIds,
    const GeoPoint& center,
    const PolarArrayOptions& options)
{
    const std::vector<EntityId> editable = document_.EditableEntityIds(sourceIds);
    if (editable.empty()) {
        return false;
    }
    std::vector<std::unique_ptr<Entity>> copies =
        EditKernel::ClonePolarArray(document_, editable, center, options);
    if (copies.empty()) {
        return false;
    }
    AddEntities(std::move(copies));
    document_.ClearSelection();
    return true;
}

bool CommandManager::CreatePathArray(
    const std::vector<EntityId>& sourceIds,
    EntityId pathId,
    const PathArrayOptions& options)
{
    const std::vector<EntityId> editable = document_.EditableEntityIds(sourceIds);
    if (editable.empty()
        || std::find(editable.begin(), editable.end(), pathId) != editable.end()) {
        return false;
    }
    std::vector<std::unique_ptr<Entity>> copies =
        EditKernel::ClonePathArray(document_, editable, pathId, options);
    if (copies.empty()) {
        return false;
    }
    AddEntities(std::move(copies));
    document_.ClearSelection();
    return true;
}

bool CommandManager::TrimSelected(EntityId cutterId, const GeoPoint& pickPoint)
{
    for (EntityId targetId : document_.EditableEntityIds(document_.SelectedEntityIds())) {
        const EntityId id = targetId;
        auto success = std::make_shared<bool>(false);
        Execute(std::make_unique<SnapshotEditCommand>(std::vector<EntityId>{id}, [=](CadDocument& document) {
            *success = EditKernel::TrimLineEntity(document, id, cutterId, pickPoint);
        }));
        return *success;
    }
    return false;
}

bool CommandManager::ExtendSelected(EntityId boundaryId, const GeoPoint& pickPoint)
{
    for (EntityId targetId : document_.EditableEntityIds(document_.SelectedEntityIds())) {
        const EntityId id = targetId;
        auto success = std::make_shared<bool>(false);
        Execute(std::make_unique<SnapshotEditCommand>(std::vector<EntityId>{id}, [=](CadDocument& document) {
            *success = EditKernel::ExtendLineEntity(document, id, boundaryId, pickPoint);
        }));
        return *success;
    }
    return false;
}

bool CommandManager::BreakSelected(
    EntityId targetId,
    const GeoPoint& firstPoint,
    const std::optional<GeoPoint>& secondPoint)
{
    if (!document_.IsEntityEditable(targetId)) {
        return false;
    }
    const Entity* entity = document_.FindEntity(targetId);
    if (!entity) {
        return false;
    }

    std::vector<std::unique_ptr<Entity>> replacements =
        EditKernel::BreakEntity(document_, targetId, firstPoint, secondPoint);
    if (replacements.empty()) {
        return false;
    }

    std::unique_ptr<Entity> original = entity->Clone();
    Execute(std::make_unique<BreakEntityCommand>(targetId, std::move(original), std::move(replacements)));
    return true;
}

bool CommandManager::FilletSelected(
    EntityId id1,
    EntityId id2,
    const FilletOptions& options,
    const GeoPoint& pick1,
    const GeoPoint& pick2,
    CornerEditError* error)
{
    if (error) {
        *error = CornerEditError::NoSolution;
    }
    if (!document_.IsEntityEditable(id1) || !document_.IsEntityEditable(id2)) {
        return false;
    }
    std::optional<CornerEditOutput> output =
        EditKernel::FilletEntities(document_, id1, id2, options, pick1, pick2, error);
    if (!output || output->created.empty()) {
        return false;
    }

    std::vector<std::unique_ptr<Entity>> originals;
    for (EntityId id : output->removeIds) {
        const Entity* entity = document_.FindEntity(id);
        if (entity) {
            originals.push_back(entity->Clone());
        }
    }
    Execute(std::make_unique<ReplaceEntitiesCommand>(
        std::move(output->removeIds),
        std::move(originals),
        std::move(output->created)));
    return true;
}

bool CommandManager::ChamferSelected(
    EntityId id1,
    EntityId id2,
    const ChamferOptions& options,
    const GeoPoint& pick1,
    const GeoPoint& pick2,
    CornerEditError* error)
{
    if (error) {
        *error = CornerEditError::NoSolution;
    }
    if (!document_.IsEntityEditable(id1) || !document_.IsEntityEditable(id2)) {
        return false;
    }
    std::optional<CornerEditOutput> output =
        EditKernel::ChamferEntities(document_, id1, id2, options, pick1, pick2, error);
    if (!output || output->created.empty()) {
        return false;
    }

    std::vector<std::unique_ptr<Entity>> originals;
    for (EntityId id : output->removeIds) {
        const Entity* entity = document_.FindEntity(id);
        if (entity) {
            originals.push_back(entity->Clone());
        }
    }
    Execute(std::make_unique<ReplaceEntitiesCommand>(
        std::move(output->removeIds),
        std::move(originals),
        std::move(output->created)));
    return true;
}

bool CommandManager::FilletPolyline(EntityId id, const FilletOptions& options, CornerEditError* error)
{
    if (error) {
        *error = CornerEditError::NoSolution;
    }
    if (!document_.IsEntityEditable(id)) {
        return false;
    }
    std::optional<CornerEditOutput> output = EditKernel::FilletPolyline(document_, id, options, error);
    if (!output || output->created.empty()) {
        return false;
    }
    std::vector<std::unique_ptr<Entity>> originals;
    for (EntityId removeId : output->removeIds) {
        const Entity* entity = document_.FindEntity(removeId);
        if (entity) {
            originals.push_back(entity->Clone());
        }
    }
    Execute(std::make_unique<ReplaceEntitiesCommand>(
        std::move(output->removeIds),
        std::move(originals),
        std::move(output->created)));
    return true;
}

bool CommandManager::ChamferPolyline(EntityId id, const ChamferOptions& options, CornerEditError* error)
{
    if (error) {
        *error = CornerEditError::NoSolution;
    }
    if (!document_.IsEntityEditable(id)) {
        return false;
    }
    std::optional<CornerEditOutput> output = EditKernel::ChamferPolyline(document_, id, options, error);
    if (!output || output->created.empty()) {
        return false;
    }
    std::vector<std::unique_ptr<Entity>> originals;
    for (EntityId removeId : output->removeIds) {
        const Entity* entity = document_.FindEntity(removeId);
        if (entity) {
            originals.push_back(entity->Clone());
        }
    }
    Execute(std::make_unique<ReplaceEntitiesCommand>(
        std::move(output->removeIds),
        std::move(originals),
        std::move(output->created)));
    return true;
}

bool CommandManager::OffsetSelected(const GeoPoint& fromPoint, const GeoPoint& toPoint)
{
    const std::vector<EntityId> selected = document_.EditableEntityIds(document_.SelectedEntityIds());
    if (selected.empty()) {
        return false;
    }

    std::vector<std::unique_ptr<Entity>> created =
        EditKernel::OffsetSelectedEntities(document_, selected, fromPoint, toPoint);
    if (created.empty()) {
        return false;
    }
    Execute(std::make_unique<AddEntitiesCommand>(std::move(created)));
    return true;
}

bool CommandManager::BooleanUnionSelected()
{
    const std::vector<EntityId> selected = document_.EditableEntityIds(document_.SelectedEntityIds());
    if (selected.size() < 2) {
        return false;
    }
    const EntityId first = selected.front();
    const std::vector<EntityId> clips(selected.begin() + 1, selected.end());

    const std::vector<GeoRegion> subjects = EditKernel::CollectRegions(document_, {first});
    const std::vector<GeoRegion> clipsRegions = EditKernel::CollectRegions(document_, clips);
    if (subjects.empty() || clipsRegions.empty()) {
        return false;
    }

    std::vector<std::unique_ptr<Entity>> createdEntities;
    for (const GeoRegion& region : ClipperUtil::BooleanOperation(
             BooleanOp::Union,
             subjects,
             clipsRegions)) {
        createdEntities.push_back(std::make_unique<RegionEntity>(IdGenerator::Next(), region, true));
    }
    if (createdEntities.empty()) {
        return false;
    }
    Execute(std::make_unique<AddEntitiesCommand>(std::move(createdEntities)));
    return true;
}

bool CommandManager::BooleanSubtractSelected()
{
    const std::vector<EntityId> selected = document_.EditableEntityIds(document_.SelectedEntityIds());
    if (selected.size() < 2) {
        return false;
    }
    const EntityId first = selected.front();
    const std::vector<EntityId> clips(selected.begin() + 1, selected.end());

    const std::vector<GeoRegion> subjects = EditKernel::CollectRegions(document_, {first});
    const std::vector<GeoRegion> clipsRegions = EditKernel::CollectRegions(document_, clips);
    if (subjects.empty() || clipsRegions.empty()) {
        return false;
    }

    std::vector<std::unique_ptr<Entity>> createdEntities;
    for (const GeoRegion& region : ClipperUtil::BooleanOperation(
             BooleanOp::Difference,
             subjects,
             clipsRegions)) {
        createdEntities.push_back(std::make_unique<RegionEntity>(IdGenerator::Next(), region, true));
    }
    if (createdEntities.empty()) {
        return false;
    }
    Execute(std::make_unique<AddEntitiesCommand>(std::move(createdEntities)));
    return true;
}

bool CommandManager::HatchSelected(double spacing)
{
    const std::vector<EntityId> selected = document_.EditableEntityIds(document_.SelectedEntityIds());
    if (selected.empty()) {
        return false;
    }

    std::vector<std::unique_ptr<Entity>> createdEntities;
    for (EntityId id : selected) {
        std::vector<std::unique_ptr<Entity>> batch = EditKernel::BuildHatchEntities(document_, id, spacing);
        for (std::unique_ptr<Entity>& entity : batch) {
            createdEntities.push_back(std::move(entity));
        }
    }
    if (createdEntities.empty()) {
        return false;
    }
    Execute(std::make_unique<AddEntitiesCommand>(std::move(createdEntities)));
    return true;
}

void CommandManager::ApplyGripEdit(EntityId id, int gripIndex, const GeoPoint& position)
{
    if (!document_.IsEntityEditable(id)) {
        return;
    }
    Execute(std::make_unique<SnapshotEditCommand>(std::vector<EntityId>{id}, [=](CadDocument& document) {
        EditKernel::ApplyGripEdit(document, id, gripIndex, position);
    }));
}

void CommandManager::ApplyEntityStyle(const std::vector<EntityId>& ids, const EntityStyle& style)
{
    const std::vector<EntityId> editable = document_.EditableEntityIds(ids);
    if (editable.empty()) {
        return;
    }
    Execute(std::make_unique<SnapshotEditCommand>(editable, [style, editable](CadDocument& document) {
        document.SetEntitiesStyle(editable, style);
    }));
}

void CommandManager::MutateEntityStyles(
    const std::vector<EntityId>& ids,
    std::function<void(EntityStyle&)> mutator)
{
    const std::vector<EntityId> editable = document_.EditableEntityIds(ids);
    if (editable.empty() || !mutator) {
        return;
    }
    Execute(std::make_unique<SnapshotEditCommand>(editable, [editable, mutator](CadDocument& document) {
        std::vector<EntityId> changed;
        for (EntityId id : editable) {
            Entity* entity = document.FindEntity(id);
            if (!entity) {
                continue;
            }
            EntityStyle style = entity->Style();
            mutator(style);
            entity->SetStyle(style);
            changed.push_back(id);
        }
        if (!changed.empty()) {
            document.NotifyEntitiesUpdated(changed);
        }
    }));
}

void CommandManager::MatchEntityProperties(EntityId sourceId, const std::vector<EntityId>& targetIds)
{
    const std::vector<EntityId> editable = document_.EditableEntityIds(targetIds);
    if (!document_.FindEntity(sourceId) || editable.empty()) {
        return;
    }
    Execute(std::make_unique<SnapshotEditCommand>(editable, [sourceId, editable](CadDocument& document) {
        document.MatchEntitiesProperties(sourceId, editable);
    }));
}

void CommandManager::ReplaceEntity(EntityId id, std::unique_ptr<Entity> replacement)
{
    const Entity* existing = document_.FindEntity(id);
    if (!existing || !replacement || !document_.IsEntityEditable(id)) return;
    replacement->SetId(id);
    Execute(std::make_unique<ReplaceEntityCommand>(id, existing->Clone(), std::move(replacement)));
}

void CommandManager::Undo()
{
    if (undoStack_.empty()) {
        return;
    }
    std::unique_ptr<ICommand> command = std::move(undoStack_.back());
    undoStack_.pop_back();
    command->Undo(document_);
    redoStack_.push_back(std::move(command));
}

void CommandManager::Redo()
{
    if (redoStack_.empty()) {
        return;
    }
    std::unique_ptr<ICommand> command = std::move(redoStack_.back());
    redoStack_.pop_back();
    command->Execute(document_);
    undoStack_.push_back(std::move(command));
}

void CommandManager::CreateDemoGeometry()
{
    document_.Clear();
    AddEntity(std::make_unique<LineEntity>(
        IdGenerator::Next(),
        GeoLine(GeoPoint(-100.0, -50.0), GeoPoint(100.0, 80.0))));
    AddEntity(std::make_unique<LineEntity>(
        IdGenerator::Next(),
        GeoLine(GeoPoint(-120.0, 80.0), GeoPoint(140.0, 80.0))));
    AddEntity(std::make_unique<CircleEntity>(
        IdGenerator::Next(),
        GeoCircle(GeoPoint(0.0, 0.0), 60.0)));
    AddEntity(std::make_unique<RegionEntity>(
        IdGenerator::Next(),
        ClipperUtil::CircleToRegion(GeoPoint(80.0, -40.0), 35.0),
        true));
}

bool CommandManager::CreateBlock(
    const std::string& name,
    double baseX,
    double baseY,
    const std::vector<EntityId>& entityIds,
    BlockCreateMode mode,
    bool allowExplode,
    bool uniformScale,
    bool annotative,
    std::vector<BlockAttributeDef> attributeDefs)
{
    const auto insertId = document_.CreateBlockDefinition(
        name, baseX, baseY, entityIds, mode, allowExplode, uniformScale, annotative, std::move(attributeDefs));
    if (!insertId.has_value() && mode == BlockCreateMode::Convert) {
        return false;
    }
    // Keep/Delete succeed when definition exists after call.
    if (!document_.HasBlockDefinition(name)) {
        return false;
    }
    redoStack_.clear();
    return true;
}

EntityId CommandManager::InsertBlock(
    const std::string& blockName,
    const GeoPoint& insertion,
    double scale,
    double rotationRadians,
    bool explode,
    std::vector<BlockAttributeValue> attributes)
{
    const BlockDefinition* def = document_.FindBlockDefinition(blockName);
    if (!def) {
        return 0;
    }

    auto entity = std::make_unique<BlockReferenceEntity>(
        IdGenerator::Next(), blockName, insertion, scale, rotationRadians);
    if (attributes.empty()) {
        for (const BlockAttributeDef& attrDef : def->attributeDefs) {
            entity->SetAttributeValue(attrDef.tag, attrDef.defaultValue);
        }
    } else {
        entity->SetAttributeValues(std::move(attributes));
    }
    if (!def->dynamic.IsEmpty()) {
        entity->SetDynamicValues(DynamicBlockEvaluator::ResolveValues(*def, {}));
    }
    entity->RebuildCache(def);
    const EntityId id = entity->Id();
    AddEntity(std::move(entity));

    if (explode) {
        if (!document_.ExplodeBlockReference(id)) {
            return id;
        }
        return 0;
    }
    return id;
}

int CommandManager::ExplodeSelectedBlocks()
{
    const std::vector<EntityId> selected = document_.SelectedEntityIds();
    int count = 0;
    for (EntityId id : selected) {
        if (document_.ExplodeBlockReference(id)) {
            ++count;
        }
    }
    if (count > 0) {
        redoStack_.clear();
    }
    return count;
}

} // namespace cad
