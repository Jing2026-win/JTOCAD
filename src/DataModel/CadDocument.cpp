#include "DataModel/CadDocument.h"

#include "Base/IdGenerator.h"
#include "DataModel/BlockReferenceEntity.h"
#include "DataModel/Entity.h"
#include "DataModel/TextEntity.h"
#include "Geometry/GeoPoint.h"
#include <algorithm>
#include <cmath>
#include <memory>
#include <utility>

namespace cad {

namespace {

constexpr int kMaxBlockExpandDepth = 16;

void ApplyInsertTransform(Entity& entity, const BlockReferenceEntity& ref)
{
    const GeoPoint origin(0.0, 0.0);
    const double sx = ref.ScaleX();
    const double sy = ref.ScaleY();
    if (std::abs(sx - sy) <= 1.0e-12) {
        if (std::abs(sx - 1.0) > 1.0e-12) {
            entity.Scale(origin, sx);
        }
    } else {
        const double avg = 0.5 * (sx + sy);
        if (std::abs(avg - 1.0) > 1.0e-12) {
            entity.Scale(origin, avg);
        }
    }
    if (std::abs(ref.RotationRadians()) > 1.0e-12) {
        entity.Rotate(origin, ref.RotationRadians());
    }
    entity.MoveBy(ref.InsertionPoint().X(), ref.InsertionPoint().Y());
}

// Expand a source entity into block-definition space (relative to base).
// Block references are soft-exploded so redefining with "原块参照 + 新图元"
// matches AutoCAD: visible geometry becomes the new definition (no self-nest).
void AppendEntityToBlockDefinition(
    const CadDocument& document,
    const Entity& source,
    double baseX,
    double baseY,
    std::vector<std::shared_ptr<Entity>>& out,
    int depth)
{
    if (source.Type() != EntityType::BlockReference) {
        std::unique_ptr<Entity> clone = source.Clone();
        if (!clone) {
            return;
        }
        clone->SetId(IdGenerator::Next());
        clone->SetSelected(false);
        clone->MoveBy(-baseX, -baseY);
        out.push_back(std::shared_ptr<Entity>(std::move(clone)));
        return;
    }

    if (depth >= kMaxBlockExpandDepth) {
        return;
    }

    const auto& ref = static_cast<const BlockReferenceEntity&>(source);
    const BlockDefinition* def = document.FindBlockDefinition(ref.BlockName());
    if (!def) {
        return;
    }

    for (const std::shared_ptr<Entity>& inner : def->entities) {
        if (!inner) {
            continue;
        }
        std::unique_ptr<Entity> clone = inner->Clone();
        if (!clone) {
            continue;
        }
        clone->SetId(IdGenerator::Next());
        clone->SetSelected(false);
        ApplyInsertTransform(*clone, ref);

        if (clone->Type() == EntityType::BlockReference) {
            AppendEntityToBlockDefinition(document, *clone, baseX, baseY, out, depth + 1);
        } else {
            clone->MoveBy(-baseX, -baseY);
            out.push_back(std::shared_ptr<Entity>(std::move(clone)));
        }
    }
}

} // namespace

CadDocument::CadDocument()
{
    layers_.emplace_back("0");
    Layer& defpoints = EnsureLayer("Defpoints");
    defpoints.SetPlottable(false);
    lineTypes_.push_back({});
    lineTypes_.push_back({"Dashed", "Dashed line", {6.0, -3.0}});
    lineTypes_.push_back({"Center", "Center line", {12.0, -3.0, 3.0, -3.0}});
    lineTypes_.push_back({"Hidden", "Hidden line", {6.0, -3.0}});
    lineTypes_.push_back({"Phantom", "Phantom line", {24.0, -6.0, 6.0, -6.0, 6.0, -6.0}});
    lineTypes_.push_back({"Dot", "Dot line", {0.0, -3.0}});
    lineTypes_.push_back({"Divide", "Divide line", {12.0, -3.0, 3.0, -3.0, 3.0, -3.0}});
    lineTypes_.push_back({"Border", "Border line", {12.0, -6.0, 12.0, -6.0, 12.0, -6.0, 12.0, -6.0, 0.0}});
    lineTypes_.push_back({"Dashdot", "Dash dot line", {12.0, -3.0, 3.0, -3.0}});
    textStyles_.push_back({});
    dimensionStyles_.push_back({});
    layouts_.push_back({});
}

void CadDocument::AddObserver(IDocumentObserver* observer)
{
    if (!observer) {
        return;
    }
    if (std::find(observers_.begin(), observers_.end(), observer) == observers_.end()) {
        observers_.push_back(observer);
    }
}

void CadDocument::RemoveObserver(IDocumentObserver* observer)
{
    observers_.erase(std::remove(observers_.begin(), observers_.end(), observer), observers_.end());
}

void CadDocument::AddEntity(std::unique_ptr<Entity> entity)
{
    if (!entity) {
        return;
    }

    if (entity->LayerName().empty()) {
        entity->SetLayerName(currentLayerName_);
    }
    EnsureLayer(entity->LayerName());
    entities_.push_back(std::move(entity));
    NotifyEntityAdded(*entities_.back());
}

std::unique_ptr<Entity> CadDocument::TakeEntity(EntityId id)
{
    auto it = std::find_if(entities_.begin(), entities_.end(), [id](const auto& entity) {
        return entity->Id() == id;
    });
    if (it == entities_.end()) {
        return nullptr;
    }

    NotifyEntityRemoved(id);
    std::unique_ptr<Entity> entity = std::move(*it);
    entities_.erase(it);
    return entity;
}

std::vector<std::unique_ptr<Entity>> CadDocument::TakeEntities(const std::vector<EntityId>& ids)
{
    std::vector<std::unique_ptr<Entity>> removed;
    std::vector<EntityId> removedIds;
    removed.reserve(ids.size());
    removedIds.reserve(ids.size());

    for (EntityId id : ids) {
        auto it = std::find_if(entities_.begin(), entities_.end(), [id](const auto& entity) {
            return entity->Id() == id;
        });
        if (it == entities_.end()) {
            continue;
        }
        removedIds.push_back(id);
    }

    if (!removedIds.empty()) {
        NotifyEntitiesRemoved(removedIds);
    }

    for (EntityId id : removedIds) {
        auto it = std::find_if(entities_.begin(), entities_.end(), [id](const auto& entity) {
            return entity->Id() == id;
        });
        if (it == entities_.end()) {
            continue;
        }
        removed.push_back(std::move(*it));
        entities_.erase(it);
    }
    return removed;
}

bool CadDocument::RemoveEntity(EntityId id)
{
    return TakeEntity(id) != nullptr;
}

void CadDocument::Clear()
{
    entities_.clear();
    layers_.clear();
    layers_.emplace_back("0");
    currentLayerName_ = "0";
    layerStates_.clear();
    Layer& defpoints = EnsureLayer("Defpoints");
    defpoints.SetPlottable(false);
    lineTypes_.clear();
    lineTypes_.push_back({});
    textStyles_.clear();
    textStyles_.push_back({});
    dimensionStyles_.clear();
    dimensionStyles_.push_back({});
    currentDimensionStyleName_ = "Standard";
    blockDefinitions_.clear();
    layouts_.clear();
    layouts_.push_back({});
    currentLayoutIndex_ = 0;
    NotifyDocumentReset();
}

Entity* CadDocument::FindEntity(EntityId id)
{
    const auto it = std::find_if(entities_.begin(), entities_.end(), [id](const auto& entity) {
        return entity->Id() == id;
    });
    return it == entities_.end() ? nullptr : it->get();
}

const Entity* CadDocument::FindEntity(EntityId id) const
{
    const auto it = std::find_if(entities_.begin(), entities_.end(), [id](const auto& entity) {
        return entity->Id() == id;
    });
    return it == entities_.end() ? nullptr : it->get();
}

void CadDocument::MoveEntities(const std::vector<EntityId>& ids, double dx, double dy, bool notifyView)
{
    for (EntityId id : ids) {
        if (Entity* entity = FindEntity(id)) {
            entity->MoveBy(dx, dy);
        }
    }
    if (notifyView) {
        NotifyEntitiesChanged(ids);
    }
}

void CadDocument::ClearSelection()
{
    for (const auto& entity : entities_) {
        entity->SetSelected(false);
    }
    NotifySelectionChanged();
}

void CadDocument::SelectOnly(EntityId id)
{
    for (const auto& entity : entities_) {
        entity->SetSelected(entity->Id() == id);
    }
    NotifySelectionChanged();
}

void CadDocument::ToggleSelection(EntityId id)
{
    if (Entity* entity = FindEntity(id)) {
        entity->SetSelected(!entity->IsSelected());
    }
    NotifySelectionChanged();
}

std::vector<EntityId> CadDocument::SelectedEntityIds() const
{
    std::vector<EntityId> selected;
    for (const auto& entity : entities_) {
        if (entity->IsSelected()) {
            selected.push_back(entity->Id());
        }
    }
    return selected;
}

Layer& CadDocument::EnsureLayer(const std::string& name)
{
    const std::string layerName = name.empty() ? "0" : name;
    auto it = std::find_if(layers_.begin(), layers_.end(), [&](const Layer& layer) {
        return layer.Name() == layerName;
    });
    if (it != layers_.end()) {
        return *it;
    }
    layers_.emplace_back(layerName);
    if (layerName == "Defpoints") {
        layers_.back().SetPlottable(false);
    }
    return layers_.back();
}

Layer* CadDocument::FindLayer(const std::string& name)
{
    const auto it = std::find_if(layers_.begin(), layers_.end(), [&](const Layer& layer) {
        return layer.Name() == name;
    });
    return it == layers_.end() ? nullptr : &*it;
}

const Layer* CadDocument::FindLayer(const std::string& name) const
{
    const auto it = std::find_if(layers_.begin(), layers_.end(), [&](const Layer& layer) {
        return layer.Name() == name;
    });
    return it == layers_.end() ? nullptr : &*it;
}

bool CadDocument::SetCurrentLayerName(const std::string& name)
{
    const Layer* layer = FindLayer(name);
    if (!layer || layer->IsFrozen()) {
        return false;
    }
    currentLayerName_ = layer->Name();
    NotifyChanged();
    return true;
}

bool CadDocument::IsLayerVisible(const std::string& name) const
{
    const Layer* layer = FindLayer(name.empty() ? "0" : name);
    return !layer || layer->IsVisible();
}

bool CadDocument::IsLayerLocked(const std::string& name) const
{
    const Layer* layer = FindLayer(name.empty() ? "0" : name);
    return layer && layer->IsLocked();
}

bool CadDocument::IsLayerPlottable(const std::string& name) const
{
    const std::string layerName = name.empty() ? "0" : name;
    if (layerName == "Defpoints") {
        return false;
    }
    const Layer* layer = FindLayer(layerName);
    return !layer || layer->IsPlottable();
}

bool CadDocument::IsEntityVisible(const Entity& entity) const
{
    return IsLayerVisible(entity.LayerName());
}

bool CadDocument::IsEntityEditable(const EntityId id) const
{
    const Entity* entity = FindEntity(id);
    if (!entity || !IsEntityVisible(*entity) || IsLayerLocked(entity->LayerName())) {
        return false;
    }
    if (refEditActive_ && !IsInRefEditWorkingSet(id)) {
        return false;
    }
    return true;
}

std::vector<EntityId> CadDocument::EditableEntityIds(const std::vector<EntityId>& ids) const
{
    std::vector<EntityId> editable;
    editable.reserve(ids.size());
    for (const EntityId id : ids) {
        if (IsEntityEditable(id)) {
            editable.push_back(id);
        }
    }
    return editable;
}

bool CadDocument::SetEntityLayer(const EntityId id, const std::string& layerName)
{
    Entity* entity = FindEntity(id);
    if (!entity || !IsEntityEditable(id)) {
        return false;
    }
    Layer& layer = EnsureLayer(layerName);
    if (layer.IsFrozen()) {
        return false;
    }
    entity->SetLayerName(layer.Name());
    NotifyEntitiesChanged({id});
    return true;
}

bool CadDocument::SetEntitiesLayer(const std::vector<EntityId>& ids, const std::string& layerName)
{
    Layer& layer = EnsureLayer(layerName);
    if (layer.IsFrozen()) {
        return false;
    }

    std::vector<EntityId> changed;
    for (const EntityId id : ids) {
        Entity* entity = FindEntity(id);
        if (!entity || !IsEntityEditable(id)) {
            continue;
        }
        entity->SetLayerName(layer.Name());
        changed.push_back(id);
    }
    if (!changed.empty()) {
        NotifyEntitiesChanged(changed);
    }
    return !changed.empty();
}

bool CadDocument::SetEntitiesStyle(const std::vector<EntityId>& ids, const EntityStyle& style)
{
    std::vector<EntityId> changed;
    for (const EntityId id : ids) {
        Entity* entity = FindEntity(id);
        if (!entity || !IsEntityEditable(id)) {
            continue;
        }
        entity->SetStyle(style);
        changed.push_back(id);
    }
    if (!changed.empty()) {
        NotifyEntitiesChanged(changed);
    }
    return !changed.empty();
}

bool CadDocument::MatchEntitiesProperties(EntityId sourceId, const std::vector<EntityId>& targetIds)
{
    const Entity* source = FindEntity(sourceId);
    if (!source) {
        return false;
    }

    std::vector<EntityId> changed;
    for (const EntityId id : targetIds) {
        if (id == sourceId) {
            continue;
        }
        Entity* entity = FindEntity(id);
        if (!entity || !IsEntityEditable(id)) {
            continue;
        }
        entity->SetLayerName(source->LayerName());
        entity->SetStyle(source->Style());
        changed.push_back(id);
    }
    if (!changed.empty()) {
        NotifyEntitiesChanged(changed);
    }
    return !changed.empty();
}

void CadDocument::SetCurrentEntityStyle(const EntityStyle& style)
{
    currentEntityStyle_ = style;
    if (style.lineTypeScale > 0.0) {
        currentLinetypeScale_ = style.lineTypeScale;
    }
    NotifySelectionChanged();
}

bool CadDocument::RenameLayer(const std::string& oldName, const std::string& newName)
{
    if (newName.empty() || oldName == newName || FindLayer(newName)) {
        return false;
    }
    Layer* layer = FindLayer(oldName);
    if (!layer || layer->IsSystemLayer()) {
        return false;
    }

    layer->SetName(newName);
    std::vector<EntityId> changed;
    for (const auto& entity : entities_) {
        if (entity && entity->LayerName() == oldName) {
            entity->SetLayerName(newName);
            changed.push_back(entity->Id());
        }
    }
    if (currentLayerName_ == oldName) {
        currentLayerName_ = newName;
    }
    if (!changed.empty()) {
        NotifyEntitiesChanged(changed);
    }
    NotifyChanged();
    return true;
}

bool CadDocument::CanDeleteLayer(const std::string& name) const
{
    const Layer* layer = FindLayer(name);
    return layer && !layer->IsSystemLayer() && layer->Name() != currentLayerName_;
}

bool CadDocument::DeleteLayerAndEntities(const std::string& name)
{
    if (!CanDeleteLayer(name)) {
        return false;
    }

    std::vector<EntityId> removedIds;
    entities_.erase(
        std::remove_if(entities_.begin(), entities_.end(), [&](const std::unique_ptr<Entity>& entity) {
            if (entity && entity->LayerName() == name) {
                removedIds.push_back(entity->Id());
                return true;
            }
            return false;
        }),
        entities_.end());
    if (!removedIds.empty()) {
        NotifyEntitiesRemoved(removedIds);
    }

    layers_.erase(
        std::remove_if(layers_.begin(), layers_.end(), [&](const Layer& layer) {
            return layer.Name() == name;
        }),
        layers_.end());
    NotifyChanged();
    return true;
}

bool CadDocument::MergeLayerInto(const std::string& sourceName, const std::string& targetName)
{
    if (sourceName == targetName || !CanDeleteLayer(sourceName)) {
        return false;
    }

    Layer& target = EnsureLayer(targetName);
    std::vector<EntityId> changed;
    for (const auto& entity : entities_) {
        if (entity && entity->LayerName() == sourceName) {
            entity->SetLayerName(target.Name());
            changed.push_back(entity->Id());
        }
    }

    layers_.erase(
        std::remove_if(layers_.begin(), layers_.end(), [&](const Layer& layer) {
            return layer.Name() == sourceName;
        }),
        layers_.end());
    if (!changed.empty()) {
        NotifyEntitiesChanged(changed);
    }
    NotifyChanged();
    return true;
}

LayerStateSnapshot CadDocument::CaptureLayerState(std::string name) const
{
    LayerStateSnapshot snapshot;
    snapshot.name = std::move(name);
    snapshot.layers = layers_;
    snapshot.currentLayerName = currentLayerName_;
    return snapshot;
}

bool CadDocument::RestoreLayerState(const LayerStateSnapshot& snapshot)
{
    if (snapshot.layers.empty()) {
        return false;
    }
    layers_ = snapshot.layers;
    currentLayerName_ = snapshot.currentLayerName.empty() ? "0" : snapshot.currentLayerName;
    EnsureLayer("0");
    Layer& defpoints = EnsureLayer("Defpoints");
    defpoints.SetPlottable(false);
    NotifyAllEntitiesChanged();
    return true;
}

bool CadDocument::BeginLayerIsolation()
{
    const bool exists = std::any_of(layerStates_.begin(), layerStates_.end(), [](const LayerStateSnapshot& state) {
        return state.name == "__LAYISO";
    });
    if (exists) {
        return true;
    }
    layerStates_.push_back(CaptureLayerState("__LAYISO"));
    return true;
}

bool CadDocument::RestoreLayerIsolation()
{
    const auto it = std::find_if(layerStates_.begin(), layerStates_.end(), [](const LayerStateSnapshot& state) {
        return state.name == "__LAYISO";
    });
    if (it == layerStates_.end()) {
        return false;
    }
    const LayerStateSnapshot snapshot = *it;
    if (!RestoreLayerState(snapshot)) {
        return false;
    }
    layerStates_.erase(it);
    return true;
}

void CadDocument::NotifyAllEntitiesChanged()
{
    std::vector<EntityId> ids;
    ids.reserve(entities_.size());
    for (const std::unique_ptr<Entity>& entity : entities_) {
        if (entity) {
            ids.push_back(entity->Id());
        }
    }
    NotifyEntitiesChanged(ids);
}

void CadDocument::SaveLayerState(std::string name)
{
    if (name.empty()) {
        name = "Layer State";
    }
    if (name == "__LAYISO") {
        BeginLayerIsolation();
        return;
    }
    LayerStateSnapshot snapshot = CaptureLayerState(name);
    const auto it = std::find_if(layerStates_.begin(), layerStates_.end(), [&](const LayerStateSnapshot& existing) {
        return existing.name == snapshot.name;
    });
    if (it == layerStates_.end()) {
        layerStates_.push_back(std::move(snapshot));
    } else {
        *it = std::move(snapshot);
    }
}

void CadDocument::AddLineType(LineTypeDefinition lineType)
{
    auto it = std::find_if(lineTypes_.begin(), lineTypes_.end(), [&](const LineTypeDefinition& existing) {
        return existing.name == lineType.name;
    });
    if (it == lineTypes_.end()) {
        lineTypes_.push_back(std::move(lineType));
    } else {
        *it = std::move(lineType);
    }
}

void CadDocument::ReplaceLineTypes(std::vector<LineTypeDefinition> lineTypes)
{
    lineTypes_ = std::move(lineTypes);
    NotifyAllEntitiesChanged();
}

const LineTypeDefinition* CadDocument::FindLineType(const std::string& name) const
{
    for (const LineTypeDefinition& lineType : lineTypes_) {
        if (lineType.name == name) {
            return &lineType;
        }
    }
    return nullptr;
}

LineTypeDefinition* CadDocument::FindLineType(const std::string& name)
{
    for (LineTypeDefinition& lineType : lineTypes_) {
        if (lineType.name == name) {
            return &lineType;
        }
    }
    return nullptr;
}

bool CadDocument::IsLineTypeInUse(const std::string& name) const
{
    for (const Layer& layer : layers_) {
        if (layer.LineTypeName() == name) {
            return true;
        }
    }
    for (const auto& entity : entities_) {
        if (!entity) {
            continue;
        }
        const EntityStyle& style = entity->Style();
        if (style.lineTypeSource == PropertySource::Override && style.lineTypeName == name) {
            return true;
        }
    }
    return false;
}

bool CadDocument::CanDeleteLineType(const std::string& name) const
{
    if (name.empty() || name == "Continuous" || name == "ByLayer" || name == "ByBlock") {
        return false;
    }
    return !IsLineTypeInUse(name) && FindLineType(name) != nullptr;
}

bool CadDocument::RemoveLineType(const std::string& name)
{
    if (!CanDeleteLineType(name)) {
        return false;
    }
    lineTypes_.erase(
        std::remove_if(lineTypes_.begin(), lineTypes_.end(), [&](const LineTypeDefinition& lt) {
            return lt.name == name;
        }),
        lineTypes_.end());
    return true;
}

bool CadDocument::RenameLineType(const std::string& oldName, const std::string& newName)
{
    if (newName.empty() || oldName == newName || FindLineType(newName)
        || oldName == "Continuous" || oldName == "ByLayer" || oldName == "ByBlock") {
        return false;
    }
    LineTypeDefinition* lineType = FindLineType(oldName);
    if (!lineType) {
        return false;
    }
    lineType->name = newName;
    for (Layer& layer : layers_) {
        if (layer.LineTypeName() == oldName) {
            layer.SetLineTypeName(newName);
        }
    }
    for (const auto& entity : entities_) {
        if (!entity) {
            continue;
        }
        EntityStyle style = entity->Style();
        if (style.lineTypeSource == PropertySource::Override && style.lineTypeName == oldName) {
            style.lineTypeName = newName;
            entity->SetStyle(style);
        }
    }
    if (currentEntityStyle_.lineTypeSource == PropertySource::Override
        && currentEntityStyle_.lineTypeName == oldName) {
        currentEntityStyle_.lineTypeName = newName;
    }
    NotifyAllEntitiesChanged();
    return true;
}

void CadDocument::SetGlobalLinetypeScale(double scale)
{
    globalLinetypeScale_ = std::max(0.0001, scale);
    NotifyAllEntitiesChanged();
}

void CadDocument::SetCurrentLinetypeScale(double scale)
{
    currentLinetypeScale_ = std::max(0.0001, scale);
    currentEntityStyle_.lineTypeScale = currentLinetypeScale_;
    NotifySelectionChanged();
}

void CadDocument::SetUsePaperSpaceLinetypeScale(bool enabled)
{
    usePaperSpaceLinetypeScale_ = enabled;
    NotifyAllEntitiesChanged();
}

void CadDocument::AddTextStyle(TextStyleDefinition textStyle)
{
    auto it = std::find_if(textStyles_.begin(), textStyles_.end(), [&](const TextStyleDefinition& existing) {
        return existing.name == textStyle.name;
    });
    if (it == textStyles_.end()) {
        textStyles_.push_back(std::move(textStyle));
    } else {
        *it = std::move(textStyle);
    }
}

void CadDocument::AddDimensionStyle(DimensionStyle dimensionStyle)
{
    auto it = std::find_if(dimensionStyles_.begin(), dimensionStyles_.end(), [&](const DimensionStyle& existing) {
        return existing.name == dimensionStyle.name;
    });
    if (it == dimensionStyles_.end()) {
        dimensionStyles_.push_back(std::move(dimensionStyle));
    } else {
        *it = std::move(dimensionStyle);
    }
}

bool CadDocument::RemoveDimensionStyle(const std::string& name)
{
    if (name == "Standard" || dimensionStyles_.size() <= 1) {
        return false;
    }
    const auto it = std::find_if(dimensionStyles_.begin(), dimensionStyles_.end(), [&](const DimensionStyle& existing) {
        return existing.name == name;
    });
    if (it == dimensionStyles_.end()) {
        return false;
    }
    dimensionStyles_.erase(it);
    if (currentDimensionStyleName_ == name) {
        currentDimensionStyleName_ = dimensionStyles_.empty() ? "Standard" : dimensionStyles_.front().name;
    }
    return true;
}

void CadDocument::SetCurrentDimensionStyleName(std::string name)
{
    if (name.empty()) {
        return;
    }
    for (const DimensionStyle& style : dimensionStyles_) {
        if (style.name == name) {
            currentDimensionStyleName_ = std::move(name);
            return;
        }
    }
}

void CadDocument::AddBlockDefinition(BlockDefinition block)
{
    auto it = std::find_if(blockDefinitions_.begin(), blockDefinitions_.end(), [&](const BlockDefinition& existing) {
        return existing.name == block.name;
    });
    if (it == blockDefinitions_.end()) {
        blockDefinitions_.push_back(std::move(block));
    } else {
        *it = std::move(block);
    }
}

BlockDefinition* CadDocument::FindBlockDefinition(const std::string& name)
{
    auto it = std::find_if(blockDefinitions_.begin(), blockDefinitions_.end(), [&](const BlockDefinition& block) {
        return block.name == name;
    });
    return it == blockDefinitions_.end() ? nullptr : &(*it);
}

const BlockDefinition* CadDocument::FindBlockDefinition(const std::string& name) const
{
    auto it = std::find_if(blockDefinitions_.begin(), blockDefinitions_.end(), [&](const BlockDefinition& block) {
        return block.name == name;
    });
    return it == blockDefinitions_.end() ? nullptr : &(*it);
}

bool CadDocument::HasBlockDefinition(const std::string& name) const
{
    return FindBlockDefinition(name) != nullptr;
}

bool CadDocument::IsBlockInUse(const std::string& name) const
{
    for (const auto& entity : entities_) {
        if (entity->Type() != EntityType::BlockReference) {
            continue;
        }
        if (static_cast<const BlockReferenceEntity&>(*entity).BlockName() == name) {
            return true;
        }
        // Nested blocks inside definitions are tracked separately.
    }
    for (const BlockDefinition& def : blockDefinitions_) {
        for (const std::shared_ptr<Entity>& child : def.entities) {
            if (!child || child->Type() != EntityType::BlockReference) {
                continue;
            }
            if (static_cast<const BlockReferenceEntity&>(*child).BlockName() == name) {
                return true;
            }
        }
    }
    return false;
}

bool CadDocument::RemoveBlockDefinition(const std::string& name)
{
    if (IsBlockInUse(name)) {
        return false;
    }
    auto it = std::find_if(blockDefinitions_.begin(), blockDefinitions_.end(), [&](const BlockDefinition& block) {
        return block.name == name;
    });
    if (it == blockDefinitions_.end()) {
        return false;
    }
    blockDefinitions_.erase(it);
    return true;
}

int CadDocument::PurgeUnusedBlocks()
{
    int removed = 0;
    for (auto it = blockDefinitions_.begin(); it != blockDefinitions_.end();) {
        if (!IsBlockInUse(it->name)) {
            it = blockDefinitions_.erase(it);
            ++removed;
        } else {
            ++it;
        }
    }
    return removed;
}

bool CadDocument::RenameBlockDefinition(const std::string& oldName, const std::string& newName)
{
    if (oldName.empty() || newName.empty() || oldName == newName) {
        return false;
    }
    if (HasBlockDefinition(newName)) {
        return false;
    }
    BlockDefinition* def = FindBlockDefinition(oldName);
    if (!def) {
        return false;
    }
    def->name = newName;

    std::vector<EntityId> updated;
    for (auto& entity : entities_) {
        if (entity->Type() != EntityType::BlockReference) {
            continue;
        }
        auto& ref = static_cast<BlockReferenceEntity&>(*entity);
        if (ref.BlockName() == oldName) {
            ref.SetBlockName(newName);
            updated.push_back(ref.Id());
        }
    }
    if (!updated.empty()) {
        NotifyEntitiesChanged(updated);
    }
    return true;
}

std::optional<EntityId> CadDocument::CreateBlockDefinition(
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
    if (name.empty() || entityIds.empty()) {
        return std::nullopt;
    }

    BlockDefinition block;
    block.name = name;
    block.baseX = baseX;
    block.baseY = baseY;
    block.allowExplode = allowExplode;
    block.uniformScale = uniformScale;
    block.annotative = annotative;
    block.attributeDefs = std::move(attributeDefs);

    for (EntityId id : entityIds) {
        const Entity* source = FindEntity(id);
        if (!source) {
            continue;
        }
        AppendEntityToBlockDefinition(*this, *source, baseX, baseY, block.entities, 0);
    }
    if (block.entities.empty()) {
        return std::nullopt;
    }

    AddBlockDefinition(std::move(block));

    std::optional<EntityId> insertId;
    if (mode == BlockCreateMode::Convert) {
        auto insert = std::make_unique<BlockReferenceEntity>(
            IdGenerator::Next(), name, GeoPoint(baseX, baseY), 1.0, 0.0);
        if (const BlockDefinition* def = FindBlockDefinition(name)) {
            for (const BlockAttributeDef& attrDef : def->attributeDefs) {
                insert->SetAttributeValue(attrDef.tag, attrDef.defaultValue);
            }
            insert->RebuildCache(def);
        }
        insertId = insert->Id();
        AddEntity(std::move(insert));
        TakeEntities(entityIds);
    } else if (mode == BlockCreateMode::Delete) {
        TakeEntities(entityIds);
    }

    // Convert/Delete already notified; Keep needs nothing entity-side.
    // Redefined blocks already in use should refresh visuals.
    std::vector<EntityId> refs;
    for (const auto& entity : entities_) {
        if (entity->Type() == EntityType::BlockReference
            && static_cast<const BlockReferenceEntity&>(*entity).BlockName() == name) {
            auto& ref = static_cast<BlockReferenceEntity&>(*entity);
            ref.InvalidateCache();
            if (const BlockDefinition* def = FindBlockDefinition(name)) {
                ref.RebuildCache(def);
            }
            refs.push_back(entity->Id());
        }
    }
    if (!refs.empty()) {
        NotifyEntitiesChanged(refs);
    }
    return insertId;
}

bool CadDocument::ExplodeBlockReference(EntityId id)
{
    Entity* entity = FindEntity(id);
    if (!entity || entity->Type() != EntityType::BlockReference) {
        return false;
    }

    auto& ref = static_cast<BlockReferenceEntity&>(*entity);
    const BlockDefinition* def = FindBlockDefinition(ref.BlockName());
    if (!def || !def->allowExplode) {
        return false;
    }

    const GeoPoint origin(0.0, 0.0);
    const double sx = ref.ScaleX();
    const double rotation = ref.RotationRadians();
    const GeoPoint insertion = ref.InsertionPoint();
    const std::string layerName = ref.LayerName();
    const EntityStyle style = ref.Style();
    const auto attributeValues = ref.AttributeValues();

    std::vector<std::unique_ptr<Entity>> exploded;
    exploded.reserve(def->entities.size() + def->attributeDefs.size());

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
        if (std::abs(sx - 1.0) > 1.0e-12) {
            clone->Scale(origin, sx);
        }
        if (std::abs(rotation) > 1.0e-12) {
            clone->Rotate(origin, rotation);
        }
        clone->MoveBy(insertion.X(), insertion.Y());
        if (clone->LayerName().empty()) {
            clone->SetLayerName(layerName);
        }
        exploded.push_back(std::move(clone));
    }

