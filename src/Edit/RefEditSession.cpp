#include "Edit/RefEditSession.h"

#include "Base/IdGenerator.h"
#include "DataModel/BlockReferenceEntity.h"
#include "DataModel/CadDocument.h"
#include "DataModel/Entity.h"
#include <algorithm>
#include <cmath>
#include <memory>
#include <unordered_set>

namespace cad {

RefEditSession::RefEditSession(CadDocument& document)
    : document_(document)
{
}

bool RefEditSession::Begin(EntityId blockReferenceId)
{
    if (active_) {
        return false;
    }

    Entity* entity = document_.FindEntity(blockReferenceId);
    if (!entity || entity->Type() != EntityType::BlockReference) {
        return false;
    }

    auto& ref = static_cast<BlockReferenceEntity&>(*entity);
    const BlockDefinition* def = document_.FindBlockDefinition(ref.BlockName());
    if (!def) {
        return false;
    }

    targetRefId_ = blockReferenceId;
    blockName_ = ref.BlockName();
    insertion_ = ref.InsertionPoint();
    scale_ = ref.ScaleX();
    rotation_ = ref.RotationRadians();
    baseX_ = def->baseX;
    baseY_ = def->baseY;
    allowExplode_ = def->allowExplode;
    uniformScale_ = def->uniformScale;
    annotative_ = def->annotative;
    attributeDefs_ = def->attributeDefs;

    const GeoPoint origin(0.0, 0.0);
    workingIds_.clear();
    for (const std::shared_ptr<Entity>& source : def->entities) {
        if (!source) {
            continue;
        }
        std::unique_ptr<Entity> clone = source->Clone();
        if (!clone) {
            continue;
        }
        clone->SetId(IdGenerator::Next());
        clone->SetSelected(false);
        if (std::abs(scale_ - 1.0) > 1.0e-12) {
            clone->Scale(origin, scale_);
        }
        if (std::abs(rotation_) > 1.0e-12) {
            clone->Rotate(origin, rotation_);
        }
        clone->MoveBy(insertion_.X(), insertion_.Y());
        const EntityId id = clone->Id();
        document_.AddEntity(std::move(clone));
        workingIds_.push_back(id);
    }

    // Hide original reference while editing (take it out of selection; keep id for restore).
    // Soft-hide via layer lock isn't ideal; instead remove from display by taking and storing?
    // Keep entity but mark it out of working set so it is dimmed; remove from scene visibility
    // by temporarily moving far? Better: TakeEntity and store clone for restore.
    // Simpler AutoCAD-like approach: leave ref in place but dimmed (not in working set).
    document_.ClearSelection();
    for (EntityId id : workingIds_) {
        if (Entity* work = document_.FindEntity(id)) {
            work->SetSelected(true);
        }
    }
    document_.SyncSelectionView();
    document_.SetRefEditWorkingSet(workingIds_);
    active_ = true;
    originalRefHidden_ = false;
    return true;
}

int RefEditSession::AddSelectionToWorkingSet(const std::vector<EntityId>& selectedIds)
{
    if (!active_) {
        return 0;
    }
    int added = 0;
    for (EntityId id : selectedIds) {
        if (id == targetRefId_) {
            continue;
        }
        if (!document_.FindEntity(id)) {
            continue;
        }
        if (std::find(workingIds_.begin(), workingIds_.end(), id) != workingIds_.end()) {
            continue;
        }
        workingIds_.push_back(id);
        ++added;
    }
    document_.SetRefEditWorkingSet(workingIds_);
    return added;
}

int RefEditSession::RemoveSelectionFromWorkingSet(const std::vector<EntityId>& selectedIds)
{
    if (!active_) {
        return 0;
    }
    int removed = 0;
    std::unordered_set<EntityId> removeSet(selectedIds.begin(), selectedIds.end());
    std::vector<EntityId> kept;
    kept.reserve(workingIds_.size());
    for (EntityId id : workingIds_) {
        if (removeSet.count(id)) {
            ++removed;
        } else {
            kept.push_back(id);
        }
    }
    workingIds_ = std::move(kept);
    document_.SetRefEditWorkingSet(workingIds_);
    return removed;
}

bool RefEditSession::WriteBackDefinition()
{
    const GeoPoint origin(0.0, 0.0);
    std::vector<std::shared_ptr<Entity>> entities;
    for (EntityId id : workingIds_) {
        Entity* source = document_.FindEntity(id);
        if (!source || source->Type() == EntityType::BlockReference) {
            continue;
        }
        std::unique_ptr<Entity> clone = source->Clone();
        if (!clone) {
            continue;
        }
        // Inverse of Begin transform: move back, un-rotate, un-scale.
        clone->MoveBy(-insertion_.X(), -insertion_.Y());
        if (std::abs(rotation_) > 1.0e-12) {
            clone->Rotate(origin, -rotation_);
        }
        if (std::abs(scale_) > 1.0e-12 && std::abs(scale_ - 1.0) > 1.0e-12) {
            clone->Scale(origin, 1.0 / scale_);
        }
        clone->SetId(IdGenerator::Next());
        clone->SetSelected(false);
        entities.push_back(std::shared_ptr<Entity>(std::move(clone)));
    }
    if (entities.empty()) {
        return false;
    }

    if (!document_.HasBlockDefinition(blockName_)) {
        BlockDefinition def;
        def.name = blockName_;
        def.baseX = baseX_;
        def.baseY = baseY_;
        def.allowExplode = allowExplode_;
        def.uniformScale = uniformScale_;
        def.annotative = annotative_;
        def.attributeDefs = attributeDefs_;
        def.entities = entities;
        document_.AddBlockDefinition(std::move(def));
        document_.RefreshBlockReferenceCaches(blockName_);
        return true;
    }

    return document_.ReplaceBlockDefinitionEntities(blockName_, std::move(entities), baseX_, baseY_);
}

void RefEditSession::CleanupWorkingEntities(bool /*keepOriginalRefVisible*/)
{
    if (!workingIds_.empty()) {
        document_.TakeEntities(workingIds_);
        workingIds_.clear();
    }
    document_.ClearRefEditWorkingSet();
}

bool RefEditSession::Close(bool save)
{
    if (!active_) {
        return false;
    }
    if (save) {
        if (!WriteBackDefinition()) {
            return false;
        }
    }
    CleanupWorkingEntities(true);
    active_ = false;
    targetRefId_ = 0;
    blockName_.clear();
    return true;
}

} // namespace cad
