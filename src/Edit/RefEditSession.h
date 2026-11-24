#pragma once

#include "Base/CadTypes.h"
#include "DataModel/BlockDefinition.h"
#include "Geometry/GeoPoint.h"
#include <optional>
#include <string>
#include <vector>

namespace cad {

class CadDocument;
class CommandManager;

// AutoCAD-like REFEDIT session on the host drawing.
class RefEditSession {
public:
    explicit RefEditSession(CadDocument& document);

    bool IsActive() const { return active_; }
    const std::string& BlockName() const { return blockName_; }
    EntityId TargetReferenceId() const { return targetRefId_; }

    // Begin in-place edit of a block reference (soft-explode working copy).
    bool Begin(EntityId blockReferenceId);
    // REFSET Add: add currently selected entities into working set.
    int AddSelectionToWorkingSet(const std::vector<EntityId>& selectedIds);
    // REFSET Remove: remove selected entities from working set (and delete them if they were temps).
    int RemoveSelectionFromWorkingSet(const std::vector<EntityId>& selectedIds);
    // REFCLOSE Save / Discard
    bool Close(bool save);
    void Cancel() { Close(false); }

private:
    bool WriteBackDefinition();
    void CleanupWorkingEntities(bool keepOriginalRefVisible);

    CadDocument& document_;
    bool active_ = false;
    EntityId targetRefId_ = 0;
    std::string blockName_;
    GeoPoint insertion_;
    double scale_ = 1.0;
    double rotation_ = 0.0;
    double baseX_ = 0.0;
    double baseY_ = 0.0;
    bool allowExplode_ = true;
    bool uniformScale_ = true;
    bool annotative_ = false;
    std::vector<BlockAttributeDef> attributeDefs_;
    std::vector<EntityId> workingIds_;
    bool originalRefHidden_ = false;
};

} // namespace cad
