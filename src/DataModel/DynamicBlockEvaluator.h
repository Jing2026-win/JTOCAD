#pragma once

#include "DataModel/BlockDefinition.h"
#include "DataModel/DynamicBlock.h"
#include "DataModel/Entity.h"
#include <memory>
#include <vector>

namespace cad {

class DynamicBlockEvaluator {
public:
    // Resolve instance values (fill missing params with defaults, apply lookup rows).
    static std::vector<DynParamValue> ResolveValues(
        const BlockDefinition& definition,
        const std::vector<DynParamValue>& instanceValues);

    // Evaluate definition entities in block-local space for the given resolved values.
    static std::vector<std::unique_ptr<Entity>> Evaluate(
        const BlockDefinition& definition,
        const std::vector<DynParamValue>& resolvedValues);

    static double GetValue(
        const std::vector<DynParamValue>& values,
        const std::string& parameterId,
        double fallback);

    static const DynParameter* FindParameter(
        const DynamicBlockData& data,
        const std::string& id);

    static std::string MakeParameterId(DynParamType type, int serial);
    static std::string MakeActionId(DynActionType type, int serial);

    // Automated smoke test (no UI). Returns true on success.
    static bool RunSelfTest(std::string& report);
};

} // namespace cad
