#pragma once

#include "DataModel/DynamicBlock.h"
#include "DataModel/Entity.h"
#include <memory>
#include <string>
#include <vector>

namespace cad {

struct BlockAttributeDef {
    std::string tag;
    std::string prompt;
    std::string defaultValue;
    double textHeight = 2.5;
    bool invisible = false;
    bool constant = false;
};

struct BlockAttributeValue {
    std::string tag;
    std::string value;
};

enum class BlockCreateMode {
    Keep,
    Convert,
    Delete
};

struct BlockDefinition {
    std::string name;
    double baseX = 0.0;
    double baseY = 0.0;
    bool allowExplode = true;
    bool uniformScale = true;
    bool annotative = false;
    std::vector<std::shared_ptr<Entity>> entities; // geometry relative to base (0,0)
    std::vector<BlockAttributeDef> attributeDefs;
    DynamicBlockData dynamic; // parameters / actions / visibility / lookup
};

} // namespace cad
