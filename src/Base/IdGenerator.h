#pragma once

#include "Base/CadTypes.h"
#include <atomic>

namespace cad {

class IdGenerator {
public:
    static EntityId Next();

private:
    static std::atomic<EntityId> nextId_;
};

} // namespace cad