    for (const BlockAttributeDef& attrDef : def->attributeDefs) {
        if (attrDef.invisible) {
            continue;
        }
        std::string value = attrDef.defaultValue;
        for (const BlockAttributeValue& av : attributeValues) {
            if (av.tag == attrDef.tag) {
                value = av.value;
                break;
            }
        }
        auto text = std::make_unique<TextEntity>(
            IdGenerator::Next(),
            insertion.Translated(0.0, -attrDef.textHeight * 1.5),
            value.empty() ? attrDef.tag : value,
            attrDef.textHeight,
            rotation);
        text->SetLayerName(layerName);
        text->SetStyle(style);
        exploded.push_back(std::move(text));
    }

    TakeEntity(id);
    for (auto& piece : exploded) {
        AddEntity(std::move(piece));
    }
    return true;
}

bool CadDocument::ReplaceBlockDefinitionEntities(
    const std::string& name,
    std::vector<std::shared_ptr<Entity>> entities,
    double baseX,
    double baseY)
{
    BlockDefinition* def = FindBlockDefinition(name);
    if (!def || entities.empty()) {
        return false;
    }
    def->entities = std::move(entities);
    def->baseX = baseX;
    def->baseY = baseY;
    RefreshBlockReferenceCaches(name);
    return true;
}

