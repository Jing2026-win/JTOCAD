#pragma once

#include "DataModel/BlockDefinition.h"
#include "DataModel/Entity.h"
#include "DataModel/IDocumentObserver.h"
#include "DataModel/Layer.h"
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace cad {

class Entity;

struct LayerStateSnapshot {
    std::string name;
    std::vector<Layer> layers;
    std::string currentLayerName;
};

class CadDocument {
public:
    CadDocument();

    void AddObserver(IDocumentObserver* observer);
    void RemoveObserver(IDocumentObserver* observer);

    void AddEntity(std::unique_ptr<Entity> entity);
    std::unique_ptr<Entity> TakeEntity(EntityId id);
    std::vector<std::unique_ptr<Entity>> TakeEntities(const std::vector<EntityId>& ids);
    bool RemoveEntity(EntityId id);
    void Clear();

    Entity* FindEntity(EntityId id);
    const Entity* FindEntity(EntityId id) const;
    void MoveEntities(const std::vector<EntityId>& ids, double dx, double dy, bool notifyView = true);

    void ClearSelection();
    void SelectOnly(EntityId id);
    void ToggleSelection(EntityId id);
    std::vector<EntityId> SelectedEntityIds() const;

    Layer& EnsureLayer(const std::string& name);
    Layer* FindLayer(const std::string& name);
    const Layer* FindLayer(const std::string& name) const;
    const std::string& CurrentLayerName() const { return currentLayerName_; }
    bool SetCurrentLayerName(const std::string& name);
    bool IsLayerVisible(const std::string& name) const;
    bool IsLayerLocked(const std::string& name) const;
    bool IsLayerPlottable(const std::string& name) const;
    bool IsEntityVisible(const Entity& entity) const;
    bool IsEntityEditable(EntityId id) const;
    std::vector<EntityId> EditableEntityIds(const std::vector<EntityId>& ids) const;
    bool SetEntityLayer(EntityId id, const std::string& layerName);
    bool SetEntitiesLayer(const std::vector<EntityId>& ids, const std::string& layerName);
    bool SetEntitiesStyle(const std::vector<EntityId>& ids, const EntityStyle& style);
    bool MatchEntitiesProperties(EntityId sourceId, const std::vector<EntityId>& targetIds);

    const EntityStyle& CurrentEntityStyle() const { return currentEntityStyle_; }
    void SetCurrentEntityStyle(const EntityStyle& style);
    bool RenameLayer(const std::string& oldName, const std::string& newName);
    bool CanDeleteLayer(const std::string& name) const;
    bool DeleteLayerAndEntities(const std::string& name);
    bool MergeLayerInto(const std::string& sourceName, const std::string& targetName);
    LayerStateSnapshot CaptureLayerState(std::string name) const;
    bool RestoreLayerState(const LayerStateSnapshot& snapshot);
    bool BeginLayerIsolation();
    bool RestoreLayerIsolation();
    void NotifyAllEntitiesChanged();
    const std::vector<LayerStateSnapshot>& LayerStates() const { return layerStates_; }
    void SaveLayerState(std::string name);
    void AddLineType(LineTypeDefinition lineType);
    void ReplaceLineTypes(std::vector<LineTypeDefinition> lineTypes);
    const LineTypeDefinition* FindLineType(const std::string& name) const;
    LineTypeDefinition* FindLineType(const std::string& name);
    bool IsLineTypeInUse(const std::string& name) const;
    bool CanDeleteLineType(const std::string& name) const;
    bool RemoveLineType(const std::string& name);
    bool RenameLineType(const std::string& oldName, const std::string& newName);

    double GlobalLinetypeScale() const { return globalLinetypeScale_; }
    void SetGlobalLinetypeScale(double scale);
    double CurrentLinetypeScale() const { return currentLinetypeScale_; }
    void SetCurrentLinetypeScale(double scale);
    bool UsePaperSpaceLinetypeScale() const { return usePaperSpaceLinetypeScale_; }
    void SetUsePaperSpaceLinetypeScale(bool enabled);

    void AddTextStyle(TextStyleDefinition textStyle);
    void AddDimensionStyle(DimensionStyle dimensionStyle);
    bool RemoveDimensionStyle(const std::string& name);
    const std::string& CurrentDimensionStyleName() const { return currentDimensionStyleName_; }
    void SetCurrentDimensionStyleName(std::string name);
    void AddBlockDefinition(BlockDefinition block);
    BlockDefinition* FindBlockDefinition(const std::string& name);
    const BlockDefinition* FindBlockDefinition(const std::string& name) const;
    bool HasBlockDefinition(const std::string& name) const;
    bool IsBlockInUse(const std::string& name) const;
    bool RemoveBlockDefinition(const std::string& name);
    int PurgeUnusedBlocks();
    bool RenameBlockDefinition(const std::string& oldName, const std::string& newName);
    std::optional<EntityId> CreateBlockDefinition(
        const std::string& name,
        double baseX,
        double baseY,
        const std::vector<EntityId>& entityIds,
        BlockCreateMode mode,
        bool allowExplode = true,
        bool uniformScale = true,
        bool annotative = false,
        std::vector<BlockAttributeDef> attributeDefs = {});
    bool ExplodeBlockReference(EntityId id);
    bool ReplaceBlockDefinitionEntities(
        const std::string& name,
        std::vector<std::shared_ptr<Entity>> entities,
        double baseX,
        double baseY);
    void RefreshBlockReferenceCaches(const std::string& blockName);

    // REFEDIT working-set helpers (empty = not in ref-edit).
    void SetRefEditWorkingSet(std::vector<EntityId> ids);
    void ClearRefEditWorkingSet();
    bool IsRefEditActive() const { return refEditActive_; }
    const std::vector<EntityId>& RefEditWorkingSet() const { return refEditWorkingSet_; }
    bool IsInRefEditWorkingSet(EntityId id) const;

    void AddLayout(PageLayout layout);

    void NotifyChanged();
    void NotifyEntitiesUpdated(const std::vector<EntityId>& ids);
    void SyncSelectionView();

    const std::vector<std::unique_ptr<Entity>>& Entities() const { return entities_; }
    const std::vector<Layer>& Layers() const { return layers_; }
    std::vector<Layer>& Layers() { return layers_; }
    const std::vector<LineTypeDefinition>& LineTypes() const { return lineTypes_; }
    const std::vector<TextStyleDefinition>& TextStyles() const { return textStyles_; }
    const std::vector<DimensionStyle>& DimensionStyles() const { return dimensionStyles_; }
    const std::vector<BlockDefinition>& BlockDefinitions() const { return blockDefinitions_; }
    const std::vector<PageLayout>& Layouts() const { return layouts_; }
    PageLayout& CurrentLayout() { return layouts_[CurrentLayoutIndex()]; }
    const PageLayout& CurrentLayout() const { return layouts_[CurrentLayoutIndex()]; }
    std::size_t CurrentLayoutIndex() const { return currentLayoutIndex_ < layouts_.size() ? currentLayoutIndex_ : 0; }
    void SetCurrentLayout(std::size_t index) { if (index < layouts_.size()) currentLayoutIndex_ = index; }
    const Layer& CurrentLayer() const { return layers_.front(); }

private:
    void NotifyEntityAdded(const Entity& entity);
    void NotifyEntityRemoved(EntityId id);
    void NotifyEntitiesRemoved(const std::vector<EntityId>& ids);
    void NotifyEntitiesChanged(const std::vector<EntityId>& ids);
    void NotifySelectionChanged();
    void NotifyDocumentReset();

    std::vector<std::unique_ptr<Entity>> entities_;
    std::vector<Layer> layers_;
    std::string currentLayerName_ = "0";
    EntityStyle currentEntityStyle_;
    std::vector<LayerStateSnapshot> layerStates_;
    std::vector<LineTypeDefinition> lineTypes_;
    double globalLinetypeScale_ = 1.0;
    double currentLinetypeScale_ = 1.0;
    bool usePaperSpaceLinetypeScale_ = true;
    std::vector<TextStyleDefinition> textStyles_;
    std::vector<DimensionStyle> dimensionStyles_;
    std::string currentDimensionStyleName_ = "Standard";
    std::vector<BlockDefinition> blockDefinitions_;
    std::vector<PageLayout> layouts_;
    std::size_t currentLayoutIndex_ = 0;
    std::vector<IDocumentObserver*> observers_;
    bool refEditActive_ = false;
    std::vector<EntityId> refEditWorkingSet_;
};

} // namespace cad
