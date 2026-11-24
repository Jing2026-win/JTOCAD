#include "Base/IdGenerator.h"

namespace cad {

std::atomic<EntityId> IdGenerator::nextId_{1};

EntityId IdGenerator::Next()
{
    return nextId_.fetch_add(1, std::memory_order_relaxed);
}

} // namespace cad