void CadDocument::RefreshBlockReferenceCaches(const std::string& blockName)
{
    const BlockDefinition* def = FindBlockDefinition(blockName);
    std::vector<EntityId> refs;
    for (const auto& entity : entities_) {
        if (!entity || entity->Type() != EntityType::BlockReference) {
            continue;
        }
        auto& ref = static_cast<BlockReferenceEntity&>(*entity);
        if (ref.BlockName() != blockName) {
            continue;
        }
        ref.InvalidateCache();
        ref.RebuildCache(def);
        refs.push_back(entity->Id());
    }
    if (!refs.empty()) {
        NotifyEntitiesChanged(refs);
    }
}

void CadDocument::SetRefEditWorkingSet(std::vector<EntityId> ids)
{
    refEditWorkingSet_ = std::move(ids);
    refEditActive_ = true;
    NotifyAllEntitiesChanged();
}

void CadDocument::ClearRefEditWorkingSet()
{
    refEditWorkingSet_.clear();
    refEditActive_ = false;
    NotifyAllEntitiesChanged();
}

bool CadDocument::IsInRefEditWorkingSet(EntityId id) const
{
    if (!refEditActive_) {
        return true;
    }
    return std::find(refEditWorkingSet_.begin(), refEditWorkingSet_.end(), id) != refEditWorkingSet_.end();
}

