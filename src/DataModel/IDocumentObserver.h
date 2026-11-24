#pragma once

#include "Base/CadTypes.h"
#include <vector>

namespace cad {

class Entity;

class IDocumentObserver {
public:
    virtual ~IDocumentObserver() = default;

    virtual void OnEntityAdded(const Entity& entity) = 0;
    virtual void OnEntityRemoved(EntityId id) = 0;
    virtual void OnEntitiesRemoved(const std::vector<EntityId>& ids) = 0;
    virtual void OnEntitiesChanged(const std::vector<EntityId>& ids) = 0;
    virtual void OnSelectionChanged() = 0;
    virtual void OnDocumentReset() = 0;
};

} // namespace cad
