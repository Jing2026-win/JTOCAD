#include "DataModel/Entity.h"

namespace cad {

Entity::Entity(EntityId id, EntityType type)
    : id_(id)
    , type_(type)
{
}

} // namespace cad