void CadDocument::AddLayout(PageLayout layout)
{
    auto it = std::find_if(layouts_.begin(), layouts_.end(), [&](const PageLayout& existing) {
        return existing.name == layout.name;
    });
    if (it == layouts_.end()) {
        layouts_.push_back(std::move(layout));
    } else {
        *it = std::move(layout);
    }
}

void CadDocument::NotifyChanged()
{
    NotifyDocumentReset();
}

void CadDocument::NotifyEntitiesUpdated(const std::vector<EntityId>& ids)
{
    NotifyEntitiesChanged(ids);
}

void CadDocument::SyncSelectionView()
{
    NotifySelectionChanged();
}

void CadDocument::NotifyEntityAdded(const Entity& entity)
{
    for (IDocumentObserver* observer : observers_) {
        observer->OnEntityAdded(entity);
    }
}

void CadDocument::NotifyEntityRemoved(EntityId id)
{
    for (IDocumentObserver* observer : observers_) {
        observer->OnEntityRemoved(id);
    }
}

void CadDocument::NotifyEntitiesRemoved(const std::vector<EntityId>& ids)
{
    for (IDocumentObserver* observer : observers_) {
        observer->OnEntitiesRemoved(ids);
    }
}

void CadDocument::NotifyEntitiesChanged(const std::vector<EntityId>& ids)
{
    for (IDocumentObserver* observer : observers_) {
        observer->OnEntitiesChanged(ids);
    }
}

void CadDocument::NotifySelectionChanged()
{
    for (IDocumentObserver* observer : observers_) {
        observer->OnSelectionChanged();
    }
}

void CadDocument::NotifyDocumentReset()
{
    for (IDocumentObserver* observer : observers_) {
        observer->OnDocumentReset();
    }
}

} // namespace cad
